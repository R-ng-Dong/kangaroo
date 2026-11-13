#include "IOT_Record.h"

#include <string.h>
#include "IOT_Log.h"
#include "dao/IOT_DaoRecord.h"
#include "IOT_ContractAttrsInternal.h"
#include "IOT_Common.h"
#include "IOT_TimerMgmt.h"
#include "IOT_Osal.h"
#include "IOT_CoreRecord.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "IOT_Core.h"
#include "IOT_ContractEvents.h"
#include <stdint.h>
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

/*
 * Reel push (CONFIG_IOT_RECORD_PUSH_ENABLED, sdkcontract/Kconfig): send each closed reel to
 * the cloud unsolicited. IOT_RECORD_PUSH is 1 when compiled in, 0 otherwise; gate with a plain
 * `if (IOT_RECORD_PUSH)` so the off path is still parsed and type-checked. Off by default.
 */
#ifndef CONFIG_IOT_RECORD_PUSH_ENABLED
#define CONFIG_IOT_RECORD_PUSH_ENABLED 0
#endif
#if CONFIG_IOT_RECORD_PUSH_ENABLED
#define IOT_RECORD_PUSH 1
#else
#define IOT_RECORD_PUSH 0
#endif

static const char *TAG = "RECORD";

#ifndef CONFIG_IOT_RECORD_MIN_HEARTBEAT_SEC
#define CONFIG_IOT_RECORD_MIN_HEARTBEAT_SEC 120
#endif
#define IOT_RECORD_MAX_RULES     16
#define IOT_RECORD_MAX_OVERRIDES 16

typedef struct
{
    bool used;
    IOT_ContractRecordRule_t rule;
} RecordRuleSlot_t;

typedef struct
{
    bool used;
    bool enabled;
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
} RecordElementOverride_t;

static RecordRuleSlot_t s_rules[IOT_RECORD_MAX_RULES];
static RecordElementOverride_t s_overrides[IOT_RECORD_MAX_OVERRIDES];

static bool ruleMatches(const IOT_ContractRecordRule_t *rule, const IOT_ContractRecordEvent_t *event, bool deviceSpecific)
{
    if (rule == NULL || event == NULL)
    {
        return false;
    }
    if (rule->attrId != event->attrId || rule->elemType != event->elemType)
    {
        return false;
    }
    if (deviceSpecific)
    {
        return rule->deviceType != 0 && rule->deviceType == event->deviceType;
    }
    return rule->deviceType == 0;
}

static bool ruleAcceptsValue(const IOT_ContractRecordRule_t *rule, const IOT_ContractRecordEvent_t *event)
{
    if (rule->valueSize != 0 && rule->valueSize != event->valueLen)
    {
        return false;
    }
    if (rule->filter != NULL)
    {
        return rule->filter(event, rule->userCtx);
    }
    return true;
}

static RecordElementOverride_t *findOverride(uint16_t eid, uint16_t elemId, uint16_t attrId)
{
    for (uint8_t i = 0; i < IOT_RECORD_MAX_OVERRIDES; i++)
    {
        if (s_overrides[i].used && s_overrides[i].eid == eid && s_overrides[i].elemId == elemId
            && s_overrides[i].attrId == attrId)
        {
            return &s_overrides[i];
        }
    }
    return NULL;
}

static void resolveTiming(const IOT_ContractRecordRule_t *rule, uint16_t attrId,
                          uint16_t *outMinInterval, uint16_t *outHeartbeat);

iot_err_t IOT_ContractRecordRegisterRule(const IOT_ContractRecordRule_t *rule)
{
    /* Reject rather than silently correct -- an app-writer who asked for a 3-byte width has a
     * wrong idea about the format and should find out at boot. Shared with the Record-off build. */
    if (!IOT_RecordRuleIsValid(rule))
    {
        return IOT_ERR_INVALID_ARG;
    }

    /* Warned once, here, where the app-writer chose the number -- not on every report,
     * which is where resolveTiming() applies the clamp. */
    if (rule->heartbeatSec != IOT_CONTRACT_RECORD_USE_DEFAULT && rule->heartbeatSec != IOT_CONTRACT_RECORD_NO_HEARTBEAT
        && rule->heartbeatSec < CONFIG_IOT_RECORD_MIN_HEARTBEAT_SEC)
    {
        IOT_LOGW(TAG, "Record attr %04x heartbeat %u s is below the %u s floor; clamping. "
                      "A 16-block ring holds 864 entries, so anything faster burns the "
                      "whole chain in hours for a sensor that never changed.",
                 rule->attrId, (unsigned) rule->heartbeatSec,
                 (unsigned) CONFIG_IOT_RECORD_MIN_HEARTBEAT_SEC);
    }

    /* A heartbeat no longer than the window cannot fire on time: closing a window does not
     * look at the heartbeat, so the repeat waits for the first report after the window.
     * Accepted (it still works), but said once, here. Compared after defaults and the floor,
     * because that is what will run. */
    uint16_t minInterval = 0, heartbeat = 0;
    resolveTiming(rule, rule->attrId, &minInterval, &heartbeat);
    if (minInterval != IOT_CONTRACT_RECORD_EVERY_CHANGE && heartbeat != IOT_CONTRACT_RECORD_NO_HEARTBEAT
        && heartbeat <= minInterval)
    {
        IOT_LOGW(TAG, "Record attr %04x heartbeat %u s is not longer than its %u s window; "
                      "the repeat waits for the first report after the window",
                 rule->attrId, (unsigned) heartbeat, (unsigned) minInterval);
    }

    for (uint8_t i = 0; i < IOT_RECORD_MAX_RULES; i++)
    {
        if (s_rules[i].used && s_rules[i].rule.deviceType == rule->deviceType
            && s_rules[i].rule.elemType == rule->elemType && s_rules[i].rule.attrId == rule->attrId)
        {
            s_rules[i].rule = *rule;
            return IOT_OK;
        }
    }

    for (uint8_t i = 0; i < IOT_RECORD_MAX_RULES; i++)
    {
        if (!s_rules[i].used)
        {
            s_rules[i].used = true;
            s_rules[i].rule = *rule;
            return IOT_OK;
        }
    }

    return IOT_ERR_NO_MEM;
}


static IOT_ContractRecordElemIdResolverFn s_elemIdResolver;

iot_err_t IOT_RecordSetElementIdResolver(IOT_ContractRecordElemIdResolverFn fn)
{
    s_elemIdResolver = fn;
    return IOT_OK;
}

iot_err_t IOT_ContractRecordSetElementEnabled(uint16_t eid, uint16_t elemId, uint16_t attrId, bool enabled)
{
    if (!IOT_RecordOverrideIdsAreValid(eid, elemId, attrId))
    {
        return IOT_ERR_INVALID_ARG;
    }

    /* The app names its own elements locally (1..N); events and stored history carry the
     * cloud's numbering, which its model record assigns freely (158, 159, ...). Stored as
     * given, an override for "element 1" would match nothing on such a device -- history
     * off, no error. Translate for the device itself through the resolver the application
     * installed (it owns the local numbering; this component does not). A sub-device's ids
     * already are the cloud's, so they pass through, as does every id with no resolver. */
    uint16_t rootEid = 0;
    if (s_elemIdResolver != NULL && IOT_CoreGetRootEid(&rootEid) == IOT_OK && rootEid == eid)
    {
        elemId = s_elemIdResolver(eid, elemId);
    }

    RecordElementOverride_t *existing = findOverride(eid, elemId, attrId);
    if (existing != NULL)
    {
        existing->enabled = enabled;
        return IOT_OK;
    }

    for (uint8_t i = 0; i < IOT_RECORD_MAX_OVERRIDES; i++)
    {
        if (!s_overrides[i].used)
        {
            s_overrides[i] = (RecordElementOverride_t) {
                .used = true,
                .enabled = enabled,
                .eid = eid,
                .elemId = elemId,
                .attrId = attrId,
            };
            return IOT_OK;
        }
    }

    return IOT_ERR_NO_MEM;
}

/**
 * @brief The rule that governs this element attribute, device-specific first.
 *
 * Shared by the append path and the read path so the two can never disagree about a
 * stride -- a disagreement the app would render as a garbled chart with no error
 * anywhere.
 */
static const IOT_ContractRecordRule_t *findRule(uint16_t deviceType, uint16_t elemType, uint16_t attrId)
{
    const IOT_ContractRecordEvent_t probe = {
        .deviceType = deviceType,
        .elemType = elemType,
        .attrId = attrId,
    };

    for (uint8_t i = 0; i < IOT_RECORD_MAX_RULES; i++)
    {
        if (s_rules[i].used && ruleMatches(&s_rules[i].rule, &probe, true))
            return &s_rules[i].rule;
    }
    for (uint8_t i = 0; i < IOT_RECORD_MAX_RULES; i++)
    {
        if (s_rules[i].used && ruleMatches(&s_rules[i].rule, &probe, false))
            return &s_rules[i].rule;
    }
    return NULL;
}

/**
 * @brief Resolve the capture decision and the rule that made it.
 *
 * Append needs the matched rule, not just the yes/no, because the rule carries
 * the log entry's value width. An exact element override reports no rule, so it
 * falls back to the default width.
 */
static bool resolveCapture(const IOT_ContractRecordEvent_t *event, const IOT_ContractRecordRule_t **outRule)
{
    *outRule = NULL;

    if (event == NULL || event->attrId == 0 || event->value == NULL || event->valueLen == 0)
    {
        return false;
    }

    RecordElementOverride_t *override = findOverride(event->eid, event->elemId, event->attrId);
    if (override != NULL)
    {
        return override->enabled;
    }

    const IOT_ContractRecordRule_t *rule = findRule(event->deviceType, event->elemType, event->attrId);
    if (rule == NULL)
        return false;

    *outRule = rule;
    return ruleAcceptsValue(rule, event);
}

bool IOT_RecordShouldCapture(const IOT_ContractRecordEvent_t *event)
{
    const IOT_ContractRecordRule_t *rule = NULL;
    return resolveCapture(event, &rule);
}

uint8_t IOT_ContractRecordLogValueWidth(uint16_t deviceType, uint16_t elemType, uint16_t attrId)
{
    const IOT_ContractRecordRule_t *rule = findRule(deviceType, elemType, attrId);
    if (rule == NULL || rule->logValueBytes == 0)
        return IOT_CONTRACT_RECORD_DEFAULT_LOG_VALUE_BYTES;
    return rule->logValueBytes;
}

static IOT_ContractRecordAppendOverrideFn s_appendOverride;

iot_err_t IOT_RecordSetAppendOverride(IOT_ContractRecordAppendOverrideFn fn)
{
    s_appendOverride = fn;
    return IOT_OK;
}

bool IOT_RecordHasAppendOverride(void)
{
    return s_appendOverride != NULL;
}

// ============================================================================
// Record V2 — policy, instances and the open block
// ============================================================================
//
// Keyed {eid, elemId, attrId} -- an INSTANCE, not a rule. One rule on a hub matches
// every matching sub-device, so a 20-sub-device hub needs 20 instances.
//
// Two independent knobs, and keeping them apart is the whole design:
//
//   minIntervalSec  ours.         Within this long of a recorded CHANGE, further changes
//                                 are held; the settled value is written at close.
//   heartbeatSec    the cloud's.  An UNCHANGED value is re-recorded after this long.
//
// Conflating them -- taking the cloud's heartbeat numbers and using them as rate limits
// -- leaves no heartbeat at all: a presence sensor reporting every 10 s for an hour
// would give the gateway ~240 entries and this device two.

#ifndef CONFIG_IOT_RECORD_MAX_INSTANCES
#define CONFIG_IOT_RECORD_MAX_INSTANCES 32
#endif
#ifndef CONFIG_IOT_RECORD_MAX_OPEN_BLOCKS
#define CONFIG_IOT_RECORD_MAX_OPEN_BLOCKS 6
#endif
#ifndef CONFIG_IOT_RECORD_BLOCKS_PER_ATTR
#define CONFIG_IOT_RECORD_BLOCKS_PER_ATTR 16
#endif

/* How long a dirty block may sit in RAM before it is written. The timer is armed from
 * the earliest WINDOW deadline, and a rule with IOT_CONTRACT_RECORD_EVERY_CHANGE opens no window
 * at all -- so without this a smoke sensor's entries would sit in RAM until the block
 * rotated, and a power cut would take every one of them. A flush timer exists to bound
 * what a power cut loses. */
#ifndef CONFIG_IOT_RECORD_FLUSH_INTERVAL_SEC
#define CONFIG_IOT_RECORD_FLUSH_INTERVAL_SEC 60
#endif

/** The open block's entry capacity, in uint16s. A multiple of both entry widths, so
 *  either width fills a block exactly. */
#define RECORD_BLOCK_FIELDS 108

/** diffSeconds is a 16-bit offset from the block's checkpoint. Past this, rotate. */
#define RECORD_MAX_DIFF_SECONDS 65535

typedef struct
{
    bool used;
    uint16_t eid, elemId, attrId;
    uint8_t valueCount;

    uint64_t checkpoint;          /* of the open block, epoch ms */
    uint16_t sequence;            /* monotonic; what boot sorts by */

    /* True while every time field above came from the monotonic clock because the wall
     * clock had not synced yet. Such a block is NEVER written to storage -- a checkpoint in 1970
     * would stay in the chain forever -- and is corrected in place once the clock
     * syncs. See recordNowMs(). */
    bool provisional;
    /** Latches the open-block budget warning, so it is logged once per instance. */
    bool budgetWarned;

    int32_t windowDeadline;       /* epoch SECONDS, signed */
    int32_t heartbeatDeadline;    /* epoch SECONDS, signed */

    uint16_t lastValue[2];
    bool haveLastValue;

    bool pending;
    uint16_t pendingValue[2];
    int32_t pendingTimeSec;       /* when the pending value ARRIVED -- not when the
                                   * window closes. Without it the entry cannot be
                                   * stamped truthfully. */

    uint8_t ring[CONFIG_IOT_RECORD_BLOCKS_PER_ATTR];  /* slot indices, newest first */
    uint16_t ringSeq[CONFIG_IOT_RECORD_BLOCKS_PER_ATTR]; /* each slot's sequence, for boot ordering */
    uint8_t ringCount;

    /* Push bookmark: the sequence of the newest reel the cloud has accepted. A closed
     * reel (ring index >= 1) is unsent when !havePushed or its ringSeq is newer than
     * pushedSeq, wrap-aware. Persisted with IOT_DaoRecordPutPushed() and reloaded at boot;
     * havePushed false -- a fresh instance, or an upgrade from a build without push --
     * means every closed reel in the ring is unsent. Zeroed with the rest of the instance
     * by admitInstance() and IOT_ContractRecordClearRules(). */
    uint16_t pushedSeq;
    bool havePushed;

    bool dirty;

    /* When this instance last wrote an entry. An instance quiet for a whole flush
     * interval gives its block buffer back; an active one keeps it, so the
     * reclaim does not become a malloc/free per flush cycle. */
    int32_t lastAppendSec;

    /* Resolved once per append and kept, because the window close happens on the timer
     * task with no event in hand: findRule() needs deviceType and elemType, and the
     * instance key carries neither. Re-deriving from attrId alone would silently fall
     * back to the attribute defaults and ignore the app's rule. */
    uint16_t minIntervalSec;
    uint16_t heartbeatSec;

    /* Allocated LAZILY and freed when the instance flushes and goes idle. The resident
     * part of an instance is small; the block buffer exists only while one is actively
     * recording, so a hub with many quiet children pays only for the active ones. */
    uint16_t *fields;
    uint16_t fieldCount;
} RecordState_t;

static RecordState_t s_instances[CONFIG_IOT_RECORD_MAX_INSTANCES];
static uint8_t s_openBlocks;

/*
 * One mutex over the whole table, held for any append, window close, flush, rotation or
 * read.
 *
 * More than one task touches RecordState_t: whichever delivers the event, and the core work
 * task sweeping windows or serving a GET. Without the lock, a rotation that has updated
 * `checkpoint` but not yet cleared `fieldCount` can be observed mid-flight, and the
 * reader ships the new checkpoint with the old block's entries -- every diffSeconds
 * measured from the wrong origin, and a chart offset by the block's whole lifetime.
 *
 * Eviction and a GET are the same hazard: the key must not be removed between the
 * reader's ring snapshot and its IOT_DaoRecordGet().
 */
static IOT_OsalMutexHandle_t s_recordMutex;

/* Set when IOT_RecordInit() could not create the mutex. Recording is then OFF rather
 * than unlocked: tasks racing the instance table produce exactly the
 * half-rotated-block corruption the mutex exists to prevent, and the caller may ignore
 * IOT_RecordInit()'s return. */
static bool s_recordDisabled;

/* One global one-shot, armed for the earliest deadline across all instances. Nothing
 * pending, nothing armed: a periodic tick would wake the CPU 86,400 times a day to find
 * no work. */
static IOT_TimerHandle_t s_recordTimer;
/* When s_recordTimer fires, in Record's epoch seconds; 0 = not known, so the next
 * rearmTimer() arms afresh. See rearmTimer() for why an armed wake is never moved later. */
static int32_t s_recordTimerDueSec;

#define RECORD_LOCK_TIMEOUT_MS 1000

static bool recordLock(void)
{
    if (s_recordMutex == NULL)
    {
        /* Before IOT_RecordInit(), or on a build where OSAL could not supply one. Not
         * silently proceeding unlocked: on a single-threaded host build there is nothing
         * to race, and on a device a missing mutex is a boot fault worth seeing. */
        return true;
    }
    return IOT_OsalMutexLock(s_recordMutex, RECORD_LOCK_TIMEOUT_MS) == IOT_OK;
}

static void recordUnlock(void)
{
    if (s_recordMutex != NULL)
    {
        IOT_OsalMutexUnlock(s_recordMutex);
    }
}

/* ---- clock seam ----
 *
 * The provisional timebase is IOT_OsalGetUptimeUs(): int64 microseconds since boot,
 * lock-free, valid from the first instruction. Not IOT_TimerGetCurrentTimeMs(), which is
 * uint32 (wraps every ~49.7 days, so it needs counting logic that can still be fooled) and
 * may depend on state core creates after IOT_RecordInit() has run. Neither problem exists
 * on a 64-bit clock with a fixed origin.
 *
 * Biased by one so it can never be 0. checkpoint == 0 is the "this instance has no block
 * yet" sentinel, and uptime reads 0 at boot: without the bias a value recorded in the
 * first millisecond would get a checkpoint indistinguishable from the sentinel, and the
 * next append would open a SECOND block for an instance that already had one, claiming a
 * second ring slot. Correction subtracts against this same function, so the bias cancels. */
static uint64_t provisionalNowMs(void)
{
    return (uint64_t) (IOT_OsalGetUptimeUs() / 1000) + 1u;
}

static iot_err_t recordNowMs(uint64_t *outMs, bool *outProvisional)
{
    if (!IOT_TimerIsTimeSynced())
    {
        /* Record anyway, against the monotonic clock, and mark it. Refusing here would
         * cost more than a slow start: a device that never syncs its clock would record
         * NOTHING, for its whole life, with no error at any layer.
         *
         * Safe because diffSeconds is an offset from the block's checkpoint, so fixing
         * the checkpoint fixes every entry in that block, and because a provisional
         * block never reaches storage -- see flushInstance(). */
        *outMs = provisionalNowMs();
        if (outProvisional != NULL) *outProvisional = true;
        return IOT_OK;
    }
    if (outProvisional != NULL) *outProvisional = false;
    return IOT_TimerGetU64UnixTimeMs(outMs);
}

static int32_t msToSec(uint64_t ms)
{
    return (int32_t) (ms / 1000ULL);
}

// ---- defaults ----------------------------------------------------------------

void IOT_RecordGetAttrDefaults(uint16_t attrId, uint16_t *outMinIntervalSec,
                               uint16_t *outHeartbeatSec)
{
    /* Written with the named constants, so no default is itself a bare 0 and there is no
     * recursion between "use the default" and the default. */
    uint16_t minInterval = 60u;
    uint16_t heartbeat = IOT_CONTRACT_RECORD_NO_HEARTBEAT;

    switch (attrId)
    {
    /* Measurements: rate-limited, because the signal is continuous and every sample is
     * a "change". */
    case IOT_ATTR_TEMP_EVT:
    case IOT_ATTR_HUMID_EVT:
    case IOT_ATTR_TEMP_HUMID_EVT:
        minInterval = 60u;
        heartbeat = 43200u;   /* 12 h */
        break;
    case IOT_ATTR_POWER_VOLTAGE:
    case IOT_ATTR_POWER_CONSUMED:
    case IOT_ATTR_POWER_CONSUMED_INSTANCE:
    case IOT_ATTR_POWER_CURRENT_INSTANCE:
    case IOT_ATTR_LUX_EVT:
        minInterval = 60u;
        heartbeat = IOT_CONTRACT_RECORD_NO_HEARTBEAT;
        break;

    /* Events: never rate-limited, because holding one risks losing a transition. */
    case IOT_ATTR_MOTION_EVT:
    case IOT_ATTR_MOTION_LUX_EVT:
    case IOT_ATTR_EVT_PRESENCE:
    case IOT_ATTR_EVT_PRESENCE_STATE:
        minInterval = IOT_CONTRACT_RECORD_EVERY_CHANGE;
        heartbeat = 300u;     /* 5 min -- 288 points a day, twenty times any chart */
        break;
    case IOT_ATTR_OPEN_CLOSE_EVT:
        minInterval = IOT_CONTRACT_RECORD_EVERY_CHANGE;
        heartbeat = 3600u;    /* 1 h */
        break;
    /* Actuators and discrete events. ONOFF belongs here: in the measurement branch below
     * it would get a 60-second rate limit, so a relay switched ON/OFF/ON/OFF inside one
     * window would record the first ON and the settled OFF and lose both middle
     * transitions.
     *
     * The rule is the reason: rate-limit measurements, never events. A
     * measurement is a continuous signal where every sample is a "change"; an event
     * is sparse, and holding one risks losing exactly the transition the history
     * exists to record. */
    case IOT_ATTR_ONOFF:
    case IOT_ATTR_SMOKE_EVT:
    case IOT_ATTR_OPEN_CLOSE_CTL:
    case IOT_ATTR_LOCK_UNLOCK:
    case IOT_ATTR_BUTTON_PRESS_EVT:
    case IOT_ATTR_WALL_MOUNTED_EVT:
        minInterval = IOT_CONTRACT_RECORD_EVERY_CHANGE;
        heartbeat = IOT_CONTRACT_RECORD_NO_HEARTBEAT;
        break;

    default:
        /* Anything UNDECLARED is treated as a measurement: the rate limit is the safe
         * default there, because an unbounded unknown stream burns the ring while a
         * thinned chart merely loses detail. A known event must be listed above --
         * falling through to here would cost it its transitions. */
        minInterval = 60u;
        heartbeat = IOT_CONTRACT_RECORD_NO_HEARTBEAT;
        break;
    }

    if (outMinIntervalSec != NULL) *outMinIntervalSec = minInterval;
    if (outHeartbeatSec != NULL) *outHeartbeatSec = heartbeat;
}

/* Resolve a rule's timing against the attribute's defaults, applying the heartbeat
 * floor. A rule that mentions neither field inherits both. */
static void resolveTiming(const IOT_ContractRecordRule_t *rule, uint16_t attrId,
                          uint16_t *outMinInterval, uint16_t *outHeartbeat)
{
    uint16_t defMin = 0, defHb = 0;
    IOT_RecordGetAttrDefaults(attrId, &defMin, &defHb);

    uint16_t minInterval = (rule != NULL && rule->minIntervalSec != IOT_CONTRACT_RECORD_USE_DEFAULT)
                               ? rule->minIntervalSec : defMin;
    uint16_t heartbeat = (rule != NULL && rule->heartbeatSec != IOT_CONTRACT_RECORD_USE_DEFAULT)
                             ? rule->heartbeatSec : defHb;

    /* The floor, clamped rather than rejected: an app-writer cannot be expected to know
     * the ring's capacity, and failing their registration over it
     * would break a device for a number they had no way to derive. Silent here: this
     * runs on every append, and IOT_ContractRecordRegisterRule() has already said it once. */
    if (heartbeat != IOT_CONTRACT_RECORD_NO_HEARTBEAT && heartbeat < CONFIG_IOT_RECORD_MIN_HEARTBEAT_SEC)
    {
        heartbeat = CONFIG_IOT_RECORD_MIN_HEARTBEAT_SEC;
    }

    *outMinInterval = minInterval;
    *outHeartbeat = heartbeat;
}

// ---- instances ---------------------------------------------------------------

static RecordState_t *findInstance(uint16_t eid, uint16_t elemId, uint16_t attrId)
{
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        if (s_instances[i].used && s_instances[i].eid == eid
            && s_instances[i].elemId == elemId && s_instances[i].attrId == attrId)
        {
            return &s_instances[i];
        }
    }
    return NULL;
}

static RecordState_t *admitInstance(uint16_t eid, uint16_t elemId, uint16_t attrId,
                                    uint8_t valueCount)
{
    RecordState_t *inst = findInstance(eid, elemId, attrId);
    if (inst != NULL)
    {
        return inst;
    }
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        if (!s_instances[i].used)
        {
            memset(&s_instances[i], 0, sizeof(s_instances[i]));
            s_instances[i].used = true;
            s_instances[i].eid = eid;
            s_instances[i].elemId = elemId;
            s_instances[i].attrId = attrId;
            s_instances[i].valueCount = valueCount;
            return &s_instances[i];
        }
    }
    /* WARN and skip, never silently. Registration is unaffected -- it is instances that
     * run out, not rules, and conflating the two is how an app-writer concludes their
     * rule was rejected when it was not. */
    IOT_LOGW(TAG, "Record instance table full (%u); not recording EID %04x elem %04x "
                  "attr %04x",
             (unsigned) CONFIG_IOT_RECORD_MAX_INSTANCES, eid, elemId, attrId);
    return NULL;
}

/* Open blocks are a separate budget from instances: a block buffer for every instance
 * would be several KB resident, and most instances are idle most of the time. */
/* Free slots, counting an instance that already exists as needing none. Used to decide
 * the temp/humid pair's admission as a pair rather than one at a time. */
static uint8_t freeInstanceSlotsFor(uint16_t eid, uint16_t elemId,
                                    const uint16_t *attrIds, uint8_t attrCount)
{
    uint8_t needed = 0;
    for (uint8_t a = 0; a < attrCount; a++)
    {
        if (findInstance(eid, elemId, attrIds[a]) == NULL) needed++;
    }
    uint8_t free_ = 0;
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        if (!s_instances[i].used) free_++;
    }
    return (free_ >= needed) ? needed : 0xFF;   /* 0xFF = cannot admit them all */
}

static void releaseOpenBlock(RecordState_t *inst);
static iot_err_t flushInstance(RecordState_t *inst, bool async);

/* The least recently active buffer that may be lent: nothing held in a window, and not
 * provisional (a provisional block cannot be written, so RAM is its only copy). With
 * wantDirty false only CLEAN buffers qualify -- already a copy of storage. */
static RecordState_t *bufferToLend(const RecordState_t *borrower, bool wantDirty)
{
    RecordState_t *best = NULL;
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        RecordState_t *c = &s_instances[i];
        if (c == borrower || !c->used || c->fields == NULL || c->pending || c->provisional
            || c->dirty != wantDirty)
        {
            continue;
        }
        if (best == NULL || c->lastAppendSec < best->lastAppendSec)
        {
            best = c;
        }
    }
    return best;
}

static bool ensureOpenBlock(RecordState_t *inst)
{
    if (inst->fields != NULL)
    {
        return true;
    }
    /* The budget bounds RAM, not who may record: a clean buffer is a copy of what storage
     * holds, so lend it and let its owner reload on its next value. Refusing instead
     * would drop the first value after a reboot whenever boot had reopened as many
     * blocks as the budget allows, none of them being written to. */
    if (s_openBlocks >= CONFIG_IOT_RECORD_MAX_OPEN_BLOCKS)
    {
        RecordState_t *lender = bufferToLend(inst, false);
        if (lender == NULL)
        {
            /* All dirty: a burst touching more attributes than the budget. Write the least
             * recently active one out, and lend it once it is clean. Async is safe: the
             * DAO serves reads from the same FIFO queue, so its owner's reload sees this
             * write. A failed write keeps it dirty, and it is not lent. */
            lender = bufferToLend(inst, true);
            if (lender != NULL && (flushInstance(lender, true) != IOT_OK || lender->dirty))
            {
                lender = NULL;
            }
        }
        if (lender != NULL)
        {
            releaseOpenBlock(lender);
        }
    }
    if (s_openBlocks >= CONFIG_IOT_RECORD_MAX_OPEN_BLOCKS)
    {
        /* Latched. An unsynced device retries on EVERY report, and a hub with many
         * children would otherwise log this continuously for the whole boot. */
        if (!inst->budgetWarned)
        {
            inst->budgetWarned = true;
            IOT_LOGW(TAG, "Record open-block budget full (%u); not recording EID %04x "
                          "elem %04x attr %04x",
                     (unsigned) CONFIG_IOT_RECORD_MAX_OPEN_BLOCKS, inst->eid,
                     inst->elemId, inst->attrId);
        }
        return false;
    }
    inst->budgetWarned = false;
    inst->fields = (uint16_t *) Mem_SafeMalloc(RECORD_BLOCK_FIELDS * sizeof(uint16_t),
                                               TAG, "recordBlock");
    if (inst->fields == NULL)
    {
        return false;
    }
    inst->fieldCount = 0;
    s_openBlocks++;

    /* An instance with a current block in storage CONTINUES it. Its buffer was handed back
     * idle or lent, and starting empty on the same checkpoint and slot would make the next
     * flush write the new entries over the stored ones: every attribute quiet for a flush
     * interval would lose its open block. If the stored copy cannot be read, refuse the value
     * rather than overwrite a block we cannot see. */
    if (inst->checkpoint != 0 && inst->ringCount > 0 && !inst->provisional)
    {
        IOT_DaoRecordBlock_t stored = {0};
        const iot_err_t err = IOT_DaoRecordGet(inst->eid, inst->elemId, inst->attrId, inst->ring[0],
                                               inst->valueCount, &stored);
        if (err != IOT_OK)
        {
            IOT_LOGW(TAG, "Record: cannot reload the open block of EID %04x elem %04x attr %04x "
                          "(%d); not recording rather than overwriting it",
                     inst->eid, inst->elemId, inst->attrId, (int) err);
            SAFE_FREE(stored.fields);
            releaseOpenBlock(inst);
            return false;
        }
        const uint16_t n = (stored.fieldCount > RECORD_BLOCK_FIELDS) ? RECORD_BLOCK_FIELDS
                                                                     : stored.fieldCount;
        if (n > 0 && stored.fields != NULL)
        {
            memcpy(inst->fields, stored.fields, n * sizeof(uint16_t));
            inst->fieldCount = n;
        }
        SAFE_FREE(stored.fields);
    }
    return true;
}

static void releaseOpenBlock(RecordState_t *inst)
{
    if (inst->fields != NULL)
    {
        SAFE_FREE(inst->fields);
        inst->fields = NULL;
        inst->fieldCount = 0;
        if (s_openBlocks > 0) s_openBlocks--;
    }
}

static uint8_t fieldsPerEntry(const RecordState_t *inst)
{
    return (uint8_t) (inst->valueCount + 1);
}

static bool blockIsFull(const RecordState_t *inst)
{
    return (uint16_t) (inst->fieldCount + fieldsPerEntry(inst)) > RECORD_BLOCK_FIELDS;
}

// ---- storage ------------------------------------------------------------------

/* Correct an instance whose time fields came from the monotonic clock, now that the
 * wall clock is real. Caller holds the lock.
 *
 * Called at the point of USE -- append, flush, the timer sweep, serve -- rather than on
 * a sync edge. There is no time-sync event to hook (only the IOT_TimerIsTimeSynced()
 * predicate), and an edge detected on the Record timer would be unreliable anyway:
 * rearmTimer() arms nothing when no deadline is pending, so the correction could be
 * delayed arbitrarily. Correcting when the instance is next touched is necessarily
 * BEFORE it can be persisted, served or appended to, which is the only ordering that
 * matters.
 *
 * One delta fixes everything because every time field was read from the same provisional
 * clock, and because diffSeconds is an offset from the checkpoint -- so the entries
 * themselves need no rewrite at all. */
static void correctIfSyncedLocked(RecordState_t *inst)
{
    if (inst == NULL || !inst->used || !inst->provisional || !IOT_TimerIsTimeSynced())
    {
        return;
    }
    uint64_t realNowMs = 0;
    if (IOT_TimerGetU64UnixTimeMs(&realNowMs) != IOT_OK)
    {
        return;
    }
    /* Signed: a stale RTC can read AHEAD of real time, giving a negative delta. */
    const int64_t deltaMs = (int64_t) realNowMs - (int64_t) provisionalNowMs();
    const int32_t deltaSec = (int32_t) (deltaMs / 1000);

    /* checkpoint == 0 is the "no block yet" sentinel, exactly like heartbeatDeadline's
     * INT32_MAX below. Shifting it makes an instance that never opened a block look as
     * if it had one, so ensureOpenBlock()'s claimRingSlot() never runs: entries get
     * written to slot 0 but ringCount stays 0, and every history query answers empty.
     * Reachable whenever the open-block budget refused the instance. */
    if (inst->checkpoint != 0)
    {
        const int64_t shifted = (int64_t) inst->checkpoint + deltaMs;
        inst->checkpoint = (shifted > 0) ? (uint64_t) shifted : 1u;
    }

    inst->windowDeadline += deltaSec;
    inst->pendingTimeSec += deltaSec;
    inst->lastAppendSec += deltaSec;
    /* INT32_MAX is the "no heartbeat" sentinel, not a deadline. Shifting it would
     * overflow into a deadline that fires at once. */
    if (inst->heartbeatDeadline != INT32_MAX)
    {
        inst->heartbeatDeadline += deltaSec;
    }

    inst->provisional = false;
    IOT_LOGI(TAG, "Record: corrected provisional checkpoint for EID %04x elem %04x "
                  "attr %04x", inst->eid, inst->elemId, inst->attrId);
}

static iot_err_t flushInstance(RecordState_t *inst, bool async)
{
    correctIfSyncedLocked(inst);
    if (!inst->used || !inst->dirty || inst->fields == NULL)
    {
        return IOT_OK;
    }
    /* A provisional checkpoint came from the monotonic clock, so writing it would put a
     * block stamped near 1970 into the chain permanently -- and every later block's prev
     * would point at it. Hold it in RAM instead; it is corrected and written once the
     * clock syncs. Worst case the device never syncs and the data is lost at power-off,
     * which is strictly better than not recording it at all. */
    if (inst->provisional)
    {
        return IOT_ERR_INVALID_STATE;
    }
    IOT_DaoRecordBlock_t block = {
        .checkpoint = inst->checkpoint,
        .sequence = inst->sequence,
        .fields = inst->fields,
        .fieldCount = inst->fieldCount,
    };
    const uint8_t slot = (inst->ringCount > 0) ? inst->ring[0] : 0;
    const iot_err_t err = IOT_DaoRecordPut(inst->eid, inst->elemId, inst->attrId, slot,
                                           &block, async);
    if (err == IOT_OK)
    {
        /* Cleared on SUCCESS, never on queueing. The storage queue is bounded, and
         * several instances flushing on one tick plus ordinary storage traffic can fill
         * it; clearing on queue would drop the block with nothing said. */
        inst->dirty = false;
    }
    else
    {
        IOT_LOGW(TAG, "Record flush failed for EID %04x elem %04x attr %04x (%s); "
                      "staying dirty and retrying",
                 inst->eid, inst->elemId, inst->attrId, iot_err_to_name(err));
    }
    return err;
}

static iot_err_t flushAllLockedMode(bool async)
{
    iot_err_t last = IOT_OK;
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        /* A provisional instance is not a failure to flush, it is a block that is not
         * ready to be written yet. Skipping it here rather than letting
         * flushInstance()'s IOT_ERR_INVALID_STATE out keeps "flush everything" an
         * honest answer: the caller asked for everything writable, and got it. */
        correctIfSyncedLocked(&s_instances[i]);
        if (s_instances[i].provisional)
        {
            continue;
        }
        const iot_err_t err = flushInstance(&s_instances[i], async);
        if (err != IOT_OK) last = err;
    }
    return last;
}

iot_err_t IOT_RecordFlushAll(void)
{
    if (!recordLock())
    {
        return IOT_ERR_TIMEOUT;
    }
    const iot_err_t err = flushAllLockedMode(true);
    recordUnlock();
    return err;
}

static void closeHeldWindowLocked(RecordState_t *inst, int32_t nowSec);

/* The last write -- a planned restart, or the power about to go -- also closes every held
 * window early. A held value lives only in the instance, not in the block, so flushing dirty
 * blocks alone would lose it. Corrected first, as the sweep does, so the close compares and
 * re-arms on the real timebase. */
static void closeHeldWindowsNowLocked(void)
{
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        correctIfSyncedLocked(&s_instances[i]);
    }
    uint64_t nowMs = 0;
    if (recordNowMs(&nowMs, NULL) != IOT_OK)
    {
        return;
    }
    const int32_t nowSec = msToSec(nowMs);
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        if (s_instances[i].used && s_instances[i].pending)
        {
            closeHeldWindowLocked(&s_instances[i], nowSec);
        }
    }
}

iot_err_t IOT_RecordFlushAllSync(void)
{
    if (!recordLock())
    {
        return IOT_ERR_TIMEOUT;
    }
    closeHeldWindowsNowLocked();
    const iot_err_t err = flushAllLockedMode(false);
    recordUnlock();
    return err;
}

/* True when sequence a is newer than b. Sequences are 16-bit and wrap, so compare the
 * signed distance rather than the raw values. */
static bool recordSeqNewer(uint16_t a, uint16_t b)
{
    return (int16_t) (uint16_t) (a - b) > 0;
}

/* Claim the next ring slot, evicting the oldest when the ring is full. Slots are indices
 * 0..N-1; the ring holds them newest first. */
static uint8_t claimRingSlot(RecordState_t *inst)
{
    uint8_t slot;
    if (inst->ringCount < CONFIG_IOT_RECORD_BLOCKS_PER_ATTR)
    {
        slot = inst->ringCount;
        inst->ringCount++;
    }
    else
    {
        /* Full: the oldest block's slot is reused, and the block that was second-oldest
         * becomes the new oldest and will report prev = 0. */
        slot = inst->ring[CONFIG_IOT_RECORD_BLOCKS_PER_ATTR - 1];
    }
    for (uint8_t i = (uint8_t) (inst->ringCount - 1); i > 0; i--)
    {
        inst->ring[i] = inst->ring[i - 1];
        inst->ringSeq[i] = inst->ringSeq[i - 1];
    }
    inst->ring[0] = slot;
    inst->ringSeq[0] = inst->sequence;
    return slot;
}

// ---- appending ----------------------------------------------------------------

static void appendEntry(RecordState_t *inst, const uint16_t *values, int32_t atSec)
{
    const int32_t checkpointSec = msToSec(inst->checkpoint);
    int32_t diff = atSec - checkpointSec;
    if (diff < 0) diff = 0;
    if (diff > RECORD_MAX_DIFF_SECONDS) diff = RECORD_MAX_DIFF_SECONDS;

    uint16_t *slotPtr = &inst->fields[inst->fieldCount];
    for (uint8_t v = 0; v < inst->valueCount; v++)
    {
        slotPtr[v] = values[v];
    }
    slotPtr[inst->valueCount] = (uint16_t) diff;
    inst->fieldCount = (uint16_t) (inst->fieldCount + fieldsPerEntry(inst));

    for (uint8_t v = 0; v < inst->valueCount; v++)
    {
        inst->lastValue[v] = values[v];
    }
    inst->haveLastValue = true;
    inst->dirty = true;
    inst->lastAppendSec = atSec;
}

/* Blocks closed by the append in progress, announced once the lock is released (a subscriber must
 * never run under it). One append closes at most two: the temperature/humidity pair. Written and
 * read only under the lock. */
typedef struct
{
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
} RecordClosed_t;
static RecordClosed_t s_closed[2];
static uint8_t s_closedCount;

static void noteClosedLocked(const RecordState_t *inst)
{
    if (s_closedCount < sizeof(s_closed) / sizeof(s_closed[0]))
    {
        s_closed[s_closedCount].eid = inst->eid;
        s_closed[s_closedCount].elemId = inst->elemId;
        s_closed[s_closedCount].attrId = inst->attrId;
        s_closedCount++;
    }
}

/* Rotation, in this order. Step 1 is the easy one to miss. */
static bool rotateBlock(RecordState_t *inst, uint64_t nowMs, uint16_t minInterval,
                        uint16_t heartbeat)
{
    const bool hadPending = inst->pending;
    uint16_t pendingCopy[2] = {0};
    int32_t pendingAt = 0;
    if (hadPending)
    {
        pendingCopy[0] = inst->pendingValue[0];
        pendingCopy[1] = inst->pendingValue[1];
        pendingAt = inst->pendingTimeSec;
    }

    /* 2. flush the block that is finishing -- and if it could not be written, ABORT.
     *
     * flushInstance() keeps `dirty` when the storage queue is full (several instances
     * flushing on one tick can fill it), and the flush is retried next tick. Rotating
     * anyway would clear fieldCount and re-point ring[0], so the unwritten entries would
     * be gone and the retry would write the NEW block instead -- a full block lost behind
     * one WARN.
     *
     * Returning false leaves the old block open and dirty. The caller appends into it,
     * which is wrong by one entry's placement and right by a whole block of data; the next
     * attempt rotates properly once the queue drains. */
    if (flushInstance(inst, true) != IOT_OK)
    {
        IOT_LOGW(TAG, "Rotation deferred for EID %04x elem %04x attr %04x: the block "
                      "could not be written and would have been discarded",
                 inst->eid, inst->elemId, inst->attrId);
        if (hadPending)
        {
            /* Put it back -- the window close still owns it. */
            inst->pending = true;
            inst->pendingValue[0] = pendingCopy[0];
            inst->pendingValue[1] = pendingCopy[1];
            inst->pendingTimeSec = pendingAt;
        }
        return false;
    }

    /* 3-4. take the next slot (evicting the oldest if full) and open there */
    inst->sequence++;
    claimRingSlot(inst);
    inst->checkpoint = nowMs;
    inst->provisional = !IOT_TimerIsTimeSynced();
    inst->fieldCount = 0;
    inst->dirty = true;

    const int32_t nowSec = msToSec(nowMs);

    /* 1, applied into the NEW block. A window can still be open when a block rotates;
     * discarding the pending value there loses exactly the transition the hold exists to
     * preserve, and writing it unchanged into the new block stamps it at the wrong time. */
    if (hadPending)
    {
        inst->pending = false;
        appendEntry(inst, pendingCopy, pendingAt);
    }

    /* 5. re-arm both deadlines against the value just written */
    inst->windowDeadline = (minInterval == IOT_CONTRACT_RECORD_EVERY_CHANGE)
                               ? nowSec : (int32_t) (nowSec + minInterval);
    inst->heartbeatDeadline = (heartbeat == IOT_CONTRACT_RECORD_NO_HEARTBEAT)
                                  ? INT32_MAX : (int32_t) (nowSec + heartbeat);
    return true;
}

/* Read up to two 16-bit fields out of the event's big-endian value bytes. */
static uint8_t readValues(const IOT_ContractRecordEvent_t *event, uint8_t valueCount, uint16_t *out)
{
    uint8_t got = 0;
    for (uint8_t v = 0; v < valueCount; v++)
    {
        const uint16_t off = (uint16_t) (v * 2);
        /* Both bytes of the field must be inside the buffer: a 3-byte value with two
         * fields holds only one complete field, and reading the second would take a byte
         * past the end. Reachable from a sub-device report of any length. */
        if ((uint16_t) (off + 2) > event->valueLen)
        {
            break;
        }
        out[v] = (uint16_t) ((event->value[off] << 8) | event->value[off + 1]);
        got++;
    }
    return got;
}

static bool valuesEqual(const uint16_t *a, const uint16_t *b, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        if (a[i] != b[i]) return false;
    }
    return true;
}

/* Record one attribute's value into its own instance. TEMP_HUMID_EVT calls this twice,
 * which is why it is split out. */
static iot_err_t recordOneAttr(const IOT_ContractRecordEvent_t *event, const IOT_ContractRecordRule_t *rule,
                               uint16_t attrId, const uint16_t *values, uint8_t valueCount)
{
    uint64_t nowMs = 0;
    bool provisional = false;
    if (recordNowMs(&nowMs, &provisional) != IOT_OK)
    {
        return IOT_OK;
    }
    const int32_t nowSec = msToSec(nowMs);

    RecordState_t *inst = admitInstance(event->eid, event->elemId, attrId, valueCount);
    if (inst == NULL)
    {
        return IOT_ERR_NO_MEM;
    }
    /* Before anything reads its deadlines or its checkpoint. */
    correctIfSyncedLocked(inst);
    /* `provisional` describes the timebase of the BLOCK, not of this clock read.
     *
     * An instance rebuilt at boot carries a REAL epoch checkpoint and a clean block. A
     * value arriving before the clock syncs cannot be placed on that block -- its offset
     * would be meaningless -- and marking the instance provisional would be far worse: the
     * correction would add the whole epoch delta to an already-real checkpoint and
     * flushInstance() would persist a block stamped decades in the future.
     *
     * Skip the value instead, in this one case only -- a fresh instance (checkpoint 0)
     * still records, which is the case pre-sync recording is for. */
    if (provisional && inst->checkpoint != 0 && !inst->provisional)
    {
        return IOT_OK;
    }
    if (!ensureOpenBlock(inst))
    {
        return IOT_ERR_NO_MEM;
    }

    uint16_t minInterval = 0, heartbeat = 0;
    resolveTiming(rule, attrId, &minInterval, &heartbeat);
    inst->minIntervalSec = minInterval;
    inst->heartbeatSec = heartbeat;

    /* First value for this instance: open the block here. Every block opens with an
     * entry. */
    if (inst->checkpoint == 0)
    {
        inst->checkpoint = nowMs;
        inst->provisional = !IOT_TimerIsTimeSynced();
        inst->sequence++;
        claimRingSlot(inst);
    }

    const bool insideWindow = (minInterval != IOT_CONTRACT_RECORD_EVERY_CHANGE)
                              && inst->haveLastValue && nowSec < inst->windowDeadline;
    const bool changed = !inst->haveLastValue || !valuesEqual(values, inst->lastValue, valueCount);

    if (insideWindow)
    {
        /* HOLD, do not drop -- and hold whatever arrived, changed or not. The value
         * written at close is the SETTLED one, so a reading that wanders away and comes
         * back must leave the pending value equal to what was already recorded, where
         * the close then writes nothing. Keeping only the first deviation would record a
         * transition the sensor had already undone.
         *
         * The arrival time is kept because the entry is stamped from it: stamping at
         * close moves every held value forward by up to a whole window. */
        inst->pending = true;
        inst->pendingValue[0] = values[0];
        inst->pendingValue[1] = (valueCount > 1) ? values[1] : 0;
        inst->pendingTimeSec = nowSec;
        return IOT_OK;
    }

    /* Decide whether this value earns an entry BEFORE touching the block. Rotation is
     * lazy for the same reason: a report that produces no entry must not consume a ring
     * slot, or a silent no-heartbeat sensor polled every hour would burn the whole chain
     * on blocks that never got an entry. */
    bool writing = changed;
    if (!changed)
    {
        /* The heartbeat is evaluated HERE and nowhere else -- no timer. A sensor that goes
         * silent simply stops producing entries. */
        writing = (heartbeat != IOT_CONTRACT_RECORD_NO_HEARTBEAT) && (nowSec >= inst->heartbeatDeadline);
    }
    if (!writing)
    {
        return IOT_OK;
    }

    /* A BACKWARD clock step is its own rotation trigger: a negative delta masks to a huge
     * timestamp, the "diff > 65535" test never fires because negative > 65535 is false,
     * and the deadline goes negative so every later report records -- a corrupt entry
     * plus a permanent flood. */
    const int32_t checkpointSec = msToSec(inst->checkpoint);
    const bool steppedBack = nowSec < checkpointSec;
    const bool tooFarAhead = (nowSec - checkpointSec) > RECORD_MAX_DIFF_SECONDS;
    if (steppedBack || tooFarAhead || blockIsFull(inst))
    {
        if (steppedBack)
        {
            IOT_LOGW(TAG, "Clock stepped back for EID %04x attr %04x; rotating to a new "
                          "block rather than writing a negative offset",
                     event->eid, attrId);
        }
        if (rotateBlock(inst, nowMs, minInterval, heartbeat))
        {
            /* A queue post, so safe under the lock; the send itself happens on the core task. */
            IOT_RecordPushKick();
            noteClosedLocked(inst);
        }
        else
        {
            /* The old block could not be written, so it is still open. Appending into a
             * full one would overrun it; appending into one whose window is merely long
             * is fine and keeps the value. */
            if (blockIsFull(inst))
            {
                return IOT_ERR_NO_MEM;
            }
        }
    }

    appendEntry(inst, values, nowSec);
    inst->pending = false;
    inst->windowDeadline = (minInterval == IOT_CONTRACT_RECORD_EVERY_CHANGE)
                               ? nowSec : (int32_t) (nowSec + minInterval);
    inst->heartbeatDeadline = (heartbeat == IOT_CONTRACT_RECORD_NO_HEARTBEAT)
                                  ? INT32_MAX : (int32_t) (nowSec + heartbeat);
    return IOT_OK;
}

/* Close one held window: write the settled value if it differs from the last recorded one,
 * stamped from when it arrived. Shared by the timer's sweep (the window's own deadline) and
 * the last write before a restart or a power loss, which closes it early. */
static void closeHeldWindowLocked(RecordState_t *inst, int32_t nowSec)
{
    const uint16_t minInterval = inst->minIntervalSec;
    const uint16_t heartbeat = inst->heartbeatSec;

    const bool differs = !inst->haveLastValue
        || !valuesEqual(inst->pendingValue, inst->lastValue, inst->valueCount);
    inst->pending = false;

    if (differs && inst->fields != NULL)
    {
        /* Stamped from when it ARRIVED, not from now. Stamping at close would
         * move every held value forward by up to a whole window. */
        appendEntry(inst, inst->pendingValue, inst->pendingTimeSec);

        /* A close that WRITES re-arms both deadlines: it is a recorded value
         * like any other. Without this the rate limit evaporates after one
         * window -- with minInterval 60 and a change every 2 s, the close at
         * t=60 would leave no window open and t=62 would record immediately.
         *
         * A close that writes NOTHING must re-arm neither, which is the whole
         * reason this sits inside the branch. Re-arming the heartbeat on a
         * no-write close would push its deadline forward by a window every
         * window, so for a sensor reporting faster than minIntervalSec it would
         * advance exactly as fast as the clock and NEVER BE REACHED: one entry
         * and then silence forever. Re-arming the window on a no-write close
         * would make a genuine change arriving a second later wait another full
         * window. */
        inst->windowDeadline = (minInterval == IOT_CONTRACT_RECORD_EVERY_CHANGE)
                                   ? nowSec : (int32_t) (nowSec + minInterval);
        inst->heartbeatDeadline = (heartbeat == IOT_CONTRACT_RECORD_NO_HEARTBEAT)
                                      ? INT32_MAX : (int32_t) (nowSec + heartbeat);
    }
}

/**
 * @brief Close any window whose deadline has passed, writing the settled value.
 *
 * Called from the Record timer. Returns the earliest remaining deadline in epoch
 * seconds, or 0 when nothing is pending -- the caller arms a one-shot for it, so an idle
 * device arms nothing at all.
 */
static int32_t serviceWindowsLocked(void)
{
    /* Correct FIRST, in a pass of its own.
     *
     * recordNowMs() below returns REAL epoch seconds once the clock has synced, and the
     * loop after it both compares against and re-arms windowDeadline/heartbeatDeadline.
     * Correcting afterwards would add the epoch delta to deadlines already on the real
     * timebase: both would overflow int32_t and go negative, permanently disabling the
     * rate limit (nowSec < windowDeadline never true again) and pinning the heartbeat
     * open (nowSec >= heartbeatDeadline always true) -- an entry per report and a storage
     * write per flush interval, for the life of the instance. */
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        correctIfSyncedLocked(&s_instances[i]);
    }

    uint64_t nowMs = 0;
    if (recordNowMs(&nowMs, NULL) != IOT_OK)
    {
        return 0;
    }
    const int32_t nowSec = msToSec(nowMs);
    int32_t earliest = 0;

    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        RecordState_t *inst = &s_instances[i];
        if (!inst->used || !inst->pending)
        {
            continue;
        }
        if (nowSec >= inst->windowDeadline)
        {
            closeHeldWindowLocked(inst, nowSec);
        }
        else if (earliest == 0 || inst->windowDeadline < earliest)
        {
            earliest = inst->windowDeadline;
        }
    }

    /*
     * A second pass for the two things a window deadline does not cover.
     *
     * A DIRTY block with no open window needs a flush deadline of its own. A rule with
     * IOT_CONTRACT_RECORD_EVERY_CHANGE opens no window, so without one a smoke sensor's
     * entries would sit in RAM until the block rotated, and a power cut would take every
     * one.
     *
     * An IDLE instance gives its block buffer back, because
     * CONFIG_IOT_RECORD_MAX_OPEN_BLOCKS is a concurrency bound, not a lifetime cap: without
     * the reclaim, a hub with more children than the budget would give history to the
     * first few and log "budget full" for the rest on every report, for the whole boot.
     * Quiet for a whole flush interval is the test, so an active instance keeps its
     * buffer rather than reallocating one per cycle.
     */
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        RecordState_t *inst = &s_instances[i];
        if (!inst->used)
        {
            continue;
        }
        if (inst->dirty)
        {
            const int32_t flushBy = (int32_t) (nowSec + CONFIG_IOT_RECORD_FLUSH_INTERVAL_SEC);
            if (earliest == 0 || flushBy < earliest)
            {
                earliest = flushBy;
            }
        }
        else if (!inst->pending && inst->fields != NULL
                 && (nowSec - inst->lastAppendSec) >= CONFIG_IOT_RECORD_FLUSH_INTERVAL_SEC)
        {
            releaseOpenBlock(inst);
        }
    }

    return earliest;
}

int32_t IOT_RecordServiceWindows(void)
{
    if (!recordLock())
    {
        return 0;
    }
    const int32_t earliest = serviceWindowsLocked();
    recordUnlock();
    return earliest;
}

// ---- the timer -----------------------------------------------------------------

static void recordTimerCb(void *unused);

/* Arm the one-shot for the earliest pending deadline, or arm nothing. Caller holds the
 * lock. */
static void cancelTimer(void)
{
    /* Cancelling an already-fired handle is safe; it returns NOT_FOUND. */
    if (s_recordTimer != IOT_TIMER_HANDLE_INVALID)
    {
        IOT_TimerCancel(s_recordTimer);
    }
    s_recordTimer = IOT_TIMER_HANDLE_INVALID;
    s_recordTimerDueSec = 0;
}

static void rearmTimer(int32_t earliestDeadlineSec)
{
    if (earliestDeadlineSec == 0)
    {
        cancelTimer();
        return;
    }
    uint64_t nowMs = 0;
    if (recordNowMs(&nowMs, NULL) != IOT_OK)
    {
        cancelTimer();
        return;
    }
    const int32_t nowSec = msToSec(nowMs);
    int32_t delaySec = earliestDeadlineSec - nowSec;
    if (delaySec < 1) delaySec = 1;
    const int32_t dueSec = nowSec + delaySec;

    /*
     * Never move an armed wake LATER. Every append re-arms, and a dirty block's flush
     * deadline is "now + FLUSH_INTERVAL" at each call -- so re-arming to the new value would
     * push the flush back on every event, and a device reporting more often than the
     * interval would keep its block in RAM until it filled (or forever, for a value that
     * never changes). Keeping the earlier wake makes the interval a bound on what a power
     * cut loses, counted from the first unwritten entry. A wake that turns out to have
     * nothing due is harmless: the sweep is idempotent and re-arms for what remains.
     *
     * dueSec >= nowSec rejects a handle that has already fired, and a due time left on the
     * monotonic timebase before the clock synced (it is far below an epoch now).
     */
    if (s_recordTimer != IOT_TIMER_HANDLE_INVALID && s_recordTimerDueSec != 0
        && s_recordTimerDueSec >= nowSec && s_recordTimerDueSec <= dueSec)
    {
        return;
    }

    /* Cancel before inserting. Re-arming without cancelling would leave one live timer per
     * append, each handing a sweep to core's task, back to back. */
    cancelTimer();
    s_recordTimer = IOT_TimerInsertCb(recordTimerCb, NULL, (uint32_t) delaySec * 1000u);
    if (s_recordTimer != IOT_TIMER_HANDLE_INVALID)
    {
        s_recordTimerDueSec = dueSec;
    }
}

/* A full core work queue refuses the sweep; it is handed over again this soon. */
#define RECORD_SWEEP_RETRY_MS 100u

/*
 * Runs on core's task: close the windows that are due, flush what is dirty, and re-arm the
 * one-shot for the next deadline. The flush goes through IOT_DaoStorageSetAsync(), which
 * copies before queueing and therefore does its flash write on the DAO task, outside this
 * lock.
 *
 * One-shot, re-armed here. No new task, and no periodic tick.
 */
static void recordSweepWork(void *unused)
{
    (void) unused;
    if (!recordLock())
    {
        return;
    }
    /* The one-shot that brought us here has fired: forget its due time, so the re-arm
     * below is not mistaken for an earlier wake that is still pending. */
    s_recordTimerDueSec = 0;
    const int32_t earliest = serviceWindowsLocked();
    flushAllLockedMode(true);
    rearmTimer(earliest);
    recordUnlock();
}

/*
 * Runs on the TIMER task, which has a small stack: it only hands the sweep to core's task
 * (taking the record lock and walking the instance table is not for this stack). A refused
 * hand-off is retried shortly on its own one-shot, not on s_recordTimer: an append meanwhile
 * may re-arm that for a later deadline, and the sweep it would then run is idempotent.
 */
static void recordTimerCb(void *unused)
{
    (void) unused;
    if (IOT_CoreDeferWork(recordSweepWork, NULL) == IOT_OK)
    {
        return;
    }
    if (IOT_TimerInsertCb(recordTimerCb, NULL, RECORD_SWEEP_RETRY_MS) == IOT_TIMER_HANDLE_INVALID)
    {
        /* The windows stay open until the next append re-arms the timer. */
        IOT_LOGE(TAG, "Record sweep lost: core queue full and no timer free");
    }
}

// ---- serving a GET ---------------------------------------------------------------

/*
 * Walk the chain from `checkpoint`, newest first, INCLUSIVELY.
 *
 * Inclusive because the app's first request after GET_STATE names a checkpoint it
 * already holds the header of but not the entries; an exclusive walk would hand it the
 * block before and leave a hole it has no way to ask about again.
 *
 * checkpoint 0 means the open block, served from RAM. It is also the end-of-chain
 * sentinel, so no stored block ever carries it and there is no ambiguity.
 */

/* Blocks handed back rather than sent. See the header for why this is not ServeGet.
 *
 * Scratch is static and sized to the same 4 as ServeGet's, which is also the cap the
 * GET_STATE path applies -- the two reply paths agree on how many blocks one message
 * carries, so neither can outgrow the other's assumptions. */
iot_err_t IOT_ContractRecordGetCurrentBlocks(uint16_t eid, IOT_CoreRecordStateBlock_t *outBlocks,
                                     uint8_t maxBlocks, uint8_t *outCount)
{
    static uint16_t scratch[IOT_CORE_RECORD_STATE_BLOCKS_MAX][RECORD_BLOCK_FIELDS];

    if (outBlocks == NULL || outCount == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    *outCount = 0;
    if (s_recordDisabled)
    {
        return IOT_ERR_INVALID_STATE;
    }
    if (!recordLock())
    {
        return IOT_ERR_TIMEOUT;
    }

    uint8_t n = 0;
    const uint8_t cap = (maxBlocks < IOT_CORE_RECORD_STATE_BLOCKS_MAX)
                            ? maxBlocks : IOT_CORE_RECORD_STATE_BLOCKS_MAX;
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES && n < cap; i++)
    {
        RecordState_t *inst = &s_instances[i];
        if (!inst->used || inst->eid != eid)
        {
            continue;
        }
        /* The clock may have synced since this instance last did anything. Correcting
         * here is what turns an omitted provisional block into a usable one. */
        correctIfSyncedLocked(inst);
        if (inst->provisional || inst->checkpoint == 0)
        {
            continue;
        }
        uint16_t fieldCount = 0;
        if (inst->fields != NULL)
        {
            /* The open block, from RAM: it may hold entries no flush has reached yet. */
            fieldCount = inst->fieldCount;
            if (fieldCount > 0)
            {
                memcpy(scratch[n], inst->fields, fieldCount * sizeof(uint16_t));
            }
        }
        else if (inst->ringCount > 0)
        {
            /* An instance idle past the flush interval has handed its buffer back, and
             * the next service pass runs the moment ANY attribute appends -- so reading
             * RAM alone would drop every quiet attribute from GET_STATE a minute after its
             * last value, on any device that kept reporting something else. Its newest
             * block is clean in ring[0]. IOT_ContractRecordServeGet() reads storage here
             * for the same reason. */
            IOT_DaoRecordBlock_t stored = {0};
            if (IOT_DaoRecordGet(inst->eid, inst->elemId, inst->attrId, inst->ring[0],
                                 inst->valueCount, &stored) == IOT_OK)
            {
                fieldCount = (stored.fieldCount > RECORD_BLOCK_FIELDS) ? RECORD_BLOCK_FIELDS
                                                                       : stored.fieldCount;
                if (fieldCount > 0 && stored.fields != NULL)
                {
                    memcpy(scratch[n], stored.fields, fieldCount * sizeof(uint16_t));
                }
                else
                {
                    fieldCount = 0;
                }
                SAFE_FREE(stored.fields);
            }
        }
        if (fieldCount == 0)
        {
            continue;
        }
        outBlocks[n].eid = inst->eid;
        outBlocks[n].elemId = inst->elemId;
        outBlocks[n].attrId = inst->attrId;
        outBlocks[n].valueCount = inst->valueCount;
        outBlocks[n].block.checkpoint = inst->checkpoint;
        /* prevCheckpoint 0 is the documented END-OF-CHAIN sentinel, not "unknown": the
         * app stops walking when it sees one. Reporting 0 on a device that has older
         * blocks would hide every one of them, and would contradict the history-query
         * reply, which fills this from the next ring slot for the very same block. */
        outBlocks[n].block.prevCheckpoint = 0;
        if (inst->ringCount > 1)
        {
            IOT_DaoRecordBlock_t older = {0};
            if (IOT_DaoRecordGet(inst->eid, inst->elemId, inst->attrId, inst->ring[1],
                                 inst->valueCount, &older) == IOT_OK)
            {
                outBlocks[n].block.prevCheckpoint = older.checkpoint;
                SAFE_FREE(older.fields);
            }
        }
        outBlocks[n].block.fields = scratch[n];
        outBlocks[n].block.fieldCount = fieldCount;
        n++;
    }

    recordUnlock();
    *outCount = n;
    return IOT_OK;
}

iot_err_t IOT_ContractRecordServeGet(uint16_t eid, uint16_t elemId, uint16_t attrId,
                             uint64_t checkpoint, const IOT_RequestContext_t *ctx)
{
    /* Blocks are framed and copied by core one at a time, so this is the only buffer
     * the walk needs beyond the stored fields themselves. File-static rather than stack
     * because four blocks plus headers is most of a task stack; serialised by the
     * record mutex, which every caller takes on entry. */
    static IOT_CoreRecordBlock_t blocks[4];
    static uint16_t scratch[4][RECORD_BLOCK_FIELDS];
    uint16_t blockCount = 0;

    if (ctx == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    if (s_recordDisabled)
    {
        return IOT_ERR_INVALID_STATE;
    }
    if (!recordLock())
    {
        return IOT_ERR_TIMEOUT;
    }

    const uint8_t valueCount = IOT_AttrRecordValueCount(attrId);
    RecordState_t *inst = findInstance(eid, elemId, attrId);

    /* The one path that puts a checkpoint directly into a reply.
     *
     * Correct first, or an app that asks right after the clock syncs is handed the monotonic
     * value and anchors its chart on 1970. And if the clock STILL has not synced, answer
     * as if the instance did not exist: the echoed empty block is the documented reply
     * for "nothing to show", whereas a provisional checkpoint is a timestamp the app
     * would treat as real. An unsynced device therefore answers exactly as one that has
     * recorded nothing. */
    correctIfSyncedLocked(inst);
    if (inst != NULL && inst->provisional)
    {
        inst = NULL;
    }

    /* An instance this device has never recorded -- a hub asked for a sub-device it does
     * not serve, or an attribute nobody enabled. The answer is the echoed empty block,
     * not silence and not an error: silence is indistinguishable from a device that is
     * not responding. */
    if (inst != NULL)
    {
        /* Which ring position does the requested checkpoint sit at? */
        int16_t startIdx = -1;
        if (checkpoint == 0 || checkpoint == inst->checkpoint)
        {
            startIdx = 0;
        }
        else
        {
            for (uint8_t i = 1; i < inst->ringCount; i++)
            {
                IOT_DaoRecordBlock_t probe = {0};
                if (IOT_DaoRecordGet(eid, elemId, attrId, inst->ring[i], valueCount, &probe) == IOT_OK)
                {
                    const bool match = (probe.checkpoint == checkpoint);
                    SAFE_FREE(probe.fields);
                    if (match) { startIdx = (int16_t) i; break; }
                }
            }
        }

        for (int16_t i = startIdx; i >= 0 && i < (int16_t) inst->ringCount
             && blockCount < (uint16_t) (sizeof(blocks) / sizeof(blocks[0])); i++)
        {
            uint16_t *fields = NULL;
            uint16_t fieldCount = 0;
            uint64_t cp = 0;

            if (i == 0 && inst->fields != NULL)
            {
                /* The open block, straight from RAM -- it may hold entries no flush has
                 * reached yet, and a reader that went to storage would miss them. */
                cp = inst->checkpoint;
                fields = inst->fields;
                fieldCount = inst->fieldCount;
            }
            else
            {
                /* Slot 0 comes from RAM only while the buffer is resident. An instance
                 * whose buffer was reclaimed as idle -- or never allocated at boot -- has
                 * a valid checkpoint and ring[0] but fields == NULL, so serving RAM would
                 * return the NEWEST block empty while its entries sat in storage. */
                IOT_DaoRecordBlock_t stored = {0};
                if (IOT_DaoRecordGet(eid, elemId, attrId, inst->ring[i], valueCount, &stored) != IOT_OK)
                {
                    break;
                }
                cp = stored.checkpoint;
                fieldCount = (stored.fieldCount > RECORD_BLOCK_FIELDS)
                                 ? RECORD_BLOCK_FIELDS : stored.fieldCount;
                if (fieldCount > 0 && stored.fields != NULL)
                {
                    memcpy(scratch[blockCount], stored.fields, fieldCount * sizeof(uint16_t));
                }
                SAFE_FREE(stored.fields);
                fields = scratch[blockCount];
            }

            if (i == 0 && fields != NULL && fieldCount > 0)
            {
                memcpy(scratch[blockCount], fields, fieldCount * sizeof(uint16_t));
                fields = scratch[blockCount];
            }

            blocks[blockCount].checkpoint = cp;
            blocks[blockCount].fields = (fieldCount > 0) ? scratch[blockCount] : NULL;
            blocks[blockCount].fieldCount = fieldCount;
            /* prev is the NEXT older block's checkpoint, and 0 when there is none --
             * which is how the app learns to stop walking. */
            blocks[blockCount].prevCheckpoint = 0;
            if ((uint8_t) (i + 1) < inst->ringCount)
            {
                IOT_DaoRecordBlock_t older = {0};
                if (IOT_DaoRecordGet(eid, elemId, attrId, inst->ring[i + 1], valueCount, &older) == IOT_OK)
                {
                    blocks[blockCount].prevCheckpoint = older.checkpoint;
                    SAFE_FREE(older.fields);
                }
            }
            blockCount++;
        }
    }

    if (blockCount == 0)
    {
        /* The echoed empty block. Never 0 blocks and never a checkpoint of 0 of our own
         * invention: 0 collides with both the end-of-chain sentinel and "give me the
         * current block", and would send a conforming app back to the head of the chain. */
        blocks[0].checkpoint = checkpoint;
        blocks[0].prevCheckpoint = 0;
        blocks[0].fields = NULL;
        blocks[0].fieldCount = 0;
        blockCount = 1;
    }

    IOT_CoreRecordResponse_t response = {
        .eid = eid,
        .elemId = elemId,
        .attrId = attrId,
        .valueCount = valueCount,
        .blocks = blocks,
        .blockCount = blockCount,
        .ctx = *ctx,
    };

    /*
     * Unlock BEFORE sending.
     *
     * The send can block on a congested connection. Holding the table across it would
     * make the event task's IOT_RecordAppend() wait at recordLock(), time out after
     * RECORD_LOCK_TIMEOUT_MS and silently drop the reading: a sensor value lost because
     * somebody opened a chart. The window sweep would be held off the same way.
     *
     * Safe to unlock here because `blocks` and `scratch` are file-static and this
     * function is the only writer of them -- a second concurrent GET would be serialised
     * by the lock it takes on entry, before it reaches them.
     *
     * Still synchronous with respect to the EVENT CALLBACK, which is what the contract
     * actually requires: ctx.senderId points into the message reader's buffer and is
     * freed when the handler returns.
     */
    recordUnlock();
    return IOT_CoreRequestRecordResponse(&response);
}

// ---- pushing closed reels --------------------------------------------------------
//
// A reel is pushed once it has CLOSED (ring index >= 1) and its slot on flash holds the
// sequence the ring expects. The open reel, ring[0], is never pushed, and a provisional
// instance never is either: it has written nothing.

/* Separate from ServeGet's buffers: a GET may be served while a push is in flight, since
 * the push sends with the lock released. Only the core work task pushes, so nothing else
 * writes these. */
static IOT_CoreRecordBlock_t s_pushBlocks[IOT_CORE_RECORD_PUSH_BLOCKS_MAX];
static uint16_t s_pushScratch[IOT_CORE_RECORD_PUSH_BLOCKS_MAX][RECORD_BLOCK_FIELDS];

/* Ring index of the OLDEST unsent closed reel, or 0 when there is none -- 0 is the open
 * reel, which is never pushed, so it can stand for "nothing". Caller holds the lock.
 *
 * Unsent reels are always a prefix 1..k of the ring, since the ring is newest first and
 * the bookmark is a sequence, so the first unsent index from the old end is the oldest. */
static uint8_t oldestUnsentLocked(RecordState_t *inst)
{
    correctIfSyncedLocked(inst);
    if (!inst->used || inst->provisional || inst->checkpoint == 0 || inst->ringCount < 2)
    {
        return 0;
    }
    for (uint8_t i = (uint8_t) (inst->ringCount - 1); i > 0; i--)
    {
        if (!inst->havePushed || recordSeqNewer(inst->ringSeq[i], inst->pushedSeq))
        {
            return i;
        }
    }
    return 0;
}

/* Read up to IOT_CORE_RECORD_PUSH_BLOCKS_MAX reels, from `oldest` toward the open one,
 * into the push buffers. Caller holds the lock. Returns how many were taken.
 *
 * Stops at the first reel that cannot be read or whose stored sequence is not the one the
 * ring expects: rotateBlock() flushes with async = true, so "rotated" is not "written",
 * and a reel pushed past would never be sent once the bookmark moved beyond it. */
static uint16_t collectReelsLocked(const RecordState_t *inst, uint8_t oldest, uint16_t *outNewestSeq)
{
    uint16_t n = 0;

    /* Same rule as ServeGet: prev is the next older reel's checkpoint, 0 when none. */
    uint64_t prev = 0;
    if ((uint8_t) (oldest + 1) < inst->ringCount)
    {
        IOT_DaoRecordBlock_t older = {0};
        if (IOT_DaoRecordGet(inst->eid, inst->elemId, inst->attrId, inst->ring[oldest + 1],
                             inst->valueCount, &older) == IOT_OK)
        {
            prev = older.checkpoint;
        }
        SAFE_FREE(older.fields);
    }

    for (uint8_t i = oldest; i > 0 && n < IOT_CORE_RECORD_PUSH_BLOCKS_MAX; i--)
    {
        IOT_DaoRecordBlock_t stored = {0};
        if (IOT_DaoRecordGet(inst->eid, inst->elemId, inst->attrId, inst->ring[i], inst->valueCount,
                             &stored) != IOT_OK
            || stored.sequence != inst->ringSeq[i])
        {
            SAFE_FREE(stored.fields);
            break;
        }
        uint16_t fieldCount = (stored.fieldCount > RECORD_BLOCK_FIELDS) ? RECORD_BLOCK_FIELDS : stored.fieldCount;
        if (fieldCount > 0 && stored.fields != NULL)
        {
            memcpy(s_pushScratch[n], stored.fields, fieldCount * sizeof(uint16_t));
        }
        else
        {
            fieldCount = 0;
        }
        SAFE_FREE(stored.fields);

        s_pushBlocks[n].checkpoint = stored.checkpoint;
        s_pushBlocks[n].prevCheckpoint = prev;
        s_pushBlocks[n].fields = (fieldCount > 0) ? s_pushScratch[n] : NULL;
        s_pushBlocks[n].fieldCount = fieldCount;
        prev = stored.checkpoint;
        *outNewestSeq = stored.sequence;
        n++;
    }
    return n;
}

/* True when any instance has a closed reel not yet pushed. Caller holds the lock. */
static bool anyUnsentLocked(void)
{
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        if (oldestUnsentLocked(&s_instances[i]) != 0)
        {
            return true;
        }
    }
    return false;
}

/*
 * Send at most ONE message of closed reels, then advance that series' bookmark.
 *
 * Picks the first instance, in table order, that has a collectable unsent reel, and sends
 * up to four of its reels oldest first. An instance whose oldest unsent reel cannot be
 * collected yet is passed over rather than allowed to hold up every series behind it; it
 * clears itself when the write lands, or when the reel is evicted.
 *
 * The send is made with the record lock RELEASED, for the reason IOT_ContractRecordServeGet()
 * gives: a congested publish must not stall appends. The bookmark is advanced afterwards,
 * under the lock again, to the newest SEQUENCE sent -- never to a ring index, which a
 * rotation during the send would have shifted.
 *
 * @param outSendErr May be NULL. Receives IOT_OK when a message was handed to core; the
 *                   send error when one was attempted and refused (nothing advanced);
 *                   IOT_ERR_NOT_FOUND when there was nothing collectable to send;
 *                   IOT_ERR_NOT_SUPPORTED when push is compiled out;
 *                   IOT_ERR_INVALID_STATE when Record is disabled; IOT_ERR_TIMEOUT when the
 *                   table could not be locked.
 * @return true when unsent closed reels remain after this call, whether or not it sent.
 *         A caller pacing messages continues only on true AND IOT_OK; any other error
 *         means wait for the next trigger.
 */
static bool recordPushOnce(iot_err_t *outSendErr)
{
    iot_err_t sendErr = IOT_ERR_NOT_FOUND;
    bool remains = false;

    if (!IOT_RECORD_PUSH || s_appendOverride != NULL)
    {
        sendErr = IOT_ERR_NOT_SUPPORTED;
    }
    else if (s_recordDisabled)
    {
        sendErr = IOT_ERR_INVALID_STATE;
    }
    else if (!recordLock())
    {
        sendErr = IOT_ERR_TIMEOUT;
        remains = true;   /* unknown; let the caller's next trigger find out */
    }
    else
    {
        IOT_CoreRecordPush_t push = {0};
        uint16_t newestSeq = 0;
        for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES && push.blockCount == 0; i++)
        {
            RecordState_t *inst = &s_instances[i];
            const uint8_t oldest = oldestUnsentLocked(inst);
            if (oldest == 0)
            {
                continue;
            }
            push.blockCount = collectReelsLocked(inst, oldest, &newestSeq);
            if (push.blockCount == 0)
            {
                remains = true;   /* unsent, just not collectable this time */
                continue;
            }
            push.eid = inst->eid;
            push.elemId = inst->elemId;
            push.attrId = inst->attrId;
            push.valueCount = inst->valueCount;
            push.blocks = s_pushBlocks;
        }
        recordUnlock();

        if (push.blockCount > 0)
        {
            sendErr = IOT_CoreReportRecordBlocks(&push);
            if (sendErr != IOT_OK)
            {
                /* Offline is the common case and is retried on the next trigger -- not a
                 * WARN per attempt. */
                IOT_LOGD(TAG, "Record push for EID %04x elem %04x attr %04x not sent (%s)", push.eid,
                         push.elemId, push.attrId, iot_err_to_name(sendErr));
                remains = true;
            }
            else if (!recordLock())
            {
                /* Sent but not bookmarked: the worst case is these reels go again. */
                IOT_LOGW(TAG, "Record push for EID %04x elem %04x attr %04x sent but the bookmark "
                              "could not be advanced; it will be resent",
                         push.eid, push.elemId, push.attrId);
                remains = true;
            }
            else
            {
                /* Looked up again: the table may have changed while it was unlocked. */
                RecordState_t *inst = findInstance(push.eid, push.elemId, push.attrId);
                if (inst != NULL && (!inst->havePushed || recordSeqNewer(newestSeq, inst->pushedSeq)))
                {
                    inst->pushedSeq = newestSeq;
                    inst->havePushed = true;
                    /* A failed persist costs one resend after a reboot, and a resent reel
                     * is harmless -- not worth failing a push that succeeded. */
                    const iot_err_t perr =
                        IOT_DaoRecordPutPushed(push.eid, push.elemId, push.attrId, newestSeq, true);
                    if (perr != IOT_OK)
                    {
                        IOT_LOGW(TAG, "Record push bookmark for EID %04x elem %04x attr %04x not "
                                      "persisted (%s)",
                                 push.eid, push.elemId, push.attrId, iot_err_to_name(perr));
                    }
                }
                remains = anyUnsentLocked();
                recordUnlock();
            }
            /* Sent is sent, bookmarked or not. Outside the lock: a subscriber never runs under it. */
            if (sendErr == IOT_OK)
            {
                IOT_ContractEventRecordPushed(push.eid, push.elemId, push.attrId, push.blockCount);
            }
        }
    }

    if (outSendErr != NULL)
    {
        *outSendErr = sendErr;
    }
    return remains;
}

// ---- push pacing -------------------------------------------------------------------

#define RECORD_PUSH_GAP_MS   500
#define RECORD_PUSH_RETRY_MS (5 * 60 * 1000)

/* DEV_EVENT_CLOUD_CONNECTED means the session is subscribed, so the push it kicks is normally
 * accepted the first time. It can still be refused with IOT_ERR_INVALID_STATE: the session can
 * drop again before this push runs, and core refuses a history send while the clock is not synced
 * (it holds the cloud connect for the clock only up to a cap). Retry that soon, a few times, before
 * settling for the long retry (a genuinely offline device). */
#define RECORD_PUSH_NOT_READY_MS    5000
#define RECORD_PUSH_NOT_READY_TRIES 6

/* Consecutive not-ready refusals. Touched only by recordPushWork (core task). */
static uint8_t s_pushNotReady;

/* The one push timer. Written from three tasks: recordPushWork() on the core task,
 * recordPushTimerCb() on the timer task and IOT_RecordPushKick() on whoever kicked, the
 * last two only when the defer queue is full. It is deliberately NOT under the record lock
 * (Kick runs while that lock is held). A race can at worst leak one one-shot, which fires
 * one extra deferral of idempotent work, after which the chains merge again. */
static IOT_TimerHandle_t s_pushTimer;

/* True from a successful defer until recordPushWork starts. Coalesces kicks: N series
 * rotating together would otherwise queue N items on the shared core work queue and
 * send N messages back to back, skipping RECORD_PUSH_GAP_MS. */
static volatile bool s_pushQueued;

static void recordPushTimerCb(void *unused);
static void recordPushWork(void *unused);

/* Queue recordPushWork unless one is already queued. Posts only; takes no lock.
 *
 * The flag is set BEFORE the post: IOT_CoreDeferWork() has already queued the item when it
 * returns, so the core task may run recordPushWork (which clears the flag) before this
 * task resumes. Set -> post -> handler clears means a late set can never overwrite a clear.
 * A refused post queued nothing, so the flag goes back to false.
 *
 * Coalescing is best effort: two kickers that read false together may both post, which is
 * harmless because the work is idempotent. A kicker skipped while another kicker's post is
 * failing is recovered by that failing caller's retry timer. */
static iot_err_t deferPushWork(void)
{
    if (s_pushQueued)
    {
        return IOT_OK;
    }
    s_pushQueued = true;
    const iot_err_t err = IOT_CoreDeferWork(recordPushWork, NULL);
    if (err != IOT_OK)
    {
        s_pushQueued = false;
    }
    return err;
}

static void armPushTimer(uint32_t delayMs)
{
    if (s_pushTimer != IOT_TIMER_HANDLE_INVALID)
    {
        IOT_TimerCancel(s_pushTimer);
    }
    s_pushTimer = IOT_TimerInsertCb(recordPushTimerCb, NULL, delayMs);
}

static void cancelPushTimer(void)
{
    if (s_pushTimer != IOT_TIMER_HANDLE_INVALID)
    {
        IOT_TimerCancel(s_pushTimer);
        s_pushTimer = IOT_TIMER_HANDLE_INVALID;
    }
}

/* Runs on the TIMER task: no lock, no DAO, no send. Hand the work to the core task. */
static void recordPushTimerCb(void *unused)
{
    (void) unused;
    if (deferPushWork() == IOT_ERR_NO_MEM)
    {
        armPushTimer(RECORD_PUSH_RETRY_MS);
    }
}

/* Runs on the core work task. Sends at most one message, then decides when to come back. */
static void recordPushWork(void *unused)
{
    (void) unused;
    s_pushQueued = false;
    iot_err_t err = IOT_OK;
    const bool remains = recordPushOnce(&err);
    if (!remains)
    {
        cancelPushTimer();
    }
    else if (err == IOT_ERR_INVALID_STATE && s_pushNotReady < RECORD_PUSH_NOT_READY_TRIES)
    {
        s_pushNotReady++;
        armPushTimer(RECORD_PUSH_NOT_READY_MS);
        return;
    }
    else if (err == IOT_OK)
    {
        armPushTimer(RECORD_PUSH_GAP_MS);
    }
    else
    {
        armPushTimer(RECORD_PUSH_RETRY_MS);
    }
    if (err != IOT_ERR_INVALID_STATE)
    {
        s_pushNotReady = 0;
    }
}

void IOT_RecordPushKick(void)
{
    if (!IOT_RECORD_PUSH || s_appendOverride != NULL)
    {
        return;
    }
    /* Every external trigger (rotation, reconnect, boot) opens a fresh short-backoff window;
     * otherwise an outage would use up the tries and the reconnect would get the long retry.
     * Timer-driven re-runs do not reset it. A plain byte store, racing only with the core task. */
    s_pushNotReady = 0;
    if (deferPushWork() == IOT_ERR_NO_MEM)
    {
        armPushTimer(RECORD_PUSH_RETRY_MS);
    }
}

// ---- boot ------------------------------------------------------------------------

/* Rebuild one device's instances from storage. Caller holds the lock. */
static void rebuildDevice(uint16_t eid)
{
    /*
     * Eight attributes' worth, which is what a boot scan is budgeted for. A truncated scan
     * is worse than a slow one: ringCount would under-count, claimRingSlot() would hand
     * out a slot index that already holds an unlisted blob, and the next rotation would
     * overwrite a block nobody knew was there.
     *
     * Heap rather than stack -- the list is too big for an arbitrary task's stack, and
     * this runs on whatever task starts the contract.
     */
    const uint16_t slotCap = (uint16_t) (CONFIG_IOT_RECORD_BLOCKS_PER_ATTR * 8);
    IOT_DaoRecordSlotInfo_t *slots =
        (IOT_DaoRecordSlotInfo_t *) Mem_SafeMalloc(slotCap * sizeof(IOT_DaoRecordSlotInfo_t),
                                                   TAG, "recordScan");
    if (slots == NULL)
    {
        IOT_LOGE(TAG, "Record: cannot scan EID %04x, out of memory", eid);
        return;
    }

    uint16_t count = 0;
    const iot_err_t scanErr = IOT_DaoRecordScanDevice(eid, slots, slotCap, &count);
    /* One line per device at boot: whether history came back is otherwise invisible until
     * an app finds its chart restarted. */
    IOT_LOGI(TAG, "Record: EID %04x boot scan -> %d, %u stored block(s)", eid, (int) scanErr,
             (unsigned) count);
    if (scanErr != IOT_OK || count == 0)
    {
        SAFE_FREE(slots);
        return;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        RecordState_t *inst = admitInstance(eid, slots[i].elemId, slots[i].attrId,
                                            IOT_AttrRecordValueCount(slots[i].attrId));
        if (inst == NULL)
        {
            continue;
        }
        /* Insert into the ring in DESCENDING sequence order. Never by checkpoint: a
         * backward clock step legitimately writes a newer block with an earlier
         * checkpoint, and sorting by it would call the newest block oldest, set its prev
         * to 0, and reopen a block in the future of the corrected clock -- a negative
         * delta on every append, permanently. */
        if (inst->ringCount >= CONFIG_IOT_RECORD_BLOCKS_PER_ATTR)
        {
            continue;
        }
        uint8_t pos = 0;
        while (pos < inst->ringCount && !recordSeqNewer(slots[i].sequence, inst->ringSeq[pos]))
        {
            pos++;
        }
        for (uint8_t j = inst->ringCount; j > pos; j--)
        {
            inst->ring[j] = inst->ring[j - 1];
            inst->ringSeq[j] = inst->ringSeq[j - 1];
        }
        inst->ring[pos] = slots[i].slot;
        inst->ringSeq[pos] = slots[i].sequence;
        inst->ringCount++;

        if (pos == 0)
        {
            inst->checkpoint = slots[i].checkpoint;
            inst->sequence = slots[i].sequence;
        }
    }

    /*
     * Reopen the newest block rather than starting a fresh one.
     *
     * NOT optional. Opening a fresh block each boot would consume a ring slot per reboot,
     * so a run of power cycles -- a flaky supply, an OTA retry, a boot-recovery loop --
     * would evict the entire chain, each surviving block holding one entry. The history
     * would be gone while the bound held perfectly.
     */
    for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
    {
        RecordState_t *inst = &s_instances[i];
        if (!inst->used || inst->eid != eid || inst->ringCount == 0)
        {
            continue;
        }
        if (IOT_RECORD_PUSH)
        {
            /* ANY failure reads as "no bookmark": not found is an upgrade from a build
             * without push, and a corrupt value has already been deleted by the DAO. Both
             * mean every closed reel in the ring is sent again, which is harmless. Guessing
             * a bookmark instead could skip reels the cloud never received. */
            uint16_t pushed = 0;
            inst->havePushed = (IOT_DaoRecordGetPushed(eid, inst->elemId, inst->attrId, &pushed) == IOT_OK);
            inst->pushedSeq = inst->havePushed ? pushed : 0;
        }
        IOT_DaoRecordBlock_t newest = {0};
        if (IOT_DaoRecordGet(eid, inst->elemId, inst->attrId, inst->ring[0],
                             inst->valueCount, &newest) != IOT_OK)
        {
            continue;
        }
        const uint8_t fpe = fieldsPerEntry(inst);
        if (newest.fieldCount >= fpe && newest.fields != NULL)
        {
            const uint16_t n = (newest.fieldCount > RECORD_BLOCK_FIELDS)
                                   ? RECORD_BLOCK_FIELDS : newest.fieldCount;
            /* Restore lastValue from the block's LAST entry, so the first report after a
             * reboot is compared against what was really recorded rather than treated as
             * a change. */
            const uint16_t lastStart = (uint16_t) (n - fpe);
            for (uint8_t v = 0; v < inst->valueCount; v++)
            {
                inst->lastValue[v] = newest.fields[lastStart + v];
            }
            inst->haveLastValue = true;
            /* Reopen into RAM only while the budget has room. Beyond that the instance
             * reloads its block from storage on its next value (ensureOpenBlock()), so
             * nothing depends on the buffer being resident -- and taking the whole budget
             * here would leave none for the first report after the reboot. */
            if (s_openBlocks < CONFIG_IOT_RECORD_MAX_OPEN_BLOCKS && ensureOpenBlock(inst))
            {
                /* Stamp it, or the first service pass sees lastAppendSec == 0, calls the
                 * instance idle, and hands the buffer straight back. */
                uint64_t nowMs = 0;
                if (recordNowMs(&nowMs, NULL) == IOT_OK)
                {
                    inst->lastAppendSec = msToSec(nowMs);
                }
            }
        }
        SAFE_FREE(newest.fields);
    }

    SAFE_FREE(slots);
}

static iot_err_t recordAppendV2(const IOT_ContractRecordEvent_t *event, const IOT_ContractRecordRule_t *rule)
{
    uint16_t values[2] = {0};

    /* One incoming attribute, two records. History for the combined attribute is kept
     * under IOT_ATTR_TEMP_EVT and IOT_ATTR_HUMID_EVT, never under the combined id, and the
     * app asks for each separately -- two blocks, two chains, two rings, two instances.
     *
     * Recording it as one attribute with two fields would not work: the app sizes entries
     * from attrId and asks for the separate ids. */
    if (event->attrId == IOT_ATTR_TEMP_HUMID_EVT)
    {
        if (readValues(event, 2, values) < 2)
        {
            IOT_LOGW(TAG, "TEMP_HUMID_EVT for EID %04x elem %04x carries %u bytes; need 4",
                     event->eid, event->elemId, (unsigned) event->valueLen);
            return IOT_ERR_INVALID_SIZE;
        }
        /* An override addresses ONE HALF of the pair. resolveCapture() matched on the
         * incoming combined attrId, under which no override for either half is stored, so
         * look each half up here -- otherwise an app-writer who disabled humidity would
         * still get humidity. */
        const RecordElementOverride_t *tempOv =
            findOverride(event->eid, event->elemId, IOT_ATTR_TEMP_EVT);
        const RecordElementOverride_t *humidOv =
            findOverride(event->eid, event->elemId, IOT_ATTR_HUMID_EVT);
        const bool wantTemp = (tempOv == NULL) || tempOv->enabled;
        const bool wantHumid = (humidOv == NULL) || humidOv->enabled;

        /* Admitted together or not at all: admitted one at a time, a table with one free
         * slot would take temperature and drop humidity, leaving half a reading. Half a
         * temp/humid pair is worse than none -- the chart shows a sensor that measures
         * temperature and has never once reported humidity. */
        uint16_t pair[2] = {IOT_ATTR_TEMP_EVT, IOT_ATTR_HUMID_EVT};
        uint8_t pairCount = 0;
        if (wantTemp) pair[pairCount++] = IOT_ATTR_TEMP_EVT;
        if (wantHumid) pair[pairCount++] = IOT_ATTR_HUMID_EVT;
        if (pairCount == 0)
        {
            return IOT_OK;
        }
        if (freeInstanceSlotsFor(event->eid, event->elemId, pair, pairCount) == 0xFF)
        {
            IOT_LOGW(TAG, "Record instance table cannot take both halves of TEMP_HUMID "
                          "for EID %04x elem %04x; recording neither",
                     event->eid, event->elemId);
            return IOT_ERR_NO_MEM;
        }

        const uint16_t temp = values[0];
        const uint16_t humid = values[1];
        iot_err_t e1 = IOT_OK, e2 = IOT_OK;
        if (wantTemp)  e1 = recordOneAttr(event, rule, IOT_ATTR_TEMP_EVT, &temp, 1);
        if (wantHumid) e2 = recordOneAttr(event, rule, IOT_ATTR_HUMID_EVT, &humid, 1);
        return (e1 != IOT_OK) ? e1 : e2;
    }

    const uint8_t valueCount = IOT_AttrRecordValueCount(event->attrId);

    /*
     * Warn when the FORMAT cannot carry the attribute -- not when
     * IOT_AttrRecordValueCount() deliberately narrowed it.
     *
     * The test is the DECLARED field count against the format's ceiling of two, not the
     * declared size against what we chose to keep. IOT_ATTR_OPEN_CLOSE_CTL declares two
     * fields and records one, by hand-written exception: its position percentage is not
     * recorded. Comparing against the kept width would make that designed case warn on
     * EVERY report, on any hub with a curtain. A warning that
     * cries wolf on the one deliberate case trains people to
     * ignore the one that catches a genuinely unhandled wide attribute.
     */
    const uint16_t declaredFields = (uint16_t) (IOT_AttrGetSize(event->attrId) / 2);
    if (declaredFields > 2)
    {
        IOT_LOGW(TAG, "Attr %04x declares %u 16-bit fields but a record entry carries at "
                      "most 2; recording the leading %u only",
                 event->attrId, (unsigned) declaredFields, (unsigned) valueCount);
    }

    if (readValues(event, valueCount, values) < valueCount)
    {
        IOT_LOGW(TAG, "Attr %04x needs %u 16-bit fields but the value is %u bytes",
                 event->attrId, (unsigned) valueCount, (unsigned) event->valueLen);
        return IOT_ERR_INVALID_SIZE;
    }
    return recordOneAttr(event, rule, event->attrId, values, valueCount);
}

iot_err_t IOT_RecordAppend(const IOT_ContractRecordEvent_t *event)
{
    const IOT_ContractRecordRule_t *rule = NULL;
    if (!resolveCapture(event, &rule))
    {
        return IOT_OK;
    }

    /* The override sees the event after rule resolution and nothing else: no V2 lock, no
     * instance table, no timer. */
    if (s_appendOverride != NULL)
    {
        return s_appendOverride(event, rule);
    }
    if (s_recordDisabled)
    {
        return IOT_ERR_INVALID_STATE;
    }

    if (!recordLock())
    {
        return IOT_ERR_TIMEOUT;
    }
    s_closedCount = 0;
    const iot_err_t err = recordAppendV2(event, rule);
    /* An append can open a window, so the one-shot has to be reconsidered here -- there
     * is no periodic tick that would notice later. */
    rearmTimer(serviceWindowsLocked());
    RecordClosed_t closed[sizeof(s_closed) / sizeof(s_closed[0])];
    const uint8_t closedCount = s_closedCount;
    memcpy(closed, s_closed, sizeof(closed));
    s_closedCount = 0;
    recordUnlock();

    for (uint8_t i = 0; i < closedCount; i++)
    {
        IOT_ContractEventRecordBlockClosed(closed[i].eid, closed[i].elemId, closed[i].attrId);
    }
    return err;
}

// ============================================================================
// Lifecycle
// ============================================================================
//
// These live at the bottom because they drive the V2 machinery above: the boot rebuild,
// the mutex and the one-shot timer are all defined in the Record V2 section, and a
// forward declaration of each would be a second place to keep in step.

iot_err_t IOT_RecordInit(void)
{
    /* A mis-set override is otherwise invisible, so say which path is live. WARN rather
     * than INFO because several targets run at WARN by default. */
    if (s_appendOverride != NULL)
    {
        IOT_LOGW(TAG, "Record override active");
        return IOT_OK;
    }
    IOT_LOGW(TAG, "Record V2 active");

    if (s_recordMutex == NULL)
    {
        s_recordMutex = IOT_OsalMutexCreate();
        if (s_recordMutex == NULL)
        {
            /* Without it, a GET can observe a half-rotated block and ship the new
             * checkpoint with the old block's entries. Refuse rather than record
             * unlocked -- the failure it would produce is a chart offset by a whole
             * block lifetime, with nothing wrong anywhere in the logs.
             *
             * The flag matters because the caller may ignore IOT_RecordInit()'s return, so
             * an error alone would change nothing: recording would carry on with three
             * tasks racing the instance table AND the static reply buffers. */
            IOT_LOGE(TAG, "Record: no mutex available; recording is DISABLED for this boot");
            s_recordDisabled = true;
            return IOT_ERR_NO_MEM;
        }
        s_recordDisabled = false;
    }

    if (!recordLock())
    {
        return IOT_ERR_TIMEOUT;
    }

    uint16_t *eids = NULL;
    uint16_t eidCount = 0;
    if (IOT_DaoDeviceGetAllEids(&eids, &eidCount) == IOT_OK && eids != NULL)
    {
        for (uint16_t i = 0; i < eidCount; i++)
        {
            rebuildDevice(eids[i]);
            /* V1 stored one "L%04x" namespace per device and nothing else on a V2 build
             * deletes it -- not device removal, not factory reset. Every V1->V2 upgrade
             * would otherwise strand one per child forever, invisible to every API. */
            IOT_DaoRecordReapV1(eids[i]);
        }
        SAFE_FREE(eids);
    }

    const int32_t earliest = serviceWindowsLocked();
    rearmTimer(earliest);
    recordUnlock();
    /* No push kick here: the contract runs this before core creates its work queue, so a
     * kick could only fail. A boot backlog drains on the cloud-connect kick. */
    return IOT_OK;
}

iot_err_t IOT_ContractRecordClearRules(void)
{
    /* Rules and instance state are torn down together, so re-registering opens fresh
     * chains rather than resuming blocks whose policy has changed -- a block half-filled
     * under a 60 s rate limit and continued under EVERY_CHANGE would carry two different
     * sampling densities under one checkpoint, with nothing in it to say where. */
    if (recordLock())
    {
        flushAllLockedMode(true);
        for (uint8_t i = 0; i < CONFIG_IOT_RECORD_MAX_INSTANCES; i++)
        {
            releaseOpenBlock(&s_instances[i]);
        }
        memset(s_instances, 0, sizeof(s_instances));
        s_openBlocks = 0;
        recordUnlock();
    }

    memset(s_rules, 0, sizeof(s_rules));
    memset(s_overrides, 0, sizeof(s_overrides));
    return IOT_OK;
}

#include "services/IOT_ServiceSmart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "IOT_Log.h"
#include "IOT_Common.h"
#include "IOT_Core.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_TimerMgmt.h"
#include "IOT_CoreReporter.h"
#include "dao/IOT_DaoSmartMgmt.h"
#include "dao/IOT_DaoSmartTriggerMgmt.h"
#include "dao/IOT_DaoSmartScheduleMgmt.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "IOT_ContractAttrsInternal.h"
#include "services/IOT_SmartCondGroup.h"
#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"
#include "IOT_Osal.h"
#include "IOT_ContractEvents.h"
#include "IOT_ContractSenderCheck.h"

static const char *TAG = "SDK_SMART";

#define SECONDS_PER_DAY         86400
#define SECONDS_PER_MINUTE      60
#define MINUTES_PER_HOUR        60
#define MINUTES_PER_DAY         1440

// ---- Local trigger dedup: run an incoming ACTIVE_BY_TRIGGER at most once ----
// Guards both the cloud/own echo AND a duplicate from a second cond-group device that
// fired in the cond-group convergence race. Window must exceed the duplicate spread (a fast
// location-wide broadcast) yet stay under the shortest LEGITIMATE re-fire
// interval (a fall-below-K then re-complete), since a re-fire is content-identical to a
// duplicate and can only be told apart by time. 2s covers echo + convergence with margin
// while only suppressing genuine re-fires that recur within 2s.
#define LOCAL_TRIG_DEDUP_SIZE  8
#define LOCAL_TRIG_DEDUP_MS    2000

static struct { uint16_t smid; uint32_t tick; } s_localTrigs[LOCAL_TRIG_DEDUP_SIZE];
static uint8_t s_localTrigIdx;

void SmartService_MarkLocalTrigger(uint16_t smid)
{
    s_localTrigs[s_localTrigIdx].smid = smid;
    s_localTrigs[s_localTrigIdx].tick = IOT_OsalGetTickCount();
    s_localTrigIdx = (s_localTrigIdx + 1) % LOCAL_TRIG_DEDUP_SIZE;
}

iot_err_t SmartService_ExecuteTriggerDeduped(uint16_t smid, const uint8_t *triggerValue,
                                             uint16_t triggerValueSize)
{
    // Consumer-side dedup: run an incoming ACTIVE_BY_TRIGGER at most once per dedup
    // window. Covers BOTH our own broadcast echo AND a duplicate from a second condition
    // device that fired in the cond-group convergence race — so a non-idempotent action
    // never runs twice. The window/ring is the same one used for echo suppression.
    if (SmartService_IsRecentLocalTrigger(smid))
    {
        IOT_LOGI(TAG, "ACTIVE_BY_TRIGGER: SMID=%04x recently executed, skipping duplicate", smid);
        return IOT_OK;
    }

    iot_err_t err = SmartService_ExecuteCommandsEx(smid, triggerValue, triggerValueSize);
    if (err == IOT_ERR_NOT_FOUND)
        return IOT_OK; // no bind cmds on this device — cross-device broadcast, normal
    if (err == IOT_OK)
        SmartService_MarkLocalTrigger(smid); // stake the claim so a duplicate is skipped
    return err;
}

// ---- Schedule dedup: run one due minute at most once, whichever path delivers it ----
// A scheduled automation reaches this device twice: the cloud pushes
// SMART ACTIVE_BY_SCHEDULE carrying the due minute, and the offline tick below
// independently matches the same minute against the stored schedule. Both then run the
// bind commands, which are not necessarily idempotent (a toggle, a countdown, a
// notification). The trigger dedup above does NOT cover this — it keys on SMID alone and
// its 2 s window is far shorter than the up-to-60 s gap between the push and the tick that
// observes the same minute.
//
// The key is (SMID, minute-of-week): a slot recurs weekly, so the window must comfortably
// exceed the tick period yet stay far below a week. 3 minutes covers the push/tick gap
// plus timer jitter with margin.
//
// The window is measured in TICKS, not by comparing minute-of-week values. Both delivery
// paths consult an entry with the SAME minute-of-week it was stored under, so a
// minute-of-week comparison cannot tell this week's claim from the identical one a week
// ago — and the weekly re-fire is exactly what must NOT be suppressed.
//
// 16 entries, not 8: ACTIVE_BY_SCHEDULE arrives as a location-wide broadcast and a location can
// easily have a dozen automations due in the same minute. Eviction fails OPEN — the
// automation simply actuates a second time — so the ring has to hold a whole burst.
// Eviction is oldest-first: the index only ever advances, so slot age is monotonic and the
// slot the index points at is the oldest claim in the ring.
//
// Entries are read and written by the cloud push and by the offline tick. Both run on core's
// task today (the tick's timer only hands its body over), but a 4-field entry is not written
// atomically, so the scan and the write still sit inside a critical section: a caller on any
// other task stays safe.
#define SCHED_DEDUP_SIZE 16
#define SCHED_DEDUP_MS   180000

static struct { uint16_t smid; uint16_t mow; uint32_t tick; bool used; } s_schedRuns[SCHED_DEDUP_SIZE];
static uint8_t s_schedRunIdx;

/* True if this (smid, minute-of-week) was already run inside the window, i.e. the other
 * delivery path got there first. */
static bool isScheduleRunClaimed(uint16_t smid, uint16_t minuteOfWeek)
{
    uint32_t now = IOT_OsalGetTickCount();
    uint32_t window = IOT_OsalMsToTicks(SCHED_DEDUP_MS);
    bool claimed = false;

    IOT_OsalEnterCritical();
    for (int i = 0; i < SCHED_DEDUP_SIZE; i++)
    {
        if (s_schedRuns[i].used && s_schedRuns[i].smid == smid &&
            s_schedRuns[i].mow == minuteOfWeek && (now - s_schedRuns[i].tick) <= window)
        {
            claimed = true;
            break;
        }
    }
    IOT_OsalExitCritical();

    return claimed;
}

/* Record that this (smid, minute-of-week) has RUN. Called only after a successful run:
 * see SmartService_ExecuteSchedule. */
static void claimScheduleRun(uint16_t smid, uint16_t minuteOfWeek)
{
    uint32_t now = IOT_OsalGetTickCount();

    IOT_OsalEnterCritical();
    s_schedRuns[s_schedRunIdx].smid = smid;
    s_schedRuns[s_schedRunIdx].mow = minuteOfWeek;
    s_schedRuns[s_schedRunIdx].tick = now;
    s_schedRuns[s_schedRunIdx].used = true;
    s_schedRunIdx = (uint8_t)((s_schedRunIdx + 1) % SCHED_DEDUP_SIZE);
    IOT_OsalExitCritical();
}

iot_err_t SmartService_ExecuteSchedule(uint16_t smid, uint16_t minuteOfWeek)
{
    if (isScheduleRunClaimed(smid, minuteOfWeek))
    {
        IOT_LOGI(TAG, "Schedule SMID=%04x MOW=%u already ran, skipping duplicate", smid, minuteOfWeek);
        return IOT_OK;
    }

    /* Claim only what actually ran, and only for automations this unit owns — the same
     * rule SmartService_ExecuteTriggerDeduped follows. ACTIVE_BY_SCHEDULE is a LOCATION
     * broadcast: every unit in the location reaches here for every automation in it, and
     * NOT_FOUND simply means "not mine". Claiming before executing would stake claims on
     * other units' automations and, worse, would let a transient failure — a push that
     * lands before the matching BIND_CMD — suppress the tick's retry 60 s later. */
    iot_err_t err = SmartService_ExecuteCommands(smid);
    if (err == IOT_ERR_NOT_FOUND)
    {
        return IOT_OK; // no bind cmds here — location broadcast for another unit, normal
    }
    if (err == IOT_OK)
    {
        claimScheduleRun(smid, minuteOfWeek);
    }
    return err;
}

bool SmartService_IsRecentLocalTrigger(uint16_t smid)
{
    uint32_t now = IOT_OsalGetTickCount();
    uint32_t window = IOT_OsalMsToTicks(LOCAL_TRIG_DEDUP_MS);
    for (int i = 0; i < LOCAL_TRIG_DEDUP_SIZE; i++)
    {
        if (s_localTrigs[i].smid == smid && s_localTrigs[i].tick != 0 &&
            (now - s_localTrigs[i].tick) <= window)
        {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Serialization helpers
// ============================================================================

/**
 * Serialize attrValueList into flat bytes for NVS storage.
 * Format: [cmdCount(2B)] [cmd1] [cmd2] ...
 * Each cmd: [attrId(2B)][attrSize(2B)][attrValue(N)][reverse(2B)][delay(2B)][elmId(2B)]
 */
static uint8_t *serializeBindCmds(IOT_CoreSmartBlockAttrValueReverseDelayElm_t *list,
                                   uint16_t cmdSize, uint16_t *outLen)
{
    // Calculate total size
    uint16_t totalSize = 2; // cmdCount
    for (uint16_t i = 0; i < cmdSize; i++)
    {
        totalSize += 2 + 2 + list[i].attrValue.attrSize + 2 + 2 + 2; // attrId + attrSize + value + reverse + delay + elmId
    }

    uint8_t *buf = (uint8_t *)Mem_SafeMalloc(totalSize, TAG, "serializeBindCmds");
    if (buf == NULL)
    {
        return NULL;
    }

    uint16_t offset = 0;
    buf[offset++] = HIGH_BYTE(cmdSize);
    buf[offset++] = LOW_BYTE(cmdSize);

    for (uint16_t i = 0; i < cmdSize; i++)
    {
        IOT_CoreSmartBlockAttrValueReverseDelayElm_t *item = &list[i];

        buf[offset++] = HIGH_BYTE(item->attrValue.attrId);
        buf[offset++] = LOW_BYTE(item->attrValue.attrId);

        buf[offset++] = HIGH_BYTE(item->attrValue.attrSize);
        buf[offset++] = LOW_BYTE(item->attrValue.attrSize);

        if (item->attrValue.attrValue != NULL && item->attrValue.attrSize > 0)
        {
            Mem_SafeCopy(&buf[offset], totalSize - offset,
                         item->attrValue.attrValue, item->attrValue.attrSize);
        }
        offset += item->attrValue.attrSize;

        buf[offset++] = HIGH_BYTE(item->reverse);
        buf[offset++] = LOW_BYTE(item->reverse);

        buf[offset++] = HIGH_BYTE(item->delay);
        buf[offset++] = LOW_BYTE(item->delay);

        buf[offset++] = HIGH_BYTE(item->elmId);
        buf[offset++] = LOW_BYTE(item->elmId);
    }

    *outLen = totalSize;
    return buf;
}

iot_err_t SmartService_StoreBindCmd(IOT_CoreSmartEventBindCmdData_t *data, uint16_t *outCfm)
{
    if (data == NULL || outCfm == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    uint16_t serializedLen = 0;
    uint8_t *serialized = serializeBindCmds(data->attrValueList, data->cmdSize, &serializedLen);
    if (serialized == NULL)
    {
        IOT_LOGE(TAG, "BIND_CMD: failed to serialize commands");
        return IOT_ERR_NO_MEM;
    }

    uint16_t bindCmdCfm = 1;
    uint16_t existingCfm = 0;
    char **tmpKeys = NULL;
    uint16_t tmpCount = 0;
    void **tmpData = NULL;
    uint16_t *tmpSizes = NULL;
    if (IOT_DaoSmartGetBindCmds(data->smid, &tmpKeys, &tmpCount,
                                &tmpData, &tmpSizes, NULL, NULL, &existingCfm) == IOT_OK)
    {
        bindCmdCfm = existingCfm + 1;
        for (uint16_t i = 0; i < tmpCount; i++)
        {
            SAFE_FREE(tmpKeys[i]);
            SAFE_FREE(tmpData[i]);
        }
        SAFE_FREE(tmpKeys);
        SAFE_FREE(tmpData);
        SAFE_FREE(tmpSizes);
    }

    iot_err_t err = IOT_DaoSmartSetBindCmd(data->smid, data->eid,
                                           data->rootEid, data->prtc, data->filter,
                                           bindCmdCfm,
                                           serialized, serializedLen);
    SAFE_FREE(serialized);
    *outCfm = bindCmdCfm;
    return err;
}

// ============================================================================
// Timer-based execution for delay/reverse
// ============================================================================

typedef struct
{
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint16_t attrSize;
    uint8_t attrValue[8]; // 8 matches the smart subsystem's value buffers (trigger record, deferred eval)
    uint16_t reverse; // seconds; 0 = no reverse
    uint16_t smid;    // the automation this action belongs to
    uint8_t source;   // SMART_ACTION_FROM_*: where the automation keeps it
    bool delayed;     // waited on a timer: check the automation still holds it before applying
    uint32_t generation; // automation change stamp when scheduled (RAM only): a later change drops it
} smart_control_timer_params_t;

#define SMART_ACTION_FROM_BIND_CMD 0u // a stored command for one device of the automation
#define SMART_ACTION_FROM_TRIGGER  1u // synthesised from the automation's own trigger

// A full core work queue refuses the hand-off; the action is tried again this soon.
#define SMART_ACTION_RETRY_MS 100u

// ---- automation change stamps ----------------------------------------------------------------
//
// An action waiting for its delay, its reverse or core's task belongs to the automation as it
// was when the action was scheduled. Checking only that an automation with the same id still
// exists is not enough: it may have been removed and added again (or edited) meanwhile. So every
// add, edit and removal takes a new stamp, every action carries the stamp current when it was
// scheduled, and an action older than its automation's last change is dropped.
//
// RAM only: nothing waits across a reboot. The table remembers the most recent changes; a change
// pushed out of it raises the floor, which every automation not in the table is then assumed to
// have changed at. That can drop an action whose automation did not change (only when more
// automations than the table holds change during one wait), never apply one that did.
// Core's task only, like everything that schedules or applies an action.
#define SMART_CHANGE_TABLE_SIZE 16u

static struct
{
    uint16_t smid;
    uint32_t stamp; // 0 = unused
} s_smartChanges[SMART_CHANGE_TABLE_SIZE];
static uint32_t s_smartChangeStamp; // the last stamp handed out
static uint32_t s_smartChangeFloor; // the newest stamp pushed out of the table

void SmartService_NoteAutomationChanged(uint16_t smid)
{
    uint32_t stamp = ++s_smartChangeStamp;
    uint8_t slot = 0;
    for (uint8_t i = 0; i < SMART_CHANGE_TABLE_SIZE; i++)
    {
        if (s_smartChanges[i].stamp != 0 && s_smartChanges[i].smid == smid)
        {
            slot = i; // already known: just restamp it
            break;
        }
        if (s_smartChanges[i].stamp < s_smartChanges[slot].stamp)
        {
            slot = i; // an unused slot (0), else the oldest change
        }
    }
    if (s_smartChanges[slot].stamp != 0 && s_smartChanges[slot].smid != smid &&
        s_smartChanges[slot].stamp > s_smartChangeFloor)
    {
        s_smartChangeFloor = s_smartChanges[slot].stamp;
    }
    s_smartChanges[slot].smid = smid;
    s_smartChanges[slot].stamp = stamp;
}

static uint32_t automationLastChange(uint16_t smid)
{
    for (uint8_t i = 0; i < SMART_CHANGE_TABLE_SIZE; i++)
    {
        if (s_smartChanges[i].stamp != 0 && s_smartChanges[i].smid == smid)
        {
            return s_smartChanges[i].stamp;
        }
    }
    return s_smartChangeFloor;
}

// The stamp a newly scheduled action carries.
static uint32_t automationCurrentStamp(void)
{
    return s_smartChangeStamp;
}

// Whether the automation still holds this action. A delayed action outlives the request that
// scheduled it, and the automation may have been removed meanwhile.
static bool actionStillWanted(const smart_control_timer_params_t *params)
{
    if (params->source == SMART_ACTION_FROM_TRIGGER)
    {
        IOT_SmartTrigger_t trig;
        return IOT_DaoSmartTriggerGet(params->smid, &trig) == IOT_OK;
    }

    char **keyList = NULL;
    uint16_t keyCount = 0;
    void **dataList = NULL;
    uint16_t *dataSizes = NULL;
    uint16_t rootEid = 0;
    uint16_t prtc = 0;
    if (IOT_DaoSmartGetBindCmds(params->smid, &keyList, &keyCount, &dataList, &dataSizes, &rootEid, &prtc,
                                NULL) != IOT_OK)
    {
        return false;
    }
    bool found = false;
    for (uint16_t i = 0; i < keyCount; i++)
    {
        if (keyList != NULL && keyList[i] != NULL && (uint16_t) strtol(keyList[i], NULL, 16) == params->eid)
        {
            found = true;
        }
        if (keyList != NULL)
        {
            SAFE_FREE(keyList[i]);
        }
        if (dataList != NULL)
        {
            SAFE_FREE(dataList[i]);
        }
    }
    SAFE_FREE(keyList);
    SAFE_FREE(dataList);
    SAFE_FREE(dataSizes);
    return found;
}

// Core's task only: apply one action through the product's STATE_SET handler.
static void applyStateSet(smart_control_timer_params_t *params)
{
    // Build a single-element STATE_SET event
    elem_set_t elemSet = {
        .elemId = params->elemId,
    };

    dev_set_info_t devSetInfo = {
        .numOfElems = 1,
        .eid = params->eid,
        .elemList = &elemSet,
        .attrId = params->attrId,
        .attrSize = params->attrSize,
        .attrValue = params->attrValue,
    };

    IOT_EventStateSetRequest_t setRequest = {
        .targetType = IOT_SET_TARGET_DEVICE,
        .target.device.eid = params->eid,
        .devSetInfo = devSetInfo,
        .ctx = {
            .senderFrom = IOT_SENDER_AUTOMATION, // prevents trigger re-evaluation loop
        },
        .targetDeviceType = 0,
        .result = IOT_ERR_NOT_SUPPORTED, /* nobody is answered for an automation; preset as core does */
    };

    IOT_CoreEvent_t event = {
        .category = IOT_EVENT_STATE,
        .evt.state = {
            .type = IOT_EVENT_TYPE_STATE_SET,
            .data.setRequest = setRequest,
        },
    };

    // Remember the target before dispatching: the change can come back through the state-change
    // entry before this call returns, and a wrong sender there is reported against this note.
    IOT_ContractSenderCheckNoteDispatch(params->eid, params->elemId, params->attrId);
    iot_err_t err = IOT_CoreDispatchEvent(&event);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to dispatch STATE_SET for EID=%04x elem=%04x: %d",
                 params->eid, params->elemId, err);
    }
    else
    {
        IOT_LOGI(TAG, "Dispatched STATE_SET for EID=%04x elem=%04x attr=%04x",
                 params->eid, params->elemId, params->attrId);
    }
}

// Core's task. The work queue frees the arg afterwards.
static void stateSetWork(void *arg)
{
    smart_control_timer_params_t *params = (smart_control_timer_params_t *) arg;
    if (params == NULL)
    {
        return;
    }
    if (automationLastChange(params->smid) > params->generation)
    {
        IOT_LOGI(TAG, "SMID=%04x changed since it scheduled its action for EID=%04x elem=%04x: skipped",
                 params->smid, params->eid, params->elemId);
        return;
    }
    if (params->delayed && !actionStillWanted(params))
    {
        IOT_LOGI(TAG, "SMID=%04x no longer holds its delayed action for EID=%04x elem=%04x: skipped",
                 params->smid, params->eid, params->elemId);
        return;
    }
    applyStateSet(params);
}

// Timer task (a retry): hand the action to core's task again, or wait a little longer.
static void stateSetRetryCb(void *userData)
{
    smart_control_timer_params_t *job = (smart_control_timer_params_t *) userData;
    if (job == NULL)
    {
        return;
    }
    if (IOT_CoreDeferWork(stateSetWork, job) == IOT_OK)
    {
        return; // the work queue owns and frees it now
    }
    if (IOT_TimerInsertCb(stateSetRetryCb, job, SMART_ACTION_RETRY_MS) == IOT_TIMER_HANDLE_INVALID)
    {
        IOT_LOGE(TAG, "Action for EID=%04x elem=%04x lost: no timer to retry it", job->eid, job->elemId);
        SAFE_FREE(job);
    }
}

// Any task. Every action is applied on core's task, whatever noticed it: a delay or schedule
// timer, a cond-group timer, or a request core is handling. The action is copied, so the
// caller keeps its own record.
static void queueStateSet(const smart_control_timer_params_t *params)
{
    smart_control_timer_params_t *job =
        (smart_control_timer_params_t *) Mem_SafeMalloc(sizeof(smart_control_timer_params_t), TAG, "action");
    if (job == NULL)
    {
        IOT_LOGE(TAG, "No memory to apply the action for EID=%04x elem=%04x", params->eid, params->elemId);
        return;
    }
    *job = *params;
    if (IOT_CoreDeferWork(stateSetWork, job) == IOT_OK)
    {
        return; // the work queue owns and frees it now
    }
    // The action must not be lost: try again shortly.
    IOT_LOGW(TAG, "Core work queue full: action for EID=%04x elem=%04x retried in %u ms", params->eid,
             params->elemId, (unsigned) SMART_ACTION_RETRY_MS);
    if (IOT_TimerInsertCb(stateSetRetryCb, job, SMART_ACTION_RETRY_MS) == IOT_TIMER_HANDLE_INVALID)
    {
        IOT_LOGE(TAG, "Action for EID=%04x elem=%04x lost: no timer to retry it", params->eid, params->elemId);
        SAFE_FREE(job);
    }
}

static void delayedExecuteCb(void *userData)
{
    smart_control_timer_params_t *params = (smart_control_timer_params_t *)userData;
    if (params == NULL)
    {
        return;
    }

    queueStateSet(params);

    // If reverse is set and this is an ONOFF attribute, schedule the flip
    if (params->reverse > 0 && params->attrId == IOT_ATTR_ONOFF)
    {
        // Flip ON <-> OFF (2-byte big-endian: 0x0001 <-> 0x0000)
        if (params->attrValue[0] == 0x00 && params->attrValue[1] == 0x01)
        {
            params->attrValue[1] = 0x00;
        }
        else
        {
            params->attrValue[0] = 0x00;
            params->attrValue[1] = 0x01;
        }

        uint32_t reverseMs = (uint32_t)params->reverse * 1000;
        params->reverse = 0; // Prevent infinite loop
        params->delayed = true;

        if (!IOT_TimerInsertCb(delayedExecuteCb, params, reverseMs))
        {
            IOT_LOGE(TAG, "Failed to schedule reverse timer for EID=%04x", params->eid);
            SAFE_FREE(params);
        }
        // params reused by timer — don't free here
        return;
    }

    SAFE_FREE(params);
}

// ----------------------------------------------------------------------------
// The one way a smart feature drives an element.
//
// Auto-reverse, the stair switch and sync-on-off all do the same thing in the end: set ONE
// element of ONE device to an ON/OFF value WITHOUT re-evaluating triggers, optionally after
// a delay, so they share this one helper. applyStateSet() tags the change as
// IOT_SENDER_AUTOMATION, which is what keeps it from re-triggering.
//
// The action is applied on core's task either way. An immediate one is handed there now; a
// delayed one waits on the heap for its timer, which hands it over and frees it.
//
// @return false only when a delayed apply could not be scheduled.
// ----------------------------------------------------------------------------
static bool mirrorOnOffToElement(uint16_t smid, uint16_t eid, uint16_t elemId, uint16_t attrId,
                                 const uint8_t *value, uint16_t size, uint16_t delaySeconds)
{
    if (value == NULL || size == 0)
    {
        return false;
    }

    if (delaySeconds == 0)
    {
        smart_control_timer_params_t params = {0};
        params.eid = eid;
        params.elemId = elemId;
        params.attrId = attrId;
        params.attrSize = (size <= sizeof(params.attrValue)) ? size : sizeof(params.attrValue);
        Mem_SafeCopy(params.attrValue, sizeof(params.attrValue), value, params.attrSize);
        params.reverse = 0;
        params.smid = smid;
        params.source = SMART_ACTION_FROM_TRIGGER;
        params.delayed = false;
        params.generation = automationCurrentStamp();
        queueStateSet(&params);
        return true;
    }

    smart_control_timer_params_t *params = (smart_control_timer_params_t *) Mem_SafeCalloc(
        1, sizeof(smart_control_timer_params_t), TAG, "mirror params");
    if (params == NULL)
    {
        IOT_LOGE(TAG, "Mirror: no memory for EID=%04x elm=%04x", eid, elemId);
        return false;
    }
    params->eid = eid;
    params->elemId = elemId;
    params->attrId = attrId;
    params->attrSize = (size <= sizeof(params->attrValue)) ? size : sizeof(params->attrValue);
    Mem_SafeCopy(params->attrValue, sizeof(params->attrValue), value, params->attrSize);
    params->reverse = 0;
    params->smid = smid;
    params->source = SMART_ACTION_FROM_TRIGGER;
    params->delayed = true;
    params->generation = automationCurrentStamp();

    if (!IOT_TimerInsertCb(delayedExecuteCb, params, (uint32_t) delaySeconds * 1000u))
    {
        IOT_LOGE(TAG, "Mirror: failed to schedule EID=%04x elm=%04x", eid, elemId);
        SAFE_FREE(params);
        return false;
    }
    return true;
}

// ============================================================================
// Sync attribute helpers (sync on/off, normal and reversing)
// ============================================================================

/**
 * Resolve sync attributes: if attrId is a sync type (311, 312), override the
 * command value with the trigger source's current value and transform attrId
 * to IOT_ATTR_ONOFF for actual dispatch.
 *
 * @param[in,out] attrId     Command attribute ID — transformed to ONOFF if sync
 * @param[in,out] attrValue  Command value buffer — overwritten with trigger value
 * @param[in,out] attrSize   Command value size — updated to triggerValueSize
 * @param[in]     maxValueSize  Max bytes that fit in attrValue buffer
 * @param[in]     triggerValue  Trigger source's current attribute value (NULL if unavailable)
 * @param[in]     triggerValueSize  Size of triggerValue
 * @return true if a sync transform was applied
 */
static bool resolveSyncAttr(uint16_t *attrId, uint8_t *attrValue, uint16_t *attrSize,
                            uint16_t maxValueSize,
                            const uint8_t *triggerValue, uint16_t triggerValueSize)
{
    if (triggerValue == NULL || triggerValueSize == 0)
    {
        return false;
    }

    switch (*attrId)
    {
    case IOT_ATTR_SYNC_ONOFF_NORMAL:
    {
        // Copy trigger value as-is (same state)
        uint16_t copySize = (triggerValueSize <= maxValueSize) ? triggerValueSize : maxValueSize;
        Mem_SafeCopy(attrValue, maxValueSize, triggerValue, copySize);
        *attrSize = copySize;
        *attrId = IOT_ATTR_ONOFF;
        IOT_LOGI(TAG, "    Sync NORMAL: trigger value → %02x%02x, attrId → ONOFF",
                 (copySize > 0) ? attrValue[0] : 0, (copySize > 1) ? attrValue[1] : 0);
        return true;
    }
    case IOT_ATTR_SYNC_ONOFF_REVERSING:
    {
        // Copy trigger value then invert ON/OFF (2-byte big-endian)
        uint16_t copySize = (triggerValueSize <= maxValueSize) ? triggerValueSize : maxValueSize;
        Mem_SafeCopy(attrValue, maxValueSize, triggerValue, copySize);
        *attrSize = copySize;
        // Flip: 0x0001 (ON) <-> 0x0000 (OFF)
        if (copySize >= 2)
        {
            if (attrValue[0] == 0x00 && attrValue[1] == 0x01)
            {
                attrValue[1] = 0x00;
            }
            else
            {
                attrValue[0] = 0x00;
                attrValue[1] = 0x01;
            }
        }
        *attrId = IOT_ATTR_ONOFF;
        IOT_LOGI(TAG, "    Sync REVERSING: inverted → %02x%02x, attrId → ONOFF",
                 (copySize > 0) ? attrValue[0] : 0, (copySize > 1) ? attrValue[1] : 0);
        return true;
    }
    default:
        return false;
    }
}

// ============================================================================
// SMART command execution (shared by all ACTIVE_BY_* handlers)
// ============================================================================

/**
 * Execute bind-commands for a smart automation.
 *
 * @param smid          Smart ID
 * @param triggerValue  Trigger source's current attribute value (NULL for non-trigger activation)
 * @param triggerValueSize  Size of triggerValue
 */
iot_err_t SmartService_ExecuteCommandsEx(uint16_t smid,
                                        const uint8_t *triggerValue, uint16_t triggerValueSize)
{
    char **keyList = NULL;
    uint16_t keyCount = 0;
    void **dataList = NULL;
    uint16_t *dataSizes = NULL;
    uint16_t rootEid = 0;
    uint16_t prtc = 0;

    iot_err_t err = IOT_DaoSmartGetBindCmds(smid, &keyList, &keyCount,
                                             &dataList, &dataSizes, &rootEid, &prtc, NULL);
    if (err != IOT_OK)
    {
        // Normal, common case: on a location-wide ACTIVE_BY_TRIGGER most devices are not the
        // action device for this SMID and have no bind cmds. Debug-level, not a warning.
        IOT_LOGD(TAG, "No bind cmds found for SMID=%04x", smid);
        return err;
    }

    IOT_LOGI(TAG, "Executing SMART SMID=%04x: %u device(s), rootEid=%04x, prtc=%04x",
             smid, keyCount, rootEid, prtc);

    for (uint16_t k = 0; k < keyCount; k++)
    {
        uint16_t eid = (uint16_t)strtol(keyList[k], NULL, 16);
        uint8_t *data = (uint8_t *)dataList[k];
        uint16_t dataSize = dataSizes[k];

        if (dataSize < 2)
        {
            IOT_LOGW(TAG, "SMID=%04x EID=%04x: data too short (%u)", smid, eid, dataSize);
            continue;
        }

        uint16_t cmdCount = BYTES_TO_U16_BE(data[0], data[1]);
        uint16_t offset = 2;

        IOT_LOGI(TAG, "  EID=%04x: %u command(s)", eid, cmdCount);

        for (uint16_t c = 0; c < cmdCount; c++)
        {
            // Parse: [attrId(2)][attrSize(2)][attrValue(N)][reverse(2)][delay(2)][elmId(2)]
            if (offset + 4 > dataSize)
            {
                break;
            }
            uint16_t attrId = BYTES_TO_U16_BE(data[offset], data[offset + 1]);
            uint16_t attrSize = BYTES_TO_U16_BE(data[offset + 2], data[offset + 3]);
            offset += 4;

            if (offset + attrSize + 6 > dataSize)
            {
                break;
            }
            uint8_t *attrValue = &data[offset];
            offset += attrSize;

            uint16_t reverse = BYTES_TO_U16_BE(data[offset], data[offset + 1]);
            uint16_t delay = BYTES_TO_U16_BE(data[offset + 2], data[offset + 3]);
            uint16_t elmId = BYTES_TO_U16_BE(data[offset + 4], data[offset + 5]);
            offset += 6;

            IOT_LOGI(TAG, "    Cmd[%u]: attr=%04x size=%u elm=%04x delay=%u reverse=%u",
                     c, attrId, attrSize, elmId, delay, reverse);

            // Allocate timer params (always heap-alloc since timer may use it later)
            smart_control_timer_params_t *params =
                (smart_control_timer_params_t *)Mem_SafeCalloc(1, sizeof(smart_control_timer_params_t),
                                                                TAG, "timer params");
            if (params == NULL)
            {
                IOT_LOGE(TAG, "Failed to allocate timer params");
                continue;
            }

            params->eid = eid;
            params->elemId = elmId;
            params->attrId = attrId;
            params->attrSize = (attrSize <= sizeof(params->attrValue)) ? attrSize : sizeof(params->attrValue);
            Mem_SafeCopy(params->attrValue, sizeof(params->attrValue), attrValue, params->attrSize);
            params->reverse = reverse;
            params->smid = smid;
            params->source = SMART_ACTION_FROM_BIND_CMD;
            params->delayed = (delay != 0);
            params->generation = automationCurrentStamp();

            // Resolve sync attributes (311/312): override stored value with trigger's current value
            resolveSyncAttr(&params->attrId, params->attrValue, &params->attrSize,
                            sizeof(params->attrValue), triggerValue, triggerValueSize);

            if (delay == 0)
            {
                // Execute immediately via timer callback (reuses same free logic)
                delayedExecuteCb(params);
            }
            else
            {
                uint32_t delayMs = (uint32_t)delay * 1000;
                if (!IOT_TimerInsertCb(delayedExecuteCb, params, delayMs))
                {
                    IOT_LOGE(TAG, "Failed to schedule delayed execution for EID=%04x", eid);
                    SAFE_FREE(params);
                }
                else
                {
                    IOT_LOGI(TAG, "    Scheduled execution in %u ms", delayMs);
                }
            }
        }
    }

    // Cleanup
    for (uint16_t i = 0; i < keyCount; i++)
    {
        SAFE_FREE(keyList[i]);
        SAFE_FREE(dataList[i]);
    }
    SAFE_FREE(keyList);
    SAFE_FREE(dataList);
    SAFE_FREE(dataSizes);

    /* This device holds the automation's actions and has run (or scheduled) them. */
    IOT_ContractEventAutomationFired(smid);
    return IOT_OK;
}

/** Convenience wrapper for non-trigger callers (schedule, user, cloud active). */
iot_err_t SmartService_ExecuteCommands(uint16_t smid)
{
    return SmartService_ExecuteCommandsEx(smid, NULL, 0);
}

// ============================================================================
// Trigger condition evaluation
// ============================================================================

#define TRIGGER_COND_ANY     1
#define TRIGGER_COND_EQUAL   2
#define TRIGGER_COND_EXIST   3
#define TRIGGER_COND_BETWEEN 4

/**
 * Compare two byte arrays as big-endian unsigned integers.
 * Returns: negative if a < b, 0 if a == b, positive if a > b.
 */
static int compareBytesBE(const uint8_t *a, uint16_t aLen, const uint8_t *b, uint16_t bLen)
{
    uint16_t maxLen = (aLen > bLen) ? aLen : bLen;
    for (uint16_t i = 0; i < maxLen; i++)
    {
        uint8_t va = (i < aLen) ? a[i] : 0;
        uint8_t vb = (i < bLen) ? b[i] : 0;
        if (va != vb)
        {
            return (int)va - (int)vb;
        }
    }
    return 0;
}

/**
 * Check if a trigger condition matches the current attribute value.
 *
 * @param condition  Trigger condition type (COND_ANY, COND_EQUAL, COND_EXIST, COND_BETWEEN)
 * @param currentValue  Current attribute value bytes
 * @param currentSize   Current attribute value size
 * @param trigger       Trigger with stored match values
 * @return true if condition matches
 */
static bool checkTriggerCondition(uint16_t condition, const uint8_t *currentValue, uint16_t currentSize,
                                  const IOT_SmartTrigger_t *trigger)
{
    switch (condition)
    {
    case TRIGGER_COND_ANY:
        return true;

    case TRIGGER_COND_EQUAL:
    {
        uint16_t cmpLen = (currentSize < trigger->attrSize) ? currentSize : trigger->attrSize;
        return (compareBytesBE(currentValue, cmpLen, trigger->attrValue, cmpLen) == 0);
    }

    case TRIGGER_COND_EXIST:
    {
        // Check if currentValue matches any of the stored values.
        // attrValue stores up to 3 uint16 values packed sequentially (big-endian):
        // [val0_hi][val0_lo][val1_hi][val1_lo][val2_hi][val2_lo]
        uint16_t valueCount = trigger->attrSize / 2;
        if (valueCount > 3)
        {
            valueCount = 3;
        }

        for (uint16_t i = 0; i < valueCount; i++)
        {
            uint16_t offset = i * 2;
            uint16_t cmpLen = (currentSize < 2) ? currentSize : 2;
            if (compareBytesBE(currentValue, cmpLen, &trigger->attrValue[offset], cmpLen) == 0)
            {
                return true;
            }
        }
        return false;
    }

    case TRIGGER_COND_BETWEEN:
    {
        // primaryValue <= currentValue <= secondaryValue
        uint16_t cmpLen = (currentSize < trigger->attrSize) ? currentSize : trigger->attrSize;
        uint16_t cmpLen2 = (currentSize < trigger->secondaryAttrSize) ? currentSize : trigger->secondaryAttrSize;

        int cmpLow = compareBytesBE(currentValue, cmpLen, trigger->attrValue, cmpLen);
        int cmpHigh = compareBytesBE(currentValue, cmpLen2, trigger->secondaryAttrValue, cmpLen2);
        return (cmpLow >= 0 && cmpHigh <= 0);
    }

    default:
        IOT_LOGW(TAG, "Unknown trigger condition: %u", condition);
        return false;
    }
}

/*
 * Pure time-window decision. Split out from checkTriggerTimeWindow() so the
 * policy can be unit-tested without depending on the platform clock: the caller
 * injects the sync state and current time.
 *
 * Policy: a trigger with NO time window always fires. A trigger that HAS a time
 * window only fires when the wall clock is trustworthy (SNTP-synced); before
 * sync the clock reads a ~1970 epoch, which would evaluate the window against a
 * bogus local time and misfire it — so windowed triggers are suppressed.
 *
 * The window itself (timeStart/timeStop/weekDay) is always compared against
 * LOCAL time — see the reference-frame comment below the sync check.
 */
static bool triggerTimeWindowAllows(const IOT_SmartTrigger_t *trigger, bool timeSynced, uint64_t nowMs)
{
    // 0xFFFF for both means always active (no time restriction)
    if (trigger->timeStart == IOT_INVALID_U16 && trigger->timeStop == IOT_INVALID_U16)
    {
        return true;
    }

    // Both 0 also means no time restriction configured
    if (trigger->timeStart == 0 && trigger->timeStop == 0)
    {
        return true;
    }

    // Windowed trigger requires a synced clock; otherwise suppress to avoid a
    // pre-SNTP 1970-epoch misfire.
    if (!timeSynced || nowMs == 0)
    {
        IOT_LOGW(TAG, "  Trigger SMID=%04x has a time window but clock not synced — suppressing",
                 trigger->smid);
        return false;
    }

    // Reference frame: the window's raw digits (timeStart/timeStop/weekDay) are
    // ALWAYS local wall-clock values and must be compared against LOCAL time
    // (the device's configured timezone) — the bind's isDeviceUtc flag does NOT switch
    // this to UTC. The window is stored raw and compared against local time with no
    // frame switch. A UTC-frame comparison here would never fire inside the window
    // the user set in local time.
    time_t nowSec = (time_t)(nowMs / 1000);
    struct tm tmNow;
    struct tm *tmRes = localtime_r(&nowSec, &tmNow);
    if (tmRes == NULL)
    {
        IOT_LOGW(TAG, "Cannot convert current time (local) - suppressing windowed trigger");
        return false;
    }

    // Day-of-week check (0x00 = all days, otherwise bitmask: bit0=Sun..bit6=Sat)
    if (trigger->weekDay != 0x00)
    {
        uint8_t dayOfWeek = (uint8_t)tmNow.tm_wday; // 0=Sun..6=Sat
        IOT_LOGD(TAG, "  Current dayOfWeek=%u, weekDay mask=0x%02x (local)", dayOfWeek, trigger->weekDay);
        if (!((trigger->weekDay >> dayOfWeek) & 1))
        {
            IOT_LOGI(TAG, "  Trigger SMID=%04x outside weekDay mask: day=%u, mask=0x%02x",
                     trigger->smid, dayOfWeek, trigger->weekDay);
            return false;
        }
    }

    uint16_t minuteOfDay = (uint16_t)(tmNow.tm_hour * 60 + tmNow.tm_min);

    uint16_t tStart = trigger->timeStart;
    uint16_t tStop = trigger->timeStop;

    bool inWindow;
    if (tStart <= tStop)
    {
        // Normal range: e.g. 06:00 (360) to 18:00 (1080)
        inWindow = (minuteOfDay >= tStart && minuteOfDay <= tStop);
    }
    else
    {
        // Midnight wrap: e.g. 22:00 (1320) to 06:00 (360)
        inWindow = (minuteOfDay >= tStart || minuteOfDay <= tStop);
    }

    if (!inWindow)
    {
        IOT_LOGI(TAG, "  Trigger SMID=%04x outside time window: current=%u, start=%u, stop=%u",
                 trigger->smid, minuteOfDay, tStart, tStop);
    }

    return inWindow;
}

/**
 * Check if the current time falls within the trigger's time window, using the
 * platform clock and configured device timezone. Thin wrapper over the pure
 * triggerTimeWindowAllows() decision.
 */
static bool checkTriggerTimeWindow(const IOT_SmartTrigger_t *trigger)
{
    uint64_t nowMs = 0;
    (void)IOT_TimerGetU64UnixTimeMs(&nowMs);
    return triggerTimeWindowAllows(trigger, IOT_TimerIsTimeSynced(), nowMs);
}

#ifdef SMART_SERVICE_TESTING
bool SmartService_TestTriggerTimeWindowAllows(const IOT_SmartTrigger_t *trigger, bool timeSynced, uint64_t nowMs)
{
    return triggerTimeWindowAllows(trigger, timeSynced, nowMs);
}
#endif

// ============================================================================
// Smart notification report (SMART_SUB_TYPE_NOTIFICATION)
// ============================================================================

/**
 * Build and send a separate notification report when a SMART_SUB_TYPE_NOTIFICATION
 * trigger matches. Called from the deferred trigger evaluation path (timer thread).
 *
 * The report builder singleton is free at this point because the state-change entry
 * (IOT_ContractStateChanged) already sent its report before scheduling the deferred
 * evaluation.
 */
static iot_err_t sendSmartNotificationReport(uint16_t eid, dev_set_info_t *devSetInfo)
{
    iot_err_t err = IOT_OK;
    uint16_t *changedElemIds = NULL;

    changedElemIds = (uint16_t *) Mem_SafeMalloc(
        sizeof(uint16_t) * devSetInfo->numOfElems, TAG, "nfy_changedElemIds");
    if (changedElemIds == NULL)
    {
        IOT_LOGE(TAG, "Smart nfy: failed to allocate changedElemIds");
        err = IOT_ERR_NO_MEM;
        goto nfy_cleanup;
    }
    for (int i = 0; i < devSetInfo->numOfElems; i++)
        changedElemIds[i] = devSetInfo->elemList[i].elemId;

    {
        IOT_ContractStateReport_t reportReq = {
            .eid = eid,
            .changedElemIds = changedElemIds,
            .numChangedElems = devSetInfo->numOfElems,
            .changedAttrId = devSetInfo->attrId,
            .pushNotify = true,
            .ctx = {0}, // local notification — no sender
        };
        err = IOT_ContractReportState(&reportReq);
        if (err == IOT_OK)
        {
            IOT_LOGI(TAG, "Smart nfy: notification report sent for EID=%04x", eid);
        }
        else
        {
            IOT_LOGE(TAG, "Smart nfy: notification report failed for EID=%04x", eid);
        }
    }

nfy_cleanup:
    SAFE_FREE(changedElemIds);
    return err;
}

// ============================================================================
// Trigger evaluation pipeline (called from the state-change entry, IOT_ContractStateChanged)
// ============================================================================

// Action kinds live on subSmartType — see SMART_SUB_TYPE_* in IOT_ServiceSmart.h.
// `smartType` (0=scene/1=schedule/2=automation) is parsed and stored but nothing
// dispatches on it; the action kinds are classified by subSmartType instead.
// These three are outside the action-kind cluster and are unused by the device.
#define SMART_TYPE_TRIGGER_ACTION_SWITCH_SCENE 96  // 96 -> 104 for type mapping
#define SMART_TYPE_TRIGGER_ACTION_OTHER        104 // 96 -> 104 for type mapping
#define SMART_TYPE_ZONE_CONTROL                128

/**
 * @brief Send smart trigger activation to cloud (the format the cloud expects).
 *
 * Cloud receives this and dispatches bind commands to target devices
 * for cross-device automations.
 */
static void sendSmartTriggerToCloud(const IOT_SmartTrigger_t *trig,
                                    const uint8_t *attrValue, uint16_t attrSize)
{
    IOT_CoreSmartTriggerReport_t report = {
        .smid = trig->smid,
        .attrId = trig->attrId,
        .attrSize = attrSize,
        .attrValue = attrValue,
        .triggerKey = {
            (trig->triggerKeyOwnerEid >> 8) & 0xFF, trig->triggerKeyOwnerEid & 0xFF,
            (trig->triggerKeyOwnerEidCount >> 8) & 0xFF, trig->triggerKeyOwnerEidCount & 0xFF,
            (trig->triggerKeyExtEid >> 8) & 0xFF, trig->triggerKeyExtEid & 0xFF,
            (trig->triggerKeyExtEidCount >> 8) & 0xFF, trig->triggerKeyExtEidCount & 0xFF,
        },
        .timeCfg = {
            (trig->timCfgType >> 8) & 0xFF, trig->timCfgType & 0xFF,
            (trig->timCfgValue >> 8) & 0xFF, trig->timCfgValue & 0xFF,
        },
    };

    iot_err_t err = IOT_CoreRequestSmartTriggerReport(&report);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "  SMID=%04x: failed to send trigger to cloud: %d", trig->smid, err);
    }
    else
    {
        IOT_LOGI(TAG, "  SMID=%04x: trigger report sent to cloud", trig->smid);
    }
}

// Run a MATCHED trigger's action according to its subSmartType. Shared by the ordinary
// evaluation path and the cond-group fire path so both honour NOTIFICATION /
// STAIR_SWITCH / REVERSE_ON_OFF identically — otherwise a cond-group would only run the
// `default:` arm and a NOTIFICATION automation would never notify.
//
// Returns true when the caller must emit a notification report. The caller owns that
// step because the report needs the changed-element list: the ordinary path batches ONE
// report per evaluation, while a group fire synthesises it from the trigger itself.
// subSmartType 70 (REVERSE_ON_OFF, auto-reverse): drive the watched element
// back to the OPPOSITE of its trigger value after timCfgValue seconds.
//
// There is NO bind command to execute. The device receives only a trigger bind for this
// automation -- no command bind arrives -- so the action is implicit in the trigger and
// has to be synthesised here: own eid, own element, the inverse of the trigger's own
// stored value, delayed by timCfgValue. The delay travels in the DELAY slot of the
// device control call with reverse = 0 -- not in the reverse slot.
//
// The inverse comes from the trigger's STORED value, not the live value that matched.
// That keeps the action idempotent under COND_ANY, which fires on every change: "always
// drive to OFF" cannot ping-pong, whereas inverting the live value would flip the element
// on and off forever.
static void fireReverseOnOff(const IOT_SmartTrigger_t *trig)
{
    if (trig->attrId != IOT_ATTR_ONOFF)
    {
        // Only ON/OFF has a meaningful inverse; inverting a scalar such as brightness
        // would write a nonsense value.
        IOT_LOGW(TAG, "  SMID=%04x: auto-reverse on attr=%04x ignored (ON/OFF only)",
                 trig->smid, trig->attrId);
        return;
    }

    uint16_t storedValue = (trig->attrSize >= 2)
                               ? BYTES_TO_U16_BE(trig->attrValue[0], trig->attrValue[1])
                               : trig->attrValue[0];
    uint16_t inverse = storedValue ? 0u : 1u;

    uint8_t inverseValue[2] = {HIGH_BYTE(inverse), LOW_BYTE(inverse)};

    if (!mirrorOnOffToElement(trig->smid, trig->eid, trig->elmId, IOT_ATTR_ONOFF, inverseValue,
                              sizeof(inverseValue), trig->timCfgValue))
    {
        IOT_LOGE(TAG, "  SMID=%04x: auto-reverse could not be scheduled", trig->smid);
        return;
    }

    IOT_LOGI(TAG, "  SMID=%04x: auto-reverse elm=%04x -> %u in %u ms", trig->smid, trig->elmId,
             inverse, (unsigned) trig->timCfgValue * 1000u);
}

// subSmartType 64 (STAIR_SWITCH, stair switch): two switches mirror each
// other's ON/OFF, so either one controls the light.
//
// As with auto-reverse there is NO bind command -- each device receives one trigger bind,
// watching its OWN element, plus the partner it mirrors to. So the
// action is implicit in the trigger and is synthesised here.
//
// The mirrored value is the LIVE value that matched, NOT the trigger's stored value: a
// staircase switch follows the current state. (Auto-reverse deliberately does the opposite.)
//
// Routing: a partner behind our
// own root is driven locally; a partner on a different root is sent attribute 313, which the
// receiving side rewrites to ON/OFF with trigger evaluation suppressed. Two standalone devices
// are each their own root, so the cross-root path is the ordinary case.
static void fireStairSwitch(const IOT_SmartTrigger_t *trig, const uint8_t *attrValue,
                            uint16_t attrSize)
{
    // No partner means no target. Writing to eid 0 / element 0 would be worse than doing
    // nothing.
    if (trig->eidMix == 0 || trig->rootEidMix == 0 || trig->elemIdMix == 0)
    {
        IOT_LOGW(TAG, "  SMID=%04x: stair switch has no mix target (eid=%04x root=%04x elm=%04x)",
                 trig->smid, trig->eidMix, trig->rootEidMix, trig->elemIdMix);
        return;
    }

    if (attrValue == NULL || attrSize == 0)
    {
        IOT_LOGW(TAG, "  SMID=%04x: stair switch has no value to mirror", trig->smid);
        return;
    }

    uint16_t ownRoot = 0;
    if (IOT_CoreGetRootEid(&ownRoot) == IOT_OK && trig->rootEidMix == ownRoot)
    {
        // Partner sits behind our own root (e.g. a hub sub-device): drive it directly rather
        // than publishing a frame addressed back to ourselves.
        IOT_LOGI(TAG, "  SMID=%04x: stair switch (same root) -> eid=%04x elm=%04x", trig->smid,
                 trig->eidMix, trig->elemIdMix);
        mirrorOnOffToElement(trig->smid, trig->eidMix, trig->elemIdMix, IOT_ATTR_ONOFF, attrValue, attrSize, 0);
        return;
    }

    // Cross-root: attribute 313 tells the receiver "apply this ON/OFF but do NOT evaluate
    // your own triggers", which is what stops two mirrored switches oscillating forever.
    // The mirrored value is 2-byte big-endian ON/OFF, so the significant byte is the last.
    bool on = (attrValue[attrSize - 1] != 0);

    IOT_LOGI(TAG, "  SMID=%04x: stair switch -> root=%04x eid=%04x elm=%04x", trig->smid,
             trig->rootEidMix, trig->eidMix, trig->elemIdMix);
    if (IOT_CoreSmartStairSwitchMirror(trig->rootEidMix, trig->eidMix, trig->elemIdMix, on)
        != IOT_OK)
    {
        IOT_LOGE(TAG, "  SMID=%04x: stair switch send failed", trig->smid);
    }
}

static bool fireTriggerBySubSmartType(const IOT_SmartTrigger_t *trig,
                                      const uint8_t *attrValue, uint16_t attrSize)
{
    switch (trig->subSmartType)
    {
    case SMART_SUB_TYPE_NOTIFICATION:
        IOT_LOGI(TAG, "  SMID=%04x: notification trigger matched", trig->smid);
        return true;

    case SMART_SUB_TYPE_STAIR_SWITCH:
        // Legacy, one partner only. No build-flag check here: core always exports
        // IOT_CoreSmartStairSwitchMirror and answers IOT_ERR_NOT_SUPPORTED when the legacy
        // layer is not compiled in, which fireStairSwitch logs. That keeps this path
        // genuinely compiled in every build rather than folded away by the preprocessor.
        // Synthesised action, no bind cmd and no cloud trigger report -- see fireStairSwitch.
        fireStairSwitch(trig, attrValue, attrSize);
        return false;

    case SMART_SUB_TYPE_REVERSE_ON_OFF:
        // Synthesised action, no bind cmd and no cloud trigger report — see fireReverseOnOff.
        // The resulting STATE_SET reports the new state, which is all that is emitted here.
        fireReverseOnOff(trig);
        return false;

    default:
        // Standard path: local execution + cloud notification.
        // Local exec may fail for cross-device smart (bind cmds on other device) — that's
        // normal. Cloud notification is the real execution path for cross-device.
        // Mirrors SmartService_ExecuteTriggerDeduped: only stake the dedup claim
        // when local execution actually found and ran a bind command. Staking it on
        // IOT_ERR_NOT_FOUND (the normal cross-device case) would claim the SMID before the
        // cloud's authoritative follow-up command arrives, discarding that command as a
        // duplicate within the dedup window.
        iot_err_t execErr = SmartService_ExecuteCommandsEx(trig->smid, attrValue, attrSize);
        if (execErr == IOT_OK)
            SmartService_MarkLocalTrigger(trig->smid);
        sendSmartTriggerToCloud(trig, attrValue, attrSize);
        return false;
    }
}

// Fire a cond-group's action once the K-of-N threshold is met.
// Lives here (not in smart_cond_group.c) so it can reach the static cloud-report and
// notification helpers. Runs the SAME smartType dispatch as the ordinary path.
void SmartService_CondGroupFire(const IOT_SmartTrigger_t *trig,
                                const uint8_t *attrValue, uint16_t attrSize)
{
    if (trig == NULL)
        return;

    if (fireTriggerBySubSmartType(trig, attrValue, attrSize))
    {
        // A group fire has no per-evaluation dev_set_info_t (peer-driven fires are
        // reconstructed from the DAO), so synthesise the minimum the report needs:
        // the watched element and the trigger's attribute.
        elem_set_t elem = {.elemId = trig->elmId};
        dev_set_info_t info = {
            .numOfElems = 1,
            .eid = trig->eid,
            .elemList = &elem,
            .attrId = trig->attrId,
            .attrSize = attrSize,
            .attrValue = (uint8_t *) attrValue,
        };
        sendSmartNotificationReport(trig->eid, &info);
    }
}

// ============================================================================
// Cond-group boot / cloud-reconnect re-sync (glue over the scoreboard).
// Lives here (not smart_cond_group.c) because it re-evaluates each condition
// from persisted device state using the static condition/time-window helpers.
// ============================================================================

static bool s_condResyncPending;

// ---- timer -> core's task hand-off for the cond-group timers ----------------------------------
// Re-sync, re-ask, re-announce and the deferred SYNC answer all read the trigger store and send
// a message, which the timer task's stack must not carry. Their timers only hand the work to
// core's task; a full work queue refuses it and a short timer tries again.

typedef struct
{
    void (*body)(uintptr_t ctx); // core's task
    void (*lost)(uintptr_t ctx); // optional: the work could not be handed over at all; only
                                 // releases what ctx holds (light: any task)
    uintptr_t ctx;               // not owned: a smid, or a pointer into a static pool
} smart_core_job_t;

// Core's task. The work queue frees the job afterwards.
static void smartCoreJobWork(void *arg)
{
    smart_core_job_t *job = (smart_core_job_t *) arg;
    if (job != NULL && job->body != NULL)
    {
        job->body(job->ctx);
    }
}

static void smartCoreJobLost(smart_core_job_t *job)
{
    if (job->lost != NULL)
    {
        job->lost(job->ctx);
    }
}

// Timer task (or a retry): hand the job to core's task, or try again shortly. Light work only.
static void smartCoreJobHandOff(void *userData)
{
    smart_core_job_t *job = (smart_core_job_t *) userData;
    if (job == NULL)
    {
        return;
    }
    if (IOT_CoreDeferWork(smartCoreJobWork, job) == IOT_OK)
    {
        return; // the work queue owns and frees it now
    }
    if (IOT_TimerInsertCb(smartCoreJobHandOff, job, SMART_ACTION_RETRY_MS) == IOT_TIMER_HANDLE_INVALID)
    {
        IOT_LOGE(TAG, "Cond-group work lost: core queue full and no timer free");
        smartCoreJobLost(job);
        SAFE_FREE(job);
    }
}

// Timer task: run body(ctx) on core's task.
static void smartRunOnCoreTask(void (*body)(uintptr_t ctx), void (*lost)(uintptr_t ctx), uintptr_t ctx)
{
    smart_core_job_t *job = (smart_core_job_t *) Mem_SafeMalloc(sizeof(smart_core_job_t), TAG, "core job");
    if (job == NULL)
    {
        IOT_LOGE(TAG, "No memory to hand cond-group work to core's task");
        if (lost != NULL)
        {
            lost(ctx);
        }
        return;
    }
    job->body = body;
    job->lost = lost;
    job->ctx = ctx;
    smartCoreJobHandOff(job);
}

// Bounded re-ask of the boot SYNC. A SYNC answer goes to the requester alone and may be
// lost; if it is, the requester stays stale. We therefore re-send the SYNC GET a fixed,
// small number of times. The GET re-announces our own (still-stale) view, so a peer that
// still differs answers again → the lost answer is recovered. It is NOT "retry until
// answered" (silence is ambiguous — it also means "everyone agrees"), so the count is
// FIXED: an in-sync location costs only a couple of extra GETs at boot, then stops.
#define COND_SYNC_MAX_RETRIES 2
#define COND_SYNC_RETRY_INTERVAL_MS 4000
static uint8_t s_condResyncRetriesLeft;

// Current boolean result of a cond-group trigger's condition, from stored state.
static bool computeOwnCondResult(const IOT_SmartTrigger_t *trig)
{
    IOT_DaoDeviceElemsAttrsInfo_t st = {0};
    if (IOT_DaoDeviceGetDeviceElemsState(trig->eid, &st) != IOT_OK)
        return false;

    bool result = false;
    for (uint16_t e = 0; e < st.numOfElem; e++)
    {
        if (st.elemList[e].elemId != trig->elmId)
            continue;
        for (uint8_t a = 0; a < st.elemList[e].numOfAttr; a++)
        {
            IOT_DaoDeviceStateAttr_t *at = &st.elemList[e].attrList[a];
            if (at->attrId != trig->attrId)
                continue;
            result = checkTriggerCondition(trig->condition, at->attrValue, at->attrSize, trig) &&
                     checkTriggerTimeWindow(trig);
            break;
        }
        break;
    }
    IOT_DaoDeviceFreeElemsInfo(&st);
    return result;
}

// Re-announce our own bit for a list of cond-group SMIDs we own (no fire).
static void condResyncAnnounce(const uint16_t *smids, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++)
    {
        IOT_SmartTrigger_t trig;
        if (IOT_DaoSmartTriggerGet(smids[i], &trig) != IOT_OK)
            continue;
        if (!SmartTriggerIsCondGroup(&trig) || trig.enabled == 0)
            continue;
        SmartService_CondGroupResyncOwnBit(&trig, computeOwnCondResult(&trig));
    }
}

// Collect the SMIDs of every owned, enabled cond-group trigger from the DAO.
// Returns the count; caller must SAFE_FREE(*outSmids). 0 (and *outSmids == NULL) = none.
static uint16_t condResyncCollectGroups(uint16_t **outSmids)
{
    *outSmids = NULL;
    uint16_t *smids = NULL;
    uint16_t smidCount = 0;
    if (IOT_DaoSmartTriggerGetAllSmids(&smids, &smidCount) != IOT_OK || smids == NULL || smidCount == 0)
    {
        SAFE_FREE(smids);
        return 0;
    }

    uint16_t *groupSmids = (uint16_t *) Mem_SafeMalloc(smidCount * sizeof(uint16_t), TAG, "resync smids");
    uint16_t groupCount = 0;
    if (groupSmids != NULL)
    {
        for (uint16_t i = 0; i < smidCount; i++)
        {
            IOT_SmartTrigger_t trig;
            if (IOT_DaoSmartTriggerGet(smids[i], &trig) != IOT_OK)
                continue;
            if (!SmartTriggerIsCondGroup(&trig) || trig.enabled == 0)
                continue;
            groupSmids[groupCount++] = trig.smid;
        }
    }
    SAFE_FREE(smids);

    if (groupCount == 0)
    {
        SAFE_FREE(groupSmids);
        return 0;
    }
    *outSmids = groupSmids;
    return groupCount;
}

// Bounded re-ask: re-send ONLY the SYNC GET (no own-bit SET re-flood). The GET
// re-announces our own view, so a peer that still differs answers again — recovering a
// lost answer. Stops after COND_SYNC_MAX_RETRIES regardless of what came back.
static void condResyncRetryTimerCb(void *userData);

static void condResyncRetryBody(uintptr_t ctx)
{
    (void) ctx;
    if (s_condResyncRetriesLeft == 0)
        return;
    s_condResyncRetriesLeft--;

    uint16_t *groupSmids = NULL;
    uint16_t groupCount = condResyncCollectGroups(&groupSmids);
    if (groupCount > 0)
        SmartService_CondGroupSendCheck(groupSmids, groupCount, false); /* re-sync: nothing is waiting */
    SAFE_FREE(groupSmids);

    if (s_condResyncRetriesLeft > 0)
        IOT_TimerInsertCb(condResyncRetryTimerCb, NULL, COND_SYNC_RETRY_INTERVAL_MS);
}

// Timer task: only hands the re-ask over.
static void condResyncRetryTimerCb(void *userData)
{
    (void) userData;
    smartRunOnCoreTask(condResyncRetryBody, NULL, 0);
}

// Core's task: the boot / re-enable re-sync.
static void condResyncBody(uintptr_t ctx)
{
    (void) ctx;
    s_condResyncPending = false;

    uint16_t *smids = NULL;
    uint16_t smidCount = 0;
    if (IOT_DaoSmartTriggerGetAllSmids(&smids, &smidCount) != IOT_OK || smids == NULL || smidCount == 0)
    {
        SAFE_FREE(smids);
        return;
    }

    uint16_t *groupSmids = (uint16_t *) Mem_SafeMalloc(smidCount * sizeof(uint16_t), TAG, "resync smids");
    uint16_t groupCount = 0;
    for (uint16_t i = 0; i < smidCount; i++)
    {
        IOT_SmartTrigger_t trig;
        if (IOT_DaoSmartTriggerGet(smids[i], &trig) != IOT_OK)
            continue;
        if (!SmartTriggerIsCondGroup(&trig) || trig.enabled == 0)
            continue;
        SmartService_CondGroupResyncOwnBit(&trig, computeOwnCondResult(&trig));
        if (groupSmids != NULL)
            groupSmids[groupCount++] = trig.smid;
    }

    if (groupSmids != NULL && groupCount > 0)
        SmartService_CondGroupSendCheck(groupSmids, groupCount, false); /* re-sync: nothing is waiting */

    SAFE_FREE(groupSmids);
    SAFE_FREE(smids);

    // Arm bounded re-asks to recover a lost SYNC answer (only if we actually own a group).
    s_condResyncRetriesLeft = COND_SYNC_MAX_RETRIES;
    if (groupCount > 0 && COND_SYNC_MAX_RETRIES > 0)
        IOT_TimerInsertCb(condResyncRetryTimerCb, NULL, COND_SYNC_RETRY_INTERVAL_MS);
}

// The re-sync never reached core's task: let the next request schedule one again.
static void condResyncLost(uintptr_t ctx)
{
    (void) ctx;
    s_condResyncPending = false;
}

// Timer task: only hands the re-sync over.
static void condResyncTimerCb(void *userData)
{
    (void) userData;
    smartRunOnCoreTask(condResyncBody, condResyncLost, 0);
}

void SmartService_CondGroupSyncSchedule(void)
{
    if (s_condResyncPending)
        return;
    s_condResyncPending = true;
    IOT_LOGI(TAG, "Scheduling cond-group re-sync in 5s");
    IOT_TimerInsertCb(condResyncTimerCb, NULL, 5000);
}

bool SmartService_CondGroupOnEnableChange(uint16_t smid, bool enabled)
{
    // A disabled cond-group ignores peer bits, so its scoreboard mask is stale on
    // re-enable. Refresh it with a re-sync (own-bit re-announce + SYNC GET). A missing
    // trigger or a non-group smid → nothing to do either way.
    IOT_SmartTrigger_t trig;
    if (IOT_DaoSmartTriggerGet(smid, &trig) != IOT_OK)
        return false;
    if (!SmartTriggerIsCondGroup(&trig))
        return false;

    if (!enabled)
    {
        // Free the scoreboard seat. The table is a hard cap on concurrent groups, and
        // TriggerEvaluate skips disabled triggers BEFORE the cond-group branch — so a
        // disabled group's entry can never be updated again. Holding it is a pure leak
        // that starves live groups. Re-enabling rebuilds it lazily from the DAO.
        SmartService_CondGroupRemove(smid);
        return false;
    }

    SmartService_CondGroupSyncSchedule();
    return true;
}

// One-SMID jittered self re-announce, scheduled when a config-generation bump is
// observed (higher-version peer report or a fresh cond-group BIND). Re-announces THIS
// device's own bit only (no fire) so every member re-floods its bit at the new
// generation and the full mask converges. The jitter spreads a location-wide flood.
static void condReannounceBody(uintptr_t ctx)
{
    uint16_t smid = (uint16_t) ctx;
    condResyncAnnounce(&smid, 1);
}

// Timer task: only hands the re-announce over.
static void condReannounceTimerCb(void *userData)
{
    smartRunOnCoreTask(condReannounceBody, NULL, (uintptr_t) userData);
}

void SmartService_CondGroupScheduleReannounce(uint16_t smid)
{
    uint32_t jitter = 100 + (IOT_OsalGetTickCount() % 1400);
    IOT_TimerInsertCb(condReannounceTimerCb, (void *) (uintptr_t) smid, jitter);
}

// --- Deferred SYNC-answer pool -------------------------------------------------
// When a peer GETs cond-group status, we answer after a small random jitter (to avoid
// answer bursts after a location-wide power loss). The pending answer must survive that
// jitter window, so it can't live on the stack — but it also must not churn the heap on
// this hot path. Instead it lives in a fixed static pool: bounded RAM, leak-free by
// construction, and correctness is independent of the pool size (a full pool just answers
// immediately instead of deferring — no drop). Guarded by its own mutex: requests and the
// answers both run on core's task now, but a lost hand-off releases its slot from the timer task.
#define COND_ANSWER_SLOTS 8

typedef struct
{
    bool inUse;
    uint16_t requesterEid;                                // unicast the answer back here
    uint16_t count;                                       // number of owned groups below
    IOT_CoreSmartCondStatus_t groups[COND_GROUP_MAX_ENTRIES]; // requester's announced status per group
} cond_answer_slot_t;

static cond_answer_slot_t s_answerSlots[COND_ANSWER_SLOTS]; // ~1.5 KB static, mutex-guarded
static IOT_OsalMutexHandle_t s_answerLock = NULL;

void SmartService_CondAnswerPoolInit(void)
{
    // Create the pool lock ONCE, on the single-threaded SDK-init path, before the
    // timer task or core's event task can touch the pool. Idempotent; also clears the pool
    // so a re-init (or a test reset) starts from a known-empty state.
    if (s_answerLock == NULL)
        s_answerLock = IOT_OsalMutexCreate();
    if (s_answerLock == NULL)
        IOT_LOGE(TAG, "failed to create cond-answer pool mutex");
    for (int i = 0; i < COND_ANSWER_SLOTS; i++)
        s_answerSlots[i].inUse = false;
}

static void answerLock(void)
{
    if (s_answerLock) IOT_OsalMutexLock(s_answerLock, 1000);
}
static void answerUnlock(void)
{
    if (s_answerLock) IOT_OsalMutexUnlock(s_answerLock);
}

// Claim a slot for `requesterEid` and copy the answer into it — atomically under the lock.
// Returns:  1 = fresh slot claimed (caller schedules the timer, *out=slot)
//           0 = coalesced onto an existing pending slot (its timer already runs; do NOT
//               schedule a second one — the pending answer was refreshed with the latest view)
//          -1 = pool full (caller answers immediately from the stack copy)
static int answerEnqueue(uint16_t requesterEid, const IOT_CoreSmartCondStatus_t *groups,
                         uint16_t count, cond_answer_slot_t **out)
{
    int result = -1;
    cond_answer_slot_t *freeSlot = NULL;
    *out = NULL;
    answerLock();
    for (int i = 0; i < COND_ANSWER_SLOTS; i++)
    {
        if (s_answerSlots[i].inUse && s_answerSlots[i].requesterEid == requesterEid)
        {
            // Retransmit / second GET from the same device — refresh in place, keep one timer.
            memcpy(s_answerSlots[i].groups, groups, count * sizeof(groups[0]));
            s_answerSlots[i].count = count;
            *out = &s_answerSlots[i];
            answerUnlock();
            return 0;
        }
        if (!s_answerSlots[i].inUse && freeSlot == NULL)
            freeSlot = &s_answerSlots[i];
    }
    if (freeSlot)
    {
        freeSlot->inUse = true;
        freeSlot->requesterEid = requesterEid;
        memcpy(freeSlot->groups, groups, count * sizeof(groups[0]));
        freeSlot->count = count;
        *out = freeSlot;
        result = 1;
    }
    answerUnlock();
    return result;
}

static void answerRelease(cond_answer_slot_t *slot)
{
    if (slot == NULL)
        return;
    answerLock();
    slot->inUse = false;
    answerUnlock();
}

// Core's task: send the deferred answer and free its slot.
static void condAnswerBody(uintptr_t ctx)
{
    cond_answer_slot_t *slot = (cond_answer_slot_t *) ctx;
    if (slot == NULL)
        return;
    // Answer the requester — but ONLY for groups where our view actually differs from the
    // status it announced, and unicast to that device (not the whole location).
    SmartService_CondGroupAnswerSync(slot->groups, slot->count, slot->requesterEid);
    answerRelease(slot);
}

// The answer never reached core's task: free its slot so the pool is not consumed for good.
// The requester re-asks if it still needs the answer.
static void condAnswerLost(uintptr_t ctx)
{
    answerRelease((cond_answer_slot_t *) ctx);
}

// Timer task (after the jitter): only hands the answer over. The slot stays claimed until
// core's task has sent it, so a repeat GET meanwhile still coalesces into it.
static void condAnswerTimerCb(void *userData)
{
    smartRunOnCoreTask(condAnswerBody, condAnswerLost, (uintptr_t) userData);
}

void SmartService_CondGroupHandleCheckQuery(const IOT_CoreSmartCondStatus_t *reqGroups,
                                            uint16_t count, uint16_t requesterEid)
{
    // Drop our own echo.
    uint16_t ownEid = 0;
    if (IOT_CoreGetRootEid(&ownEid) == IOT_OK && requesterEid != 0 && requesterEid == ownEid)
        return;
    if (reqGroups == NULL || count == 0 || requesterEid == 0)
        return;

    // Keep only the queried groups we actually own an enabled cond-group trigger for.
    // Built on the STACK — bounded by COND_GROUP_MAX_ENTRIES, no heap.
    IOT_CoreSmartCondStatus_t owned[COND_GROUP_MAX_ENTRIES];
    uint16_t ownedCount = 0;
    for (uint16_t i = 0; i < count && ownedCount < COND_GROUP_MAX_ENTRIES; i++)
    {
        IOT_SmartTrigger_t trig;
        iot_err_t got = IOT_DaoSmartTriggerGet(reqGroups[i].smid, &trig);
        if (got != IOT_OK)
        {
            /* We hold NO trigger for this smid — say so explicitly, unicast to the asker.
             * This is the ONLY signal that lets an asker tell a REMOVED peer from an
             * offline one: silence means offline and must never retire a seat. It has to
             * happen here, not in AnswerSync — non-owned SMIDs are filtered out below and
             * ownedCount==0 returns early, so AnswerSync never sees this case.
             *
             * Only a positive NOT_FOUND proves absence; any other storage failure stays
             * silent, and a trigger we DO hold but that is not a cond-group (the stale
             * N=1 first member) falls through to the next check — that device IS
             * participating and must not be disowned. */
            /* Answer ONLY a single-group ask. That is ask-before-fire
             * — the one moment a retirement can change an outcome, because a fire is
             * gated on the answer. The reboot re-sync asks about EVERY group a device holds
             * and nothing waits on those answers, yet each non-member would emit one unicast
             * PER ABSENT SMID: with 10 devices and 20 groups that is ~1400 messages after a
             * power cut, for no benefit.
             *
             * Read the INTENT from the request's askForFire flag rather than inferring it from
             * the group count: a device that happens to hold exactly one group re-syncs with
             * count==1, which is indistinguishable from a fire-ask, so a count-based limit
             * would not stop the reply storm. */
            if (got == IOT_ERR_NOT_FOUND && reqGroups[i].askForFire)
            {
                IOT_LOGI(TAG, "SYNC GET: SMID=%04x is not ours — answering not-member to %04x",
                         reqGroups[i].smid, requesterEid);
                IOT_CoreRequestSmartCondNotMember(reqGroups[i].smid, requesterEid);
            }
            continue;
        }
        if (!SmartTriggerIsCondGroup(&trig) || trig.enabled == 0)
            continue;
        owned[ownedCount++] = reqGroups[i];
        // Re-evaluate our own condition (attr + TIME WINDOW) NOW so the answer reflects the
        // CURRENT state, not a bit latched before a window boundary. ResyncOwnBit updates our
        // bit + re-announces it (never fires — a rebuild is not a fresh edge). This is what makes
        // ask-before-fire close the time-window gap: an asker gets our up-to-date, window-checked bit.
        SmartService_CondGroupResyncOwnBit(&trig, computeOwnCondResult(&trig));
    }
    if (ownedCount == 0)
        return;

    cond_answer_slot_t *slot = NULL;
    int claim = answerEnqueue(requesterEid, owned, ownedCount, &slot);
    if (claim == 0)
        return; // coalesced onto an existing pending answer — its timer already runs
    if (claim < 0)
    {
        // Pool full: answer immediately (no drop, no heap).
        SmartService_CondGroupAnswerSync(owned, ownedCount, requesterEid);
        return;
    }

    // Fresh slot: defer with jitter 100..1499 ms.
    uint32_t jitter = 100 + (IOT_OsalGetTickCount() % 1400);
    if (IOT_TimerInsertCb(condAnswerTimerCb, slot, jitter) == IOT_TIMER_HANDLE_INVALID)
    {
        // Couldn't defer (timer pool exhausted): answer now and free the slot — never leak.
        SmartService_CondGroupAnswerSync(slot->groups, slot->count, slot->requesterEid);
        answerRelease(slot);
    }
}

// Parse a sync-on-off element list (N x uint16 big-endian) from the primary
// attr-value bytes. A trailing odd byte is ignored. Returns count written (<=cap).
uint8_t SmartService_ParseSyncOnOffElements(const uint8_t *value, uint16_t size, uint16_t *out,
                                            uint8_t cap)
{
    if (value == NULL || out == NULL)
    {
        return 0;
    }
    uint8_t n = 0;
    for (uint16_t off = 0; off + 2 <= size && n < cap; off += 2)
    {
        out[n++] = (uint16_t)((value[off] << 8) | value[off + 1]);
    }
    return n;
}

// Resolve a trigger's bound element set. For a sync-on-off trigger (subSmartType
// 71) that is elemList[0..elmNum); for any legacy/other trigger it is the single
// elmId. Returns the count written into `out` (>= 1), bounded by `cap`.
static uint8_t syncOnOffBoundElements(const IOT_SmartTrigger_t *trig, uint16_t *out, uint8_t cap)
{
    if (trig->elmNum == 0 || cap == 0)
    {
        if (cap > 0)
        {
            out[0] = trig->elmId;
            return 1;
        }
        return 0;
    }

    uint8_t n = trig->elmNum;
    if (n > SMART_TRIGGER_ELM_CAP)
    {
        n = SMART_TRIGGER_ELM_CAP;
    }
    if (n > cap)
    {
        n = cap;
    }
    for (uint8_t i = 0; i < n; i++)
    {
        out[i] = trig->elemList[i];
    }
    return n;
}

// True if any of the just-changed elements is in the bound set.
static bool changedElemInBoundSet(const dev_set_info_t *devSetInfo, const uint16_t *bound,
                                  uint8_t boundCount)
{
    for (int e = 0; e < devSetInfo->numOfElems; e++)
    {
        for (uint8_t b = 0; b < boundCount; b++)
        {
            if (devSetInfo->elemList[e].elemId == bound[b])
            {
                return true;
            }
        }
    }
    return false;
}

/* --- one automation kind per function; the classifier below picks exactly one --- */

/* A group (>= 2 seats). This device may hold SEVERAL of them, watching different
 * elements/attributes, so every owned seat is matched and evaluated on its own — the
 * caller's single loaded record speaks only for the lowest seat. The scoreboard must
 * also see FALSE results (a falling edge re-arms the group), so a seat is delivered
 * unconditionally rather than gated on its condition. */
/* ============================================================================
 * Trigger dispatch
 *
 * One automation kind per function, chosen by classifyTrigger(). The kinds are mutually
 * exclusive and the classifier is the ONLY place that decides, so the routing rule never
 * depends on the POSITION of a branch in the evaluation loop ("this must sit before that"),
 * which is not something a reader can see or a compiler can check.
 * ==========================================================================*/

typedef enum
{
    TRIGGER_KIND_ORDINARY = 0, /* level-triggered, full subSmartType action dispatch */
    TRIGGER_KIND_SYNC_ON_OFF,  /* mirrors ON/OFF across an element LIST, own broadcast */
    TRIGGER_KIND_COND_GROUP,   /* >= 2 seats: distributed K-of-N scoreboard */
} trigger_kind_t;

static trigger_kind_t classifyTrigger(const IOT_SmartTrigger_t *trig)
{
    /* Order matters HERE and nowhere else: sync-on-off owns its subSmartType outright and
     * must never take a scoreboard seat, so it is tested first. */
    if (trig->subSmartType == SMART_SUB_TYPE_SYNC_ON_OFF)
    {
        return TRIGGER_KIND_SYNC_ON_OFF;
    }
    if (SmartTriggerIsCondGroup(trig))
    {
        return TRIGGER_KIND_COND_GROUP;
    }
    return TRIGGER_KIND_ORDINARY;
}

/* Does any element that just changed match this trigger's watched element? */
static bool changedElemMatches(const IOT_SmartTrigger_t *trig, const dev_set_info_t *devSetInfo)
{
    for (int e = 0; e < devSetInfo->numOfElems; e++)
    {
        if (devSetInfo->elemList[e].elemId == trig->elmId)
        {
            return true;
        }
    }
    return false;
}

/* A group of >= 2 seats. This device may hold SEVERAL of them, watching different elements
 * and attributes, so every owned seat is matched and evaluated on its own — @p lowest is
 * only the lowest-numbered seat and cannot stand in for the rest. Note there is no
 * attribute/element pre-filter before this call for exactly that reason.
 *
 * A seat is delivered to the scoreboard even when its condition is FALSE: a falling edge is
 * what re-arms the group. */
static void evalCondGroup(uint16_t smid, const IOT_SmartTrigger_t *lowest,
                          dev_set_info_t *devSetInfo)
{
    uint8_t seats = SmartService_CondGroupOwnSeats(smid);
    if (seats == 0)
    {
        seats = (uint8_t) (1u << lowest->condIndex); // fall back to the seat already loaded
    }

    for (uint8_t seat = 0; seat < SMART_COND_GROUP_MAX_N; seat++)
    {
        if ((seats & (uint8_t) (1u << seat)) == 0)
        {
            continue;
        }

        IOT_SmartTrigger_t st;
        if (seat == lowest->condIndex)
        {
            st = *lowest; // already loaded — skip the re-read
        }
        else if (IOT_DaoSmartTriggerGetSeat(smid, seat, &st) != IOT_OK)
        {
            // The cached seat mask claims this seat exists but storage disagrees. Not fatal
            // (the seat simply contributes nothing) but it means cache and DAO have drifted.
            IOT_LOGW(TAG, "  SMID=%04x: seat %u in mask but not in storage", smid, seat);
            continue;
        }

        if (st.attrId != devSetInfo->attrId || !changedElemMatches(&st, devSetInfo))
        {
            continue; // this seat watches something else
        }

        bool condResult = checkTriggerCondition(st.condition, devSetInfo->attrValue,
                                                devSetInfo->attrSize, &st) &&
                          checkTriggerTimeWindow(&st);
        SmartService_CondGroupOnLocalResult(&st, condResult, devSetInfo->attrValue,
                                            devSetInfo->attrSize);
    }
}

/* Sync-on-off: this device mirrors ON/OFF across a LIST of its own elements and broadcasts
 * once to peers on the same SMID. Condition 3 ("IN") means "the changed element is in the
 * list", which is checked directly here — the value-based checkTriggerCondition would compare
 * an attrValue a sync-on-off trigger does not carry. */
static void evalSyncOnOff(uint16_t smid, const IOT_SmartTrigger_t *trig,
                          dev_set_info_t *devSetInfo)
{
    uint16_t bound[SMART_TRIGGER_ELM_CAP];
    uint8_t boundCount = syncOnOffBoundElements(trig, bound, SMART_TRIGGER_ELM_CAP);

    if (!changedElemInBoundSet(devSetInfo, bound, boundCount))
    {
        IOT_LOGI(TAG, "  SMID=%04x: sync-on-off, changed elem not in list", smid);
        return;
    }
    if (!checkTriggerTimeWindow(trig))
    {
        return;
    }

    // Broadcast once to peers on the same SMID.
    IOT_CoreSmartSyncOnOffReport_t report = {
        .smid = smid,
        .attrId = trig->attrId,
        .attrSize = devSetInfo->attrSize,
        .attrValue = devSetInfo->attrValue,
    };
    iot_err_t rptErr = IOT_CoreRequestSmartSyncOnOffReport(&report);
    if (rptErr == IOT_OK)
    {
        IOT_LOGI(TAG, "  SMID=%04x: sync-on-off broadcast sent", smid);
    }
    else
    {
        IOT_LOGE(TAG, "  SMID=%04x: sync-on-off broadcast failed: %d", smid, rptErr);
    }

    // Fan the value out to the OTHER bound elements so this device stays
    // internally consistent. Skip elements that just changed (the source).
    smart_control_timer_params_t sizeProbeTx;
    if (devSetInfo->attrSize <= sizeof(sizeProbeTx.attrValue))
    {
        for (uint8_t b = 0; b < boundCount; b++)
        {
            bool isSource = false;
            for (int e = 0; e < devSetInfo->numOfElems; e++)
            {
                if (devSetInfo->elemList[e].elemId == bound[b])
                {
                    isSource = true;
                    break;
                }
            }
            if (isSource)
            {
                continue; // this bound element is the source of the change
            }

            mirrorOnOffToElement(trig->smid, trig->eid, bound[b], trig->attrId, devSetInfo->attrValue,
                                 devSetInfo->attrSize, 0);
        }
    }
    else
    {
        IOT_LOGW(TAG, "  SMID=%04x: sync-on-off value size=%u exceeds buffer, no fan-out",
                 smid, devSetInfo->attrSize);
    }
    return;
}

iot_err_t SmartService_TriggerEvaluate(uint16_t eid, dev_set_info_t *devSetInfo)
{
    uint16_t *smids = NULL;
    uint16_t smidCount = 0;

    iot_err_t err = IOT_DaoSmartTriggerGetByEid(eid, &smids, &smidCount);
    if (err != IOT_OK || smids == NULL || smidCount == 0)
    {
        return IOT_OK; // No triggers for this device — not an error
    }

    IOT_LOGI(TAG, "Trigger eval: EID=%04x, attr=%04x, %u trigger(s) to check",
             eid, devSetInfo->attrId, smidCount);

    bool hasNotification = false;

    for (uint16_t i = 0; i < smidCount; i++)
    {
        IOT_SmartTrigger_t trig;
        if (IOT_DaoSmartTriggerGet(smids[i], &trig) != IOT_OK)
        {
            IOT_LOGW(TAG, "  SMID=%04x: failed to load trigger", smids[i]);
            continue;
        }

        // Skip disabled triggers
        if (trig.enabled == 0)
        {
            IOT_LOGI(TAG, "  SMID=%04x: disabled, skip", smids[i]);
            continue;
        }

        const trigger_kind_t kind = classifyTrigger(&trig);

        if (kind == TRIGGER_KIND_COND_GROUP)
        {
            // No attribute/element pre-filter here: this device may own several seats
            // watching different ones, so the match is done per seat inside.
            evalCondGroup(smids[i], &trig, devSetInfo);
            continue;
        }

        // Every remaining kind watches exactly ONE attribute, so filter on it now.
        if (trig.attrId != devSetInfo->attrId)
        {
            IOT_LOGI(TAG, "  SMID=%04x: attr mismatch (trigger=%04x, changed=%04x)",
                     smids[i], trig.attrId, devSetInfo->attrId);
            continue;
        }

        // SYNC-ON-OFF (subSmartType 71): this device mirrors ON/OFF across a LIST
        // of its own elements. condition 3 ("IN") means the changed element is in
        // elemList — checked directly here, so we do NOT run the value-based
        // checkTriggerCondition (COND_EXIST would compare attrValue, which a
        // sync-on-off trigger does not carry). On a match: broadcast ONCE to peers
        // on the same SMID, then fan the value out to our OTHER bound elements
        // locally (IOT_SENDER_AUTOMATION, loop-free). Sits before the single-elmId
        // match so it is not gated by it, and keeps the attr-sync (1024) path untouched.
        if (kind == TRIGGER_KIND_SYNC_ON_OFF)
        {
            evalSyncOnOff(smids[i], &trig, devSetInfo);
            continue;
        }

        // TRIGGER_KIND_ORDINARY from here down.
        if (!changedElemMatches(&trig, devSetInfo))
        {
            IOT_LOGI(TAG, "  SMID=%04x: element mismatch (trigger elm=%04x)", smids[i], trig.elmId);
            continue;
        }

        // Check condition
        if (!checkTriggerCondition(trig.condition, devSetInfo->attrValue, devSetInfo->attrSize, &trig))
        {
            IOT_LOGI(TAG, "  SMID=%04x: condition %u not met", smids[i], trig.condition);
            continue;
        }

        // Check time window
        if (!checkTriggerTimeWindow(&trig))
        {
            continue;
        }

        // Condition + time matched → dispatch based on smartType
        IOT_LOGI(TAG, "  SMID=%04x: MATCHED (smartType=%u)", smids[i], trig.smartType);

        if (fireTriggerBySubSmartType(&trig, devSetInfo->attrValue, devSetInfo->attrSize))
        {
            hasNotification = true;
        }
    }

    // Send ONE notification report if any SMART_SUB_TYPE_NOTIFICATION triggers matched.
    // This builds a separate report (the state report was already sent by the state-change
    // entry, IOT_ContractStateChanged, before scheduling this deferred evaluation).
    if (hasNotification)
    {
        sendSmartNotificationReport(eid, devSetInfo);
    }

    SAFE_FREE(smids);
    return IOT_OK;
}

// ============================================================================
// Sync-on-off apply (RX side of ACTIVE_BY_SYNC_ON_OFF, subSmartType 71)
// ============================================================================

iot_err_t SmartService_ApplySyncOnOff(uint16_t smid, uint16_t attrId,
                                      const uint8_t *attrValue, uint16_t attrSize,
                                      uint16_t senderEid)
{
    if (attrValue == NULL || attrSize == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    // Own broadcast echoed back — skip (same EID-based scheme as attr-sync).
    if (senderEid != 0)
    {
        uint16_t ownEid = 0;
        if (IOT_CoreGetRootEid(&ownEid) == IOT_OK && senderEid == ownEid)
        {
            IOT_LOGI(TAG, "SyncOnOff SMID=%04x: own broadcast echo (eid=%04x), skip", smid, senderEid);
            return IOT_OK;
        }
    }

    IOT_SmartTrigger_t trig;
    if (IOT_DaoSmartTriggerGet(smid, &trig) != IOT_OK)
    {
        return IOT_OK; // not a participant of this SMID — normal for a location-wide broadcast
    }

    if (trig.enabled == 0 || trig.subSmartType != SMART_SUB_TYPE_SYNC_ON_OFF || trig.attrId != attrId)
    {
        IOT_LOGI(TAG, "SyncOnOff SMID=%04x: no match (enabled=%u subType=%u attr=%04x/%04x), skip",
                 smid, trig.enabled, trig.subSmartType, trig.attrId, attrId);
        return IOT_OK;
    }

    smart_control_timer_params_t sizeProbe;
    if (attrSize > sizeof(sizeProbe.attrValue))
    {
        IOT_LOGW(TAG, "SyncOnOff SMID=%04x: attr=%04x size=%u exceeds buffer, skip apply", smid, attrId,
                 attrSize);
        return IOT_OK;
    }

    // Apply the value to EVERY bound element — all of this device's elements in
    // the group mirror together.
    uint16_t bound[SMART_TRIGGER_ELM_CAP];
    uint8_t boundCount = syncOnOffBoundElements(&trig, bound, SMART_TRIGGER_ELM_CAP);
    for (uint8_t b = 0; b < boundCount; b++)
    {
        IOT_LOGI(TAG, "SyncOnOff SMID=%04x: applying attr=%04x size=%u to EID=%04x elm=%04x", smid,
                 attrId, attrSize, trig.eid, bound[b]);
        mirrorOnOffToElement(smid, trig.eid, bound[b], attrId, attrValue, attrSize, 0);
    }
    return IOT_OK;
}

// ============================================================================
// Deferred trigger evaluation (non-blocking, runs on timer thread)
// ============================================================================

#define DEFERRED_TRIG_MAX_ELEMS 8
#define DEFERRED_TRIG_MAX_ATTR_SIZE 8

typedef struct
{
    uint16_t eid;
    uint16_t numOfElems;
    uint16_t attrId;
    uint16_t attrSize;
    elem_set_t elemList[DEFERRED_TRIG_MAX_ELEMS];
    uint8_t attrValue[DEFERRED_TRIG_MAX_ATTR_SIZE];
    // Heap fallback for oversized payloads
    elem_set_t *heapElemList;
    uint8_t *heapAttrValue;
} deferred_trigger_eval_t;

// Core's task. The work queue frees ctx itself afterwards; its buffers are freed here.
static void deferredTriggerEvalWork(void *userData)
{
    deferred_trigger_eval_t *ctx = (deferred_trigger_eval_t *)userData;
    if (ctx == NULL)
    {
        return;
    }

    dev_set_info_t devSetInfo = {
        .numOfElems = ctx->numOfElems,
        .eid = ctx->eid,
        .elemList = ctx->heapElemList ? ctx->heapElemList : ctx->elemList,
        .attrId = ctx->attrId,
        .attrSize = ctx->attrSize,
        .attrValue = ctx->heapAttrValue ? ctx->heapAttrValue : ctx->attrValue,
    };

    SmartService_TriggerEvaluate(ctx->eid, &devSetInfo);

    SAFE_FREE(ctx->heapElemList);
    SAFE_FREE(ctx->heapAttrValue);
}

static void freeDeferredEval(deferred_trigger_eval_t *ctx)
{
    SAFE_FREE(ctx->heapElemList);
    SAFE_FREE(ctx->heapAttrValue);
    SAFE_FREE(ctx);
}

// Hand the evaluation to core's task; a full work queue arms a short retry instead.
// Light enough for the timer task. @return false when the evaluation was dropped (ctx freed).
static bool queueDeferredEval(deferred_trigger_eval_t *ctx);

static void deferredTriggerEvalRetryCb(void *userData)
{
    (void) queueDeferredEval((deferred_trigger_eval_t *) userData);
}

static bool queueDeferredEval(deferred_trigger_eval_t *ctx)
{
    if (IOT_CoreDeferWork(deferredTriggerEvalWork, ctx) == IOT_OK)
    {
        return true; // the work queue owns and frees it now
    }
    if (IOT_TimerInsertCb(deferredTriggerEvalRetryCb, ctx, SMART_ACTION_RETRY_MS) == IOT_TIMER_HANDLE_INVALID)
    {
        IOT_LOGE(TAG, "Trigger evaluation for EID=%04x lost: core queue full and no timer free", ctx->eid);
        freeDeferredEval(ctx);
        return false;
    }
    IOT_LOGW(TAG, "Core work queue full: trigger evaluation for EID=%04x retried in %u ms", ctx->eid,
             (unsigned) SMART_ACTION_RETRY_MS);
    return true;
}

iot_err_t SmartService_TriggerEvaluateDeferred(uint16_t eid, dev_set_info_t *devSetInfo)
{
    if (devSetInfo == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    deferred_trigger_eval_t *ctx = (deferred_trigger_eval_t *)Mem_SafeCalloc(
        1, sizeof(deferred_trigger_eval_t), TAG, "deferred trig ctx");
    if (ctx == NULL)
    {
        return IOT_ERR_NO_MEM;
    }

    ctx->eid = eid;
    ctx->numOfElems = devSetInfo->numOfElems;
    ctx->attrId = devSetInfo->attrId;
    ctx->attrSize = devSetInfo->attrSize;

    // Deep-copy element list
    if (devSetInfo->numOfElems <= DEFERRED_TRIG_MAX_ELEMS)
    {
        Mem_SafeCopy(ctx->elemList, sizeof(ctx->elemList),
                     devSetInfo->elemList, devSetInfo->numOfElems * sizeof(elem_set_t));
    }
    else
    {
        ctx->heapElemList = (elem_set_t *)Mem_SafeMalloc(
            devSetInfo->numOfElems * sizeof(elem_set_t), TAG, "deferred elems");
        if (ctx->heapElemList == NULL)
        {
            SAFE_FREE(ctx);
            return IOT_ERR_NO_MEM;
        }
        Mem_SafeCopy(ctx->heapElemList, devSetInfo->numOfElems * sizeof(elem_set_t),
                     devSetInfo->elemList, devSetInfo->numOfElems * sizeof(elem_set_t));
    }

    // Deep-copy attribute value
    if (devSetInfo->attrSize <= DEFERRED_TRIG_MAX_ATTR_SIZE)
    {
        Mem_SafeCopy(ctx->attrValue, sizeof(ctx->attrValue),
                     devSetInfo->attrValue, devSetInfo->attrSize);
    }
    else
    {
        ctx->heapAttrValue = (uint8_t *)Mem_SafeMalloc(devSetInfo->attrSize, TAG, "deferred attr");
        if (ctx->heapAttrValue == NULL)
        {
            SAFE_FREE(ctx->heapElemList);
            SAFE_FREE(ctx);
            return IOT_ERR_NO_MEM;
        }
        Mem_SafeCopy(ctx->heapAttrValue, devSetInfo->attrSize,
                     devSetInfo->attrValue, devSetInfo->attrSize);
    }

    // On core's task, after the caller's work: never nested inside it, never on the timer task.
    if (!queueDeferredEval(ctx))
    {
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

// ============================================================================
// Schedule periodic tick (offline schedule execution)
// ============================================================================

static uint16_t s_lastMinuteOfWeek = IOT_INVALID_U16;
static bool s_schedulerRunning = false;

iot_err_t SmartService_GetCurrentUtcMinuteOfWeek(uint64_t unixMs, uint16_t *outMinuteOfWeek)
{
    if (outMinuteOfWeek == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    if (unixMs == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    // Schedule times are UTC0 minute-of-week — compare in UTC, not local TZ.
    time_t nowSec = (time_t)(unixMs / 1000);
    struct tm utcTime;
    if (gmtime_r(&nowSec, &utcTime) == NULL)
    {
        return IOT_ERR_FAIL;
    }

    uint16_t minuteOfDay = (uint16_t)(utcTime.tm_hour * MINUTES_PER_HOUR + utcTime.tm_min);
    *outMinuteOfWeek = (uint16_t)(utcTime.tm_wday * MINUTES_PER_DAY + minuteOfDay);
    return IOT_OK;
}

// Core's task: the tick's body. It reads the schedule store and runs every automation due
// this minute, which is far too much for the timer task's stack.
static void schedulerTickWork(void *arg)
{
    (void)arg;

    // Scheduled times are absolute minute-of-week values; comparing them against
    // a pre-SNTP 1970 clock would fire at the wrong time. Require a synced clock.
    if (!IOT_TimerIsTimeSynced())
    {
        return; // Time not synced yet
    }

    // Schedule times are UTC0 minute-of-week values (cloud frame), so compute the current minute-of-week in UTC.
    uint64_t nowMs = 0;
    iot_err_t err = IOT_TimerGetU64UnixTimeMs(&nowMs);
    if (err != IOT_OK || nowMs == 0)
    {
        return; // Time not available
    }

    uint16_t currentMOW = 0;
    err = SmartService_GetCurrentUtcMinuteOfWeek(nowMs, &currentMOW);
    if (err != IOT_OK)
    {
        return;
    }

    uint8_t dayOfWeek = (uint8_t)(currentMOW / MINUTES_PER_DAY);
    uint16_t minuteOfDay = (uint16_t)(currentMOW % MINUTES_PER_DAY);
    (void)dayOfWeek;
    (void)minuteOfDay;

    // Skip if same minute as last check
    if (currentMOW == s_lastMinuteOfWeek)
    {
        return;
    }
    s_lastMinuteOfWeek = currentMOW;

    // Load all schedules and fire matches
    uint16_t *smids = NULL;
    uint16_t smidCount = 0;
    uint16_t **timeLists = NULL;
    uint16_t *timeCounts = NULL;

    err = IOT_DaoSmartScheduleGetAll(&smids, &smidCount, &timeLists, &timeCounts);
    if (err != IOT_OK || smidCount == 0)
    {
        return; // No schedules stored
    }

    for (uint16_t i = 0; i < smidCount; i++)
    {
        for (uint16_t j = 0; j < timeCounts[i]; j++)
        {
            if (timeLists[i][j] == currentMOW)
            {
                if (!IOT_DaoSmartScheduleIsEnabled(smids[i]))
                {
                    IOT_LOGI(TAG, "Schedule tick: SMID=%04x matched MOW=%u but DISABLED, skip",
                             smids[i], currentMOW);
                    break;
                }
                IOT_LOGI(TAG, "Schedule tick: SMID=%04x matched UTC MOW=%u (day=%u, %02u:%02u)",
                         smids[i], currentMOW, dayOfWeek, minuteOfDay / 60, minuteOfDay % 60);
                /* Deduped against the cloud's ACTIVE_BY_SCHEDULE push for the same minute. */
                SmartService_ExecuteSchedule(smids[i], currentMOW);
                break; // One match per SMID is enough
            }
        }
    }

    // Cleanup
    for (uint16_t i = 0; i < smidCount; i++)
    {
        SAFE_FREE(timeLists[i]);
    }
    SAFE_FREE(smids);
    SAFE_FREE(timeLists);
    SAFE_FREE(timeCounts);
}

// Timer task (the tick, or a retry): hand the tick's body to core's task, or try again shortly.
// A tick that is late because of a retry is harmless: the body is deduped per minute.
static void schedulerTickHandOff(void *userData)
{
    (void)userData;
    if (IOT_CoreDeferWork(schedulerTickWork, NULL) == IOT_OK)
    {
        return;
    }
    if (IOT_TimerInsertCb(schedulerTickHandOff, NULL, SMART_ACTION_RETRY_MS) == IOT_TIMER_HANDLE_INVALID)
    {
        IOT_LOGE(TAG, "Schedule tick lost: core queue full and no timer free");
    }
}

// Timer task: re-arm the next tick (pseudo-periodic via single-shot timer) and hand this one over.
static void schedulerTickCb(void *userData)
{
    (void)userData;
    IOT_TimerInsertCb(schedulerTickCb, NULL, 60000);
    schedulerTickHandOff(NULL);
}

void SmartService_SchedulerStart(void)
{
    if (s_schedulerRunning)
    {
        return;
    }
    s_schedulerRunning = true;
    IOT_LOGI(TAG, "Starting smart scheduler (first tick in 5s, then 60s)");
    // Short initial delay so we catch any schedules missed during reboot.
    // schedulerTickCb re-registers itself at 60s for subsequent ticks.
    IOT_TimerInsertCb(schedulerTickCb, NULL, 5000);
}

// ============================================================================
// Enable/disable by SMID (the request does not say whether it is an automation or a schedule)
// ============================================================================

iot_err_t SmartService_SetEnabledBySmid(uint16_t smid, bool enabled)
{
    bool matched = false;

    /* Trigger DAO self-guards (Get-first); minuteDisable = 0 (permanent). */
    if (IOT_DaoSmartTriggerSetEnabled(smid, enabled, 0) == IOT_OK)
    {
        matched = true;
    }

    /* Schedule DAO writes unconditionally — gate on an existing schedule. */
    uint16_t *times = NULL;
    uint16_t timeCount = 0;
    if (IOT_DaoSmartScheduleGet(smid, &times, &timeCount) == IOT_OK && timeCount > 0)
    {
        IOT_DaoSmartScheduleSetEnabled(smid, enabled);
        matched = true;
    }
    SAFE_FREE(times);

    if (!matched)
    {
        IOT_LOGW(TAG, "SetEnabledBySmid: SMID=%04x not found in trigger or schedule DAO", smid);
        return IOT_ERR_NOT_FOUND;
    }

    IOT_LOGI(TAG, "SetEnabledBySmid: SMID=%04x enabled=%d", smid, enabled);
    return IOT_OK;
}

bool SmartService_ElementAcceptsMirror(uint16_t eid, uint16_t elemId)
{
    uint16_t *smids = NULL;
    uint16_t count = 0;

    if (IOT_DaoSmartTriggerGetByEid(eid, &smids, &count) != IOT_OK)
    {
        return false;
    }

    bool accepts = false;
    for (uint16_t i = 0; i < count && !accepts; i++)
    {
        IOT_SmartTrigger_t trig;
        if (IOT_DaoSmartTriggerGet(smids[i], &trig) != IOT_OK)
        {
            continue;
        }
        if (trig.enabled == 0)
        {
            continue;
        }
        if (trig.subSmartType != SMART_SUB_TYPE_STAIR_SWITCH &&
            trig.subSmartType != SMART_SUB_TYPE_SYNC_ON_OFF)
        {
            continue;
        }

        // Stair switch watches one element of its own; sync-on-off carries a list.
        if (trig.elmId == elemId)
        {
            accepts = true;
            break;
        }

        uint16_t bound[SMART_TRIGGER_ELM_CAP];
        uint8_t boundCount = syncOnOffBoundElements(&trig, bound, SMART_TRIGGER_ELM_CAP);
        for (uint8_t b = 0; b < boundCount; b++)
        {
            if (bound[b] == elemId)
            {
                accepts = true;
                break;
            }
        }
    }

    SAFE_FREE(smids);
    return accepts;
}

iot_err_t SmartService_SetEnabledAll(bool enabled)
{
    uint16_t *smids = NULL;
    uint16_t count = 0;

    iot_err_t err = IOT_DaoSmartTriggerGetAllSmids(&smids, &count);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "SetEnabledAll: cannot list automations: %d", err);
        return err;
    }

    uint16_t applied = 0;
    for (uint16_t i = 0; i < count; i++)
    {
        // A stale SMID in the list is not a failure of the sweep -- keep going, so one dead
        // entry cannot leave every other automation in the old state. SetEnabledBySmid
        // already returns NOT_FOUND for an SMID in neither DAO.
        if (SmartService_SetEnabledBySmid(smids[i], enabled) == IOT_OK)
        {
            applied++;
            // Same refresh the single-SMID path does: a cond-group ignores peer bits while
            // disabled, so its scoreboard mask is stale on re-enable.
            SmartService_CondGroupOnEnableChange(smids[i], enabled);
        }
    }
    SAFE_FREE(smids);

    IOT_LOGI(TAG, "SetEnabledAll: enabled=%d applied to %u of %u automation(s)", enabled,
             applied, count);
    // Zero automations is success, not failure: the master switch must not report an error
    // on an account that simply has none.
    return IOT_OK;
}

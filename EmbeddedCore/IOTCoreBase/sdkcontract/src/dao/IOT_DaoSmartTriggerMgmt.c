#include "dao/IOT_DaoSmartTriggerMgmt.h"
#include "services/IOT_SmartCondGroup.h" // SMART_COND_GROUP_MAX_N (shared seat cap)
#include "IOT_Common.h"
#include "IOT_ErrorManager.h"
#include "dao/IOT_Cached.h"
#include <inttypes.h>
#include <string.h>   // strncmp/strlen for seat-key prefix matching (no iotcommon equivalent)
#include "IOT_Log.h"
#include "dao/IOT_DaoStorage.h"

static const char *TAG = "DaoSmartTrig";

#define TRIG_NAMESPACE "smtrig"
#define TRIG_KEY_PREFIX "T"
#define INDEX_KEY_PREFIX "I"
#define TRIG_CACHE_MAX_ENTRIES 32

/* Trigger records are keyed per SEAT: one automation (SMID) may store SEVERAL conditions
 * on the same device — e.g. "temp > 30 AND humidity > 60" on one multi-sensor. Keying by
 * SMID alone made the second bind overwrite the first, leaving the group waiting forever
 * for a seat nobody owned. Seat index is 0..7 (the group bitmask is one byte). */
/* Seat count is shared with the scoreboard bitmask width — see IOT_SmartCondGroup.h. */
#define TRIG_MAX_SEATS SMART_COND_GROUP_MAX_N
#define TRIG_KEY_LEN   8 /* "T" + 4 hex smid + 2 hex seat + NUL */

static void makeTriggerKey(uint16_t smid, uint8_t seat, char *out, size_t outSize)
{
    snprintf(out, outSize, TRIG_KEY_PREFIX "%04x%02x", smid, seat);
}

static void makeIndexKey(uint16_t eid, char *out, size_t outSize)
{
    snprintf(out, outSize, INDEX_KEY_PREFIX "%04x", eid);
}

// ============================================================================
// Initialization
// ============================================================================

iot_err_t IOT_DaoSmartTriggerInit(void)
{
    const IOT_CacheConfig_t cfg = {
        .nsName = TRIG_NAMESPACE,
        .maxEntries = TRIG_CACHE_MAX_ENTRIES,
        .flushIntervalMs = 0,
        .policy = IOT_CACHE_WRITE_THROUGH,
    };

    iot_err_t err = IOT_CacheRegisterNamespace(&cfg);
    if (err != IOT_OK && err != IOT_ERR_INVALID_STATE)
    {
        IOT_LOGE(TAG, "Failed to register trigger cache namespace");
        return IOT_ERR_FAIL;
    }

    IOT_LOGI(TAG, "Smart trigger DAO initialized");
    return IOT_OK;
}

// ============================================================================
// EID Index helpers
// ============================================================================

/**
 * Add smid to the EID index array. If the smid already exists, do nothing.
 */
static iot_err_t indexAddSmid(uint16_t eid, uint16_t smid)
{
    char indexKey[6]; // "I" + 4 hex + null
    makeIndexKey(eid, indexKey, sizeof(indexKey));

    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_CacheGet(TRIG_NAMESPACE, indexKey, IOT_DAO_BLOB, &data, &dataLen);

    if (err == IOT_OK && data != NULL)
    {
        // Check if smid already in list
        uint16_t count = dataLen / sizeof(uint16_t);
        uint16_t *list = (uint16_t *)data;
        for (uint16_t i = 0; i < count; i++)
        {
            if (list[i] == smid)
            {
                SAFE_FREE(data);
                return IOT_OK; // Already exists
            }
        }

        // Append smid
        uint16_t newLen = dataLen + sizeof(uint16_t);
        uint16_t *newList = (uint16_t *)Mem_SafeRealloc(data, newLen, TAG, "index append");
        if (newList == NULL)
        {
            SAFE_FREE(data);
            return IOT_ERR_NO_MEM;
        }
        newList[count] = smid;
        err = IOT_CacheSet(TRIG_NAMESPACE, indexKey, IOT_DAO_BLOB, newList, newLen);
        SAFE_FREE(newList);
    }
    else
    {
        // First entry for this EID
        SAFE_FREE(data);
        err = IOT_CacheSet(TRIG_NAMESPACE, indexKey, IOT_DAO_BLOB, &smid, sizeof(uint16_t));
    }

    return err;
}

/**
 * Remove smid from the EID index array. If the array becomes empty, remove the key.
 */
static iot_err_t indexRemoveSmid(uint16_t eid, uint16_t smid)
{
    char indexKey[6];
    makeIndexKey(eid, indexKey, sizeof(indexKey));

    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_CacheGet(TRIG_NAMESPACE, indexKey, IOT_DAO_BLOB, &data, &dataLen);

    if (err != IOT_OK || data == NULL)
    {
        SAFE_FREE(data);
        return IOT_OK; // Nothing to remove
    }

    uint16_t count = dataLen / sizeof(uint16_t);
    uint16_t *list = (uint16_t *)data;

    // Find and remove
    int foundIdx = -1;
    for (uint16_t i = 0; i < count; i++)
    {
        if (list[i] == smid)
        {
            foundIdx = i;
            break;
        }
    }

    if (foundIdx < 0)
    {
        SAFE_FREE(data);
        return IOT_OK; // Not in list
    }

    if (count == 1)
    {
        // Last entry — remove the index key entirely
        SAFE_FREE(data);
        IOT_CacheDelete(TRIG_NAMESPACE, indexKey);
        return IOT_OK;
    }

    // Shift remaining entries
    for (uint16_t i = (uint16_t)foundIdx; i < count - 1; i++)
    {
        list[i] = list[i + 1];
    }

    uint16_t newLen = (count - 1) * sizeof(uint16_t);
    err = IOT_CacheSet(TRIG_NAMESPACE, indexKey, IOT_DAO_BLOB, list, newLen);
    SAFE_FREE(data);

    return err;
}

// ============================================================================
// Public API
// ============================================================================

iot_err_t IOT_DaoSmartTriggerSet(uint16_t smid, const IOT_SmartTrigger_t *trigger)
{
    if (trigger == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char trigKey[TRIG_KEY_LEN];
    makeTriggerKey(smid, trigger->condIndex, trigKey, sizeof(trigKey));

    // Check if trigger already exists (may need to update old EID index)
    IOT_SmartTrigger_t existing;
    iot_err_t existErr = IOT_DaoSmartTriggerGet(smid, &existing);
    if (existErr == IOT_OK && existing.eid != trigger->eid)
    {
        // EID changed — remove from old index
        indexRemoveSmid(existing.eid, smid);
    }

    // Store trigger blob
    iot_err_t err = IOT_CacheSet(TRIG_NAMESPACE, trigKey, IOT_DAO_BLOB,
                                 (void *)trigger, sizeof(IOT_SmartTrigger_t));
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to store trigger SMID=%04x", smid);
        return IOT_ERR_FAIL;
    }

    // Update EID index
    iot_err_t idxErr = indexAddSmid(trigger->eid, smid);
    if (idxErr != IOT_OK)
    {
        IOT_LOGW(TAG, "Failed to update EID index for SMID=%04x EID=%04x", smid, trigger->eid);
    }

    // utc=isDeviceUtc selects the time window reference frame (1=UTC0, 0=local).
    // timeZoneQuarterHours is reserved/unused (UTC eval needs no offset).
    IOT_LOGI(TAG,
             "Stored trigger SMID=%04x EID=%04x (cond=%u, attr=%04x, timeStart=%u, timeStop=%u, weekDay=0x%02x, "
             "timCfg=%u/%u, utc=%u)",
             smid, trigger->eid, trigger->condition, trigger->attrId, trigger->timeStart,
             trigger->timeStop, trigger->weekDay, trigger->timCfgType, trigger->timCfgValue,
             trigger->isDeviceUtc);

    // What was actually PERSISTED, so it can be diffed against the version/N/K/idx logged
    // when core delivered the bind — a mismatch means the bind path changed the seat, not
    // the sender. condCount==0 is a legacy / non-cond-group record, so stay quiet for those.
    if (trigger->condCount > 0)
    {
        IOT_LOGI(TAG, "  cond-group stored: version=%u N=%u K=%u condIndex=%u",
                 trigger->condVersion, trigger->condCount, trigger->fireCount,
                 trigger->condIndex);
    }
    return IOT_OK;
}

iot_err_t IOT_DaoSmartTriggerGetSeat(uint16_t smid, uint8_t seat, IOT_SmartTrigger_t *outTrigger)
{
    if (outTrigger == NULL || seat >= TRIG_MAX_SEATS)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char trigKey[TRIG_KEY_LEN];
    makeTriggerKey(smid, seat, trigKey, sizeof(trigKey));

    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_CacheGet(TRIG_NAMESPACE, trigKey, IOT_DAO_BLOB, &data, &dataLen);

    if (err != IOT_OK || data == NULL)
    {
        SAFE_FREE(data);
        return (err == IOT_ERR_NOT_FOUND) ? IOT_ERR_NOT_FOUND : IOT_ERR_FAIL;
    }

    if (dataLen > sizeof(IOT_SmartTrigger_t))
    {
        IOT_LOGW(TAG, "Trigger SMID=%04x size too large: %u > %u",
                 smid, dataLen, (uint16_t)sizeof(IOT_SmartTrigger_t));
        SAFE_FREE(data);
        return IOT_ERR_FAIL;
    }

    // Zero-init so new fields (e.g. weekDay) default to 0 for old/smaller blobs
    memset(outTrigger, 0, sizeof(IOT_SmartTrigger_t));
    Mem_SafeCopy(outTrigger, sizeof(IOT_SmartTrigger_t), data, dataLen);
    SAFE_FREE(data);
    return IOT_OK;
}

/* Seat-agnostic read: returns the LOWEST stored seat. Most callers only want the
 * automation's metadata (eid / cfm / enabled / seat count), which every seat carries
 * identically — so they need no change. Callers that must evaluate a specific
 * condition use IOT_DaoSmartTriggerGetSeat(). */
iot_err_t IOT_DaoSmartTriggerGet(uint16_t smid, IOT_SmartTrigger_t *outTrigger)
{
    if (outTrigger == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    for (uint8_t seat = 0; seat < TRIG_MAX_SEATS; seat++)
    {
        iot_err_t err = IOT_DaoSmartTriggerGetSeat(smid, seat, outTrigger);
        if (err == IOT_OK)
        {
            return IOT_OK;
        }
        /* Only "this seat is absent" may be skipped. A corrupt or oversized blob must
         * surface as-is — swallowing it into NOT_FOUND would hide the downgrade hazard
         * (a record written by newer firmware than this build understands). */
        if (err != IOT_ERR_NOT_FOUND)
        {
            return err;
        }
    }
    return IOT_ERR_NOT_FOUND;
}

/* Which seats of this SMID does this device hold? Probing is why the scoreboard caches
 * the result in its entry: the trigger-evaluation path runs on every state change and
 * must not pay 8 lookups each time. */
iot_err_t IOT_DaoSmartTriggerGetSeatMask(uint16_t smid, uint8_t *outMask)
{
    if (outMask == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    *outMask = 0;

    /* Read the lowest seat first to learn the automation's seat count, then probe only
     * 0..N-1. Two reasons: a seat at or above N is not a member of this generation (it is
     * a leftover from a larger one) and must not become a phantom bit; and each probe miss
     * logs a WARN from the platform DAO layer, so an unbounded 0..7 walk floods the log
     * and does 8 lookups where N will do. */
    IOT_SmartTrigger_t scratch;
    if (IOT_DaoSmartTriggerGet(smid, &scratch) != IOT_OK)
    {
        return IOT_ERR_NOT_FOUND;
    }
    uint8_t seatCount = scratch.condCount;
    if (seatCount == 0 || seatCount > TRIG_MAX_SEATS)
    {
        seatCount = TRIG_MAX_SEATS;
    }

    for (uint8_t seat = 0; seat < seatCount; seat++)
    {
        if (IOT_DaoSmartTriggerGetSeat(smid, seat, &scratch) == IOT_OK)
        {
            *outMask |= (uint8_t) (1u << seat);
        }
    }
    return (*outMask != 0) ? IOT_OK : IOT_ERR_NOT_FOUND;
}

/* Delete the seats of `smid` that appear in an already-fetched key list.
 *
 * Every IOT_CacheDelete is a semaphore + queue round-trip to the DAO task, so deleting
 * seats blindly would cost 8 round-trips per SMID no matter how many seats exist — 7 of
 * them wasted for the common single-seat trigger, on the cloud-message handling path. Matching against
 * the key list keeps the "remove EVERY seat" guarantee (a leftover seat would keep
 * contributing a bit to a group that no longer exists) including SPARSE seats, while
 * paying only for seats that are really there. */
static void removeSeatsFromKeyList(uint16_t smid, char **keyList, uint16_t keyCount)
{
    char prefix[TRIG_KEY_LEN];
    snprintf(prefix, sizeof(prefix), TRIG_KEY_PREFIX "%04x", smid);
    size_t prefixLen = strlen(prefix);

    for (uint16_t i = 0; i < keyCount; i++)
    {
        // Prefix is "T" + the full 4-hex SMID, so it can only match that SMID's seat keys;
        // the eid index uses a different prefix ("I") and cannot collide.
        if (keyList[i] != NULL && strncmp(keyList[i], prefix, prefixLen) == 0)
        {
            IOT_CacheDelete(TRIG_NAMESPACE, keyList[i]);
        }
    }
}

/* Blind sweep of the whole seat space — the fallback when the key list is unavailable.
 * Costs TRIG_MAX_SEATS round-trips but can never leave an orphan seat behind. */
static void removeAllSeatsBlind(uint16_t smid)
{
    for (uint8_t seat = 0; seat < TRIG_MAX_SEATS; seat++)
    {
        char trigKey[TRIG_KEY_LEN];
        makeTriggerKey(smid, seat, trigKey, sizeof(trigKey));
        IOT_CacheDelete(TRIG_NAMESPACE, trigKey);
    }
}

static void removeAllSeats(uint16_t smid)
{
    char **keyList = NULL;
    uint16_t keyCount = 0;

    if (IOT_DaoStorageGetKeyList(TRIG_NAMESPACE, &keyList, &keyCount) != IOT_OK || keyList == NULL)
    {
        removeAllSeatsBlind(smid);   // never trade completeness for the optimisation
        return;
    }

    removeSeatsFromKeyList(smid, keyList, keyCount);

    for (uint16_t i = 0; i < keyCount; i++)
    {
        SAFE_FREE(keyList[i]);
    }
    SAFE_FREE(keyList);
}

iot_err_t IOT_DaoSmartTriggerRemove(uint16_t smid)
{
    // Read trigger first to get EID for index cleanup
    IOT_SmartTrigger_t existing;
    iot_err_t getErr = IOT_DaoSmartTriggerGet(smid, &existing);

    removeAllSeats(smid);

    // Update EID index
    if (getErr == IOT_OK)
    {
        indexRemoveSmid(existing.eid, smid);
    }

    IOT_LOGI(TAG, "Removed trigger SMID=%04x", smid);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartTriggerGetByEid(uint16_t eid, uint16_t **outSmids, uint16_t *outCount)
{
    if (outSmids == NULL || outCount == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char indexKey[6];
    makeIndexKey(eid, indexKey, sizeof(indexKey));

    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_CacheGet(TRIG_NAMESPACE, indexKey, IOT_DAO_BLOB, &data, &dataLen);

    if (err != IOT_OK || data == NULL || dataLen == 0)
    {
        SAFE_FREE(data);
        *outSmids = NULL;
        *outCount = 0;
        return IOT_ERR_NOT_FOUND;
    }

    *outCount = dataLen / sizeof(uint16_t);
    *outSmids = (uint16_t *)data; // Caller must free
    return IOT_OK;
}

iot_err_t IOT_DaoSmartTriggerSetEnabled(uint16_t smid, bool enabled, uint16_t minuteDisable)
{
    /* enabled/minuteDisable are automation-wide, so every seat must agree — otherwise a
     * disabled automation could still contribute a bit through one of its other seats. */
    bool any = false;
    for (uint8_t seat = 0; seat < TRIG_MAX_SEATS; seat++)
    {
        IOT_SmartTrigger_t trigger;
        if (IOT_DaoSmartTriggerGetSeat(smid, seat, &trigger) != IOT_OK)
        {
            continue;
        }
        trigger.enabled = enabled ? 1 : 0;
        trigger.minuteDisable = minuteDisable;

        char trigKey[TRIG_KEY_LEN];
        makeTriggerKey(smid, seat, trigKey, sizeof(trigKey));
        if (IOT_CacheSet(TRIG_NAMESPACE, trigKey, IOT_DAO_BLOB,
                         &trigger, sizeof(IOT_SmartTrigger_t)) != IOT_OK)
        {
            return IOT_ERR_FAIL;
        }
        any = true;
    }
    if (!any)
    {
        return IOT_ERR_NOT_FOUND;
    }

    IOT_LOGI(TAG, "Trigger SMID=%04x enabled=%d minuteDisable=%u", smid, enabled, minuteDisable);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartTriggerSetEnabledByEid(uint16_t eid, bool enabled)
{
    uint16_t *smids = NULL;
    uint16_t count = 0;
    iot_err_t err = IOT_DaoSmartTriggerGetByEid(eid, &smids, &count);
    if (err != IOT_OK)
    {
        return IOT_OK; // No triggers for this EID — not an error
    }

    for (uint16_t i = 0; i < count; i++)
    {
        IOT_DaoSmartTriggerSetEnabled(smids[i], enabled, 0);
    }

    SAFE_FREE(smids);
    IOT_LOGI(TAG, "Set %u triggers for EID=%04x enabled=%d", count, eid, enabled);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartTriggerRemoveByEid(uint16_t eid)
{
    uint16_t *smids = NULL;
    uint16_t count = 0;
    iot_err_t err = IOT_DaoSmartTriggerGetByEid(eid, &smids, &count);
    if (err != IOT_OK)
    {
        return IOT_OK; // No triggers for this EID
    }

    // One key-list read covers ALL of this EID's SMIDs (skip index update — the whole
    // index goes below). Blind-sweeping here cost 8 round-trips per SMID.
    char **keyList = NULL;
    uint16_t keyCount = 0;
    bool haveKeys = (IOT_DaoStorageGetKeyList(TRIG_NAMESPACE, &keyList, &keyCount) == IOT_OK
                     && keyList != NULL);

    for (uint16_t i = 0; i < count; i++)
    {
        if (haveKeys)
        {
            removeSeatsFromKeyList(smids[i], keyList, keyCount);
        }
        else
        {
            removeAllSeatsBlind(smids[i]);
        }
    }

    if (haveKeys)
    {
        for (uint16_t i = 0; i < keyCount; i++)
        {
            SAFE_FREE(keyList[i]);
        }
        SAFE_FREE(keyList);
    }

    // Remove the EID index key
    char indexKey[6];
    makeIndexKey(eid, indexKey, sizeof(indexKey));
    IOT_CacheDelete(TRIG_NAMESPACE, indexKey);

    SAFE_FREE(smids);
    IOT_LOGI(TAG, "Removed %u triggers for EID=%04x", count, eid);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartTriggerFactoryReset(uint16_t **outSmids, uint16_t *outCount)
{
    if (outSmids != NULL)
    {
        *outSmids = NULL;
    }
    if (outCount != NULL)
    {
        *outCount = 0;
    }

    // Get all keys in the trigger namespace
    char **keyList = NULL;
    uint16_t keyCount = 0;
    iot_err_t err = IOT_DaoStorageGetKeyList(TRIG_NAMESPACE, &keyList, &keyCount);
    if (err != IOT_OK || keyCount == 0)
    {
        // Nothing to clean — invalidate cache just in case
        IOT_CacheInvalidate(TRIG_NAMESPACE, NULL);
        IOT_LOGI(TAG, "Factory reset: no triggers to remove");
        return IOT_OK;
    }

    // Collect unique SMIDs from trigger keys (T{smid})
    uint16_t *smids = NULL;
    uint16_t smidCount = 0;
    if (outSmids != NULL)
    {
        smids = (uint16_t *) Mem_SafeMalloc(keyCount * sizeof(uint16_t), TAG, "smids");
    }

    for (uint16_t i = 0; i < keyCount; i++)
    {
        /* Key layout is "T" + 4 hex SMID + 2 hex seat. Parsing the whole tail as one hex
         * number folded the seat digits into the value — "T007a00" read as 0x007A00, which
         * truncates to 0x7A00 — so the caller wiped bind-cmd namespaces that never existed
         * and left the real ones behind. Take exactly the 4 SMID digits.
         * (Index keys start with INDEX_KEY_PREFIX and are skipped by the prefix test.) */
        if (keyList[i] != NULL && keyList[i][0] == TRIG_KEY_PREFIX[0] && smids != NULL &&
            strlen(keyList[i]) >= 5)
        {
            char smidHex[5] = { keyList[i][1], keyList[i][2], keyList[i][3], keyList[i][4], '\0' };
            uint16_t smid = (uint16_t) strtol(smidHex, NULL, 16);

            /* Several seats of one SMID share ONE bind-cmd namespace — report it once, or
             * the caller removes the same namespace per seat and over-counts the result. */
            bool alreadyListed = false;
            for (uint16_t j = 0; j < smidCount; j++)
            {
                if (smids[j] == smid)
                {
                    alreadyListed = true;
                    break;
                }
            }
            if (!alreadyListed)
            {
                smids[smidCount++] = smid;
            }
        }
        SAFE_FREE(keyList[i]);
    }
    SAFE_FREE(keyList);

    // Wipe the entire namespace + cache
    IOT_CacheInvalidate(TRIG_NAMESPACE, NULL);
    IOT_DaoStorageRemoveNamespace(TRIG_NAMESPACE);

    if (outSmids != NULL && smidCount > 0)
    {
        *outSmids = smids;
    }
    else
    {
        SAFE_FREE(smids);
    }
    if (outCount != NULL)
    {
        *outCount = smidCount;
    }

    IOT_LOGI(TAG, "Factory reset: removed %u triggers", smidCount);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartTriggerGetAllSmids(uint16_t **outSmids, uint16_t *outCount)
{
    if (outSmids == NULL || outCount == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    *outSmids = NULL;
    *outCount = 0;

    char **keyList = NULL;
    uint16_t keyCount = 0;
    iot_err_t err = IOT_DaoStorageGetKeyList(TRIG_NAMESPACE, &keyList, &keyCount);
    if (err != IOT_OK || keyCount == 0)
    {
        // Empty namespace is a valid, non-error "nothing to sync" result.
        return IOT_OK;
    }

    // Worst case every key is a trigger key.
    uint16_t *smids = (uint16_t *) Mem_SafeMalloc(keyCount * sizeof(uint16_t), TAG, "all smids");
    uint16_t smidCount = 0;

    for (uint16_t i = 0; i < keyCount; i++)
    {
        if (keyList[i] != NULL && keyList[i][0] == 'T' && smids != NULL)
        {
            /* Key is T{smid:4}{seat:2}; take the smid half only, and de-duplicate —
             * a multi-seat automation contributes one key per seat. */
            char smidHex[5] = {0};
            Mem_SafeCopy(smidHex, sizeof(smidHex), &keyList[i][1], 4);
            uint16_t smid = (uint16_t) strtol(smidHex, NULL, 16);

            bool seen = false;
            for (uint16_t j = 0; j < smidCount; j++)
            {
                if (smids[j] == smid)
                {
                    seen = true;
                    break;
                }
            }
            if (seen)
            {
                continue;
            }
            smids[smidCount++] = smid;
        }
        SAFE_FREE(keyList[i]);
    }
    SAFE_FREE(keyList);

    if (smids == NULL)
    {
        return IOT_ERR_NO_MEM;
    }

    if (smidCount > 0)
    {
        *outSmids = smids;
        *outCount = smidCount;
    }
    else
    {
        // Only index keys present — no triggers.
        SAFE_FREE(smids);
    }
    return IOT_OK;
}

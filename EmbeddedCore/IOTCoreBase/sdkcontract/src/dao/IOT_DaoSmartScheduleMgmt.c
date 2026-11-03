#include "dao/IOT_DaoSmartScheduleMgmt.h"
#include "IOT_Common.h"
#include "IOT_ErrorManager.h"
#include "dao/IOT_Cached.h"
#include <ctype.h>
#include <inttypes.h>
#include <string.h>
#include "IOT_Log.h"
#include "dao/IOT_DaoStorage.h"

static const char *TAG = "DaoSmartSched";

#define SCHED_NAMESPACE "sched"
#define SCHED_CACHE_MAX_ENTRIES 16

static void makeKey(uint16_t smid, char *out, size_t outSize)
{
    snprintf(out, outSize, "%04x", smid);
}

static void makeEnabledKey(uint16_t smid, char *out, size_t outSize)
{
    snprintf(out, outSize, "e%04x", smid);
}

// ============================================================================
// Stored-format version
//
// One older writer of this store saved due minutes byte-swapped. Such a minute can still
// fall inside 0..10079, and the offline tick would then fire it at the wrong time until the
// schedule is next replaced wholesale. Not every stored entry came from that writer, so the
// store must not be blanket-swapped or blanket-dropped; nothing records which writer
// produced which entry.
//
// A minute-of-week is 0..10079 by definition, which makes most of the value space
// decidable per entry, with s = swap16(v):
//
//   v > 10079, s <= 10079   v is no minute at all, so it was stored swapped     -> SWAP
//   v <= 10079, s > 10079   a swapped store always yields a v whose swap is a
//                           legal minute, so this one was stored correctly      -> KEEP
//   both legal              undecidable                                         -> DROP
//   neither legal           garbage                                             -> DROP
//
// A dropped entry is restored the first time that slot comes round with the device online,
// because ACTIVE_BY_SCHEDULE appends the due minute. A wrong GUESS would fire the
// automation at the wrong time, so undecidable entries are dropped rather than guessed.
//
// The user's enable/disable choice is kept, and a schedule update clears only the times
// (IOT_DaoSmartScheduleClearTimes) rather than the whole SMID, so the flag also survives
// the update that replaces the schedule.
//
// The marker is a short non-SMID key in this same namespace. Firmware that does not know
// it reads it as a 1-byte blob, i.e. a schedule with zero times, so a downgrade is harmless.
// ============================================================================
#define SCHED_FORMAT_KEY     "fmtv"
#define SCHED_FORMAT_VERSION 1

/* Minute-of-week is day*1440 + hour*60 + minute, so 0..10079 inclusive. */
#define SCHED_MINUTES_PER_WEEK 10080u

/* A schedule time-list key is exactly 4 hex chars ("%04x"); enabled flags are "e"+4 and
 * the format marker is "fmtv". */
static bool isScheduleTimesKey(const char *key)
{
    if (key == NULL || strlen(key) != 4)
    {
        return false;
    }
    for (int i = 0; i < 4; i++)
    {
        if (!isxdigit((unsigned char) key[i]))
        {
            return false;
        }
    }
    return true;
}

/* Outcome of classifying one stored 16-bit entry. */
typedef enum
{
    SCHED_ENTRY_KEEP,
    SCHED_ENTRY_SWAP,
    SCHED_ENTRY_DROP,
} SchedEntryVerdict_t;

static SchedEntryVerdict_t classifyStoredMinute(uint16_t v, uint16_t *outCorrected)
{
    uint16_t swapped = (uint16_t) ((v << 8) | (v >> 8));
    bool vLegal = (v < SCHED_MINUTES_PER_WEEK);
    bool sLegal = (swapped < SCHED_MINUTES_PER_WEEK);

    if (vLegal && sLegal)
    {
        return SCHED_ENTRY_DROP; // undecidable — could be either way round
    }
    if (!vLegal && sLegal)
    {
        *outCorrected = swapped; // only a swapped store can have produced this
        return SCHED_ENTRY_SWAP;
    }
    if (vLegal)
    {
        *outCorrected = v; // a swapped store could not have produced it
        return SCHED_ENTRY_KEEP;
    }
    return SCHED_ENTRY_DROP; // no minute either way
}

/* Rewrite one stored time list in place, entry by entry. Returns the number of entries in
 * each class, so the caller can report the outcome of the whole namespace. */
static void migrateOneTimeList(const char *key, const uint8_t *blob, uint16_t blobLen,
                               uint16_t *swapped, uint16_t *kept, uint16_t *dropped)
{
    uint16_t count = blobLen / 2;
    uint16_t survivors = 0;
    uint8_t *out = NULL;

    if (blobLen % 2 != 0)
    {
        (*dropped)++; // trailing half entry — not a minute
    }

    if (count > 0)
    {
        out = (uint8_t *) Mem_SafeMalloc((size_t) count * 2, TAG, "sched migrate");
        if (out == NULL)
        {
            IOT_LOGE(TAG, "Migration: out of memory for key %s, leaving it untouched", key);
            return;
        }
    }

    for (uint16_t i = 0; i < count; i++)
    {
        uint16_t v = BYTES_TO_U16_BE(blob[i * 2], blob[i * 2 + 1]);
        uint16_t corrected = 0;

        switch (classifyStoredMinute(v, &corrected))
        {
        case SCHED_ENTRY_SWAP:
            (*swapped)++;
            break;
        case SCHED_ENTRY_KEEP:
            (*kept)++;
            break;
        default:
            (*dropped)++;
            continue;
        }

        out[survivors * 2]     = HIGH_BYTE(corrected);
        out[survivors * 2 + 1] = LOW_BYTE(corrected);
        survivors++;
    }

    if (survivors == 0)
    {
        /* A zero-length blob would resurface in GetAll as a phantom schedule. */
        IOT_CacheDelete(SCHED_NAMESPACE, key);
    }
    else
    {
        IOT_CacheSet(SCHED_NAMESPACE, key, IOT_DAO_BLOB, out, (uint16_t) (survivors * 2));
    }

    SAFE_FREE(out);
}

static void migrateStoredFormat(void)
{
    void *marker = NULL;
    uint16_t markerLen = 0;
    if (IOT_DaoStorageGet(SCHED_NAMESPACE, SCHED_FORMAT_KEY, IOT_DAO_BLOB, &marker, &markerLen) == IOT_OK
        && marker != NULL && markerLen >= 1 && ((uint8_t *) marker)[0] >= SCHED_FORMAT_VERSION)
    {
        SAFE_FREE(marker);
        return; // already current
    }
    SAFE_FREE(marker);

    char **keyList = NULL;
    uint16_t keyCount = 0;
    void **dataList = NULL;
    uint16_t *dataSizes = NULL;
    uint16_t swapped = 0;
    uint16_t kept = 0;
    uint16_t dropped = 0;
    uint16_t lists = 0;

    if (IOT_DaoStorageGetNamespaceData(SCHED_NAMESPACE, &keyList, &keyCount, &dataList, &dataSizes) == IOT_OK)
    {
        for (uint16_t i = 0; i < keyCount; i++)
        {
            if (isScheduleTimesKey(keyList[i]) && dataList[i] != NULL)
            {
                migrateOneTimeList(keyList[i], (const uint8_t *) dataList[i], dataSizes[i],
                                   &swapped, &kept, &dropped);
                lists++;
            }
            SAFE_FREE(keyList[i]);
            SAFE_FREE(dataList[i]);
        }
        SAFE_FREE(keyList);
        SAFE_FREE(dataList);
        SAFE_FREE(dataSizes);
    }

    if (lists > 0)
    {
        /* WARN so the one-time outcome is visible at the default log level. */
        IOT_LOGW(TAG, "Schedule migration: %u list(s), %u swapped, %u kept, %u dropped",
                 lists, swapped, kept, dropped);
    }

    uint8_t version = SCHED_FORMAT_VERSION;
    if (IOT_CacheSet(SCHED_NAMESPACE, SCHED_FORMAT_KEY, IOT_DAO_BLOB, &version, 1) != IOT_OK)
    {
        /* The migration will run again on the next boot. It is idempotent on everything it
         * can decide (a corrected minute classifies as KEEP the second time round), but an
         * undecidable entry the cloud has re-pushed since would be dropped again — and a
         * store that cannot take a 1-byte blob is a fault in its own right. */
        IOT_LOGE(TAG, "Failed to store schedule format marker; migration will re-run on next boot");
    }
}

// ============================================================================
// Initialization
// ============================================================================

iot_err_t IOT_DaoSmartScheduleInit(void)
{
    const IOT_CacheConfig_t cfg = {
        .nsName = SCHED_NAMESPACE,
        .maxEntries = SCHED_CACHE_MAX_ENTRIES,
        .flushIntervalMs = 0,
        .policy = IOT_CACHE_WRITE_THROUGH,
    };

    iot_err_t err = IOT_CacheRegisterNamespace(&cfg);
    if (err != IOT_OK && err != IOT_ERR_INVALID_STATE)
    {
        IOT_LOGE(TAG, "Failed to register schedule cache namespace");
        return IOT_ERR_FAIL;
    }

    migrateStoredFormat();

    IOT_LOGI(TAG, "Smart schedule DAO initialized");
    return IOT_OK;
}

// ============================================================================
// Public API
// ============================================================================

iot_err_t IOT_DaoSmartScheduleSet(uint16_t smid, uint16_t minuteOfWeek)
{
    char key[6]; // 4 hex + null
    makeKey(smid, key, sizeof(key));

    // Read existing blob
    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_CacheGet(SCHED_NAMESPACE, key, IOT_DAO_BLOB, &data, &dataLen);

    if (err == IOT_OK && data != NULL && dataLen >= 2)
    {
        // Check for duplicate
        uint16_t count = dataLen / 2;
        uint8_t *bytes = (uint8_t *)data;
        for (uint16_t i = 0; i < count; i++)
        {
            uint16_t existing = BYTES_TO_U16_BE(bytes[i * 2], bytes[i * 2 + 1]);
            if (existing == minuteOfWeek)
            {
                SAFE_FREE(data);
                return IOT_OK; // Already exists
            }
        }

        // Append new time
        uint16_t newLen = dataLen + 2;
        uint8_t *newData = (uint8_t *)Mem_SafeRealloc(data, newLen, TAG, "sched append");
        if (newData == NULL)
        {
            SAFE_FREE(data);
            return IOT_ERR_NO_MEM;
        }
        newData[dataLen] = HIGH_BYTE(minuteOfWeek);
        newData[dataLen + 1] = LOW_BYTE(minuteOfWeek);

        err = IOT_CacheSet(SCHED_NAMESPACE, key, IOT_DAO_BLOB, newData, newLen);
        SAFE_FREE(newData);
    }
    else
    {
        // First entry
        SAFE_FREE(data);
        uint8_t buf[2] = {HIGH_BYTE(minuteOfWeek), LOW_BYTE(minuteOfWeek)};
        err = IOT_CacheSet(SCHED_NAMESPACE, key, IOT_DAO_BLOB, buf, 2);
    }

    if (err == IOT_OK)
    {
        IOT_LOGI(TAG, "Stored schedule SMID=%04x, minuteOfWeek=%u", smid, minuteOfWeek);
    }
    else
    {
        IOT_LOGE(TAG, "Failed to store schedule SMID=%04x", smid);
    }

    return err;
}

iot_err_t IOT_DaoSmartScheduleGet(uint16_t smid, uint16_t **outTimes, uint16_t *outCount)
{
    if (outTimes == NULL || outCount == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char key[6];
    makeKey(smid, key, sizeof(key));

    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_CacheGet(SCHED_NAMESPACE, key, IOT_DAO_BLOB, &data, &dataLen);

    if (err != IOT_OK || data == NULL || dataLen < 2)
    {
        SAFE_FREE(data);
        *outTimes = NULL;
        *outCount = 0;
        return IOT_ERR_NOT_FOUND;
    }

    // Convert BE bytes to uint16_t array
    uint16_t count = dataLen / 2;
    uint16_t *times = (uint16_t *)Mem_SafeCalloc(count, sizeof(uint16_t), TAG, "sched times");
    if (times == NULL)
    {
        SAFE_FREE(data);
        return IOT_ERR_NO_MEM;
    }

    uint8_t *bytes = (uint8_t *)data;
    for (uint16_t i = 0; i < count; i++)
    {
        times[i] = BYTES_TO_U16_BE(bytes[i * 2], bytes[i * 2 + 1]);
    }

    SAFE_FREE(data);
    *outTimes = times;
    *outCount = count;
    return IOT_OK;
}

iot_err_t IOT_DaoSmartScheduleClearTimes(uint16_t smid)
{
    char key[6];
    makeKey(smid, key, sizeof(key));
    IOT_CacheDelete(SCHED_NAMESPACE, key);

    /* Deliberately NOT the "e<smid>" flag: this is the wholesale-replace path
     * (SCHEDULE_UPDATE_ANNOUNCE), and IsEnabled defaults to true, so deleting the flag here
     * would switch a schedule the user turned off back on the moment they edit its times. */
    IOT_LOGI(TAG, "Cleared schedule times SMID=%04x (enable flag kept)", smid);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartScheduleRemove(uint16_t smid)
{
    char key[6];
    makeKey(smid, key, sizeof(key));
    IOT_CacheDelete(SCHED_NAMESPACE, key);

    // Also remove enabled flag if present
    char eKey[7];
    makeEnabledKey(smid, eKey, sizeof(eKey));
    IOT_CacheDelete(SCHED_NAMESPACE, eKey);

    IOT_LOGI(TAG, "Removed schedule SMID=%04x", smid);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartScheduleGetAll(uint16_t **outSmids, uint16_t *outSmidCount,
                                     uint16_t ***outTimeLists, uint16_t **outTimeCounts)
{
    if (outSmids == NULL || outSmidCount == NULL || outTimeLists == NULL || outTimeCounts == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    // GetAll needs raw namespace iteration — cache doesn't provide this,
    // so we fall through to DAO_GetNamespaceData (cache still helps individual reads)
    char **keyList = NULL;
    uint16_t keyCount = 0;
    void **dataList = NULL;
    uint16_t *dataSizes = NULL;

    iot_err_t err = IOT_DaoStorageGetNamespaceData(SCHED_NAMESPACE, &keyList, &keyCount, &dataList, &dataSizes);
    if (err != IOT_OK || keyCount == 0)
    {
        *outSmids = NULL;
        *outSmidCount = 0;
        *outTimeLists = NULL;
        *outTimeCounts = NULL;
        return IOT_ERR_NOT_FOUND;
    }

    iot_err_t result = IOT_OK;
    uint16_t found = 0;
    uint16_t *smids = (uint16_t *)Mem_SafeCalloc(keyCount, sizeof(uint16_t), TAG, "sched smids");
    uint16_t **timeLists = (uint16_t **)Mem_SafeCalloc(keyCount, sizeof(uint16_t *), TAG, "sched tlists");
    uint16_t *timeCounts = (uint16_t *)Mem_SafeCalloc(keyCount, sizeof(uint16_t), TAG, "sched tcounts");

    if (smids == NULL || timeLists == NULL || timeCounts == NULL)
    {
        SAFE_FREE(smids);
        SAFE_FREE(timeLists);
        SAFE_FREE(timeCounts);
        result = IOT_ERR_NO_MEM;
        goto cleanup;
    }

    /* The namespace also holds enabled flags ("e"+4 hex) and the format marker, which are
     * not schedules. Including them would yield phantom SMIDs with zero times, so the tick
     * would iterate entries that can never match and "no schedules" would never be empty. */
    for (uint16_t i = 0; i < keyCount; i++)
    {
        if (!isScheduleTimesKey(keyList[i]))
        {
            continue;
        }

        smids[found] = (uint16_t)strtol(keyList[i], NULL, 16);

        uint16_t blobLen = dataSizes[i];
        uint16_t count = blobLen / 2;
        uint8_t *bytes = (uint8_t *)dataList[i];

        timeCounts[found] = count;
        timeLists[found] = (uint16_t *)Mem_SafeCalloc(count, sizeof(uint16_t), TAG, "sched times");
        if (timeLists[found] == NULL)
        {
            timeCounts[found] = 0;
            found++;
            continue;
        }

        for (uint16_t j = 0; j < count; j++)
        {
            timeLists[found][j] = BYTES_TO_U16_BE(bytes[j * 2], bytes[j * 2 + 1]);
        }
        found++;
    }

    if (found == 0)
    {
        SAFE_FREE(smids);
        SAFE_FREE(timeLists);
        SAFE_FREE(timeCounts);
        *outSmids = NULL;
        *outSmidCount = 0;
        *outTimeLists = NULL;
        *outTimeCounts = NULL;
        result = IOT_ERR_NOT_FOUND;
        goto cleanup;
    }

    *outSmids = smids;
    *outSmidCount = found;
    *outTimeLists = timeLists;
    *outTimeCounts = timeCounts;

cleanup:
    for (uint16_t i = 0; i < keyCount; i++)
    {
        SAFE_FREE(keyList[i]);
        SAFE_FREE(dataList[i]);
    }
    SAFE_FREE(keyList);
    SAFE_FREE(dataList);
    SAFE_FREE(dataSizes);

    return result;
}

iot_err_t IOT_DaoSmartScheduleSetEnabled(uint16_t smid, bool enabled)
{
    char key[7]; // "e" + 4 hex + null
    makeEnabledKey(smid, key, sizeof(key));

    uint8_t val = enabled ? 1 : 0;
    iot_err_t err = IOT_CacheSet(SCHED_NAMESPACE, key, IOT_DAO_BLOB, &val, 1);
    if (err == IOT_OK)
    {
        IOT_LOGI(TAG, "Schedule SMID=%04x %s", smid, enabled ? "enabled" : "disabled");
    }
    else
    {
        IOT_LOGE(TAG, "Failed to set enabled flag for SMID=%04x", smid);
    }
    return err;
}

bool IOT_DaoSmartScheduleIsEnabled(uint16_t smid)
{
    char key[7];
    makeEnabledKey(smid, key, sizeof(key));

    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_CacheGet(SCHED_NAMESPACE, key, IOT_DAO_BLOB, &data, &dataLen);

    if (err != IOT_OK || data == NULL || dataLen < 1)
    {
        SAFE_FREE(data);
        return true; // Default: enabled
    }

    bool enabled = (((uint8_t *)data)[0] != 0);
    SAFE_FREE(data);
    return enabled;
}

iot_err_t IOT_DaoSmartScheduleFactoryReset(void)
{
    IOT_CacheInvalidate(SCHED_NAMESPACE, NULL);
    iot_err_t err = IOT_DaoStorageRemoveNamespace(SCHED_NAMESPACE);
    if (err != IOT_OK && err != IOT_ERR_NOT_FOUND)
    {
        IOT_LOGE(TAG, "Factory reset: failed to remove schedule namespace");
        return IOT_ERR_FAIL;
    }
    IOT_LOGI(TAG, "Factory reset: schedule data cleared");
    return IOT_OK;
}

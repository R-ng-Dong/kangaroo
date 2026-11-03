#include "dao/IOT_DaoRecord.h"

#include "IOT_Common.h"
#include "IOT_Log.h"
#include "dao/IOT_DaoStorage.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "DaoRecord";

/* V1's namespace prefix. Kept here so the reaper and the format it reaps stay together;
 * V1's own source is compiled out on this build. */
#define RECORD_V1_NS_PREFIX "L"

static void recordNs(char *out, size_t cap, uint16_t eid)
{
    snprintf(out, cap, IOT_DAO_RECORD_NS_PREFIX "%04x", eid);
}

static void recordV1Ns(char *out, size_t cap, uint16_t eid)
{
    snprintf(out, cap, RECORD_V1_NS_PREFIX "%04x", eid);
}

/*
 * Four hex digits for the element, not two. Element ids run past 300 on real
 * deployments, so "%02x%04x%02x" would alias 0x0019 onto 0x0119 and two elements would
 * silently share one ring.
 */
static void recordKey(char *out, size_t cap, uint16_t elemId, uint16_t attrId, uint8_t slot)
{
    snprintf(out, cap, "%04x%04x%02x", elemId, attrId, slot);
}

/* The push bookmark's key: 'p' + elemId + attrId, nine characters like a block key but
 * never a valid one (parseKey wants all hex digits, and 'p' is not). */
#define RECORD_PUSHED_KEY_PREFIX 'p'

static void pushedKey(char *out, size_t cap, uint16_t elemId, uint16_t attrId)
{
    snprintf(out, cap, "%c%04x%04x", RECORD_PUSHED_KEY_PREFIX, elemId, attrId);
}

/** Bytes one entry occupies: (valueCount values + 1 timestamp) * 2. */
static uint16_t entryBytes(uint8_t valueCount)
{
    return (uint16_t) ((valueCount + 1) * 2);
}

/* Parse elemId/attrId/slot back out of a key. Returns false for anything that is not
 * ours -- a namespace could in principle hold a key written by a different version, and
 * mis-parsing one would attribute a block to the wrong attribute. */
static bool parseKey(const char *key, uint16_t *elemId, uint16_t *attrId, uint8_t *slot)
{
    if (key == NULL || strlen(key) != 10)
    {
        return false;
    }
    for (int i = 0; i < 10; i++)
    {
        const char c = key[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }
    unsigned e = 0, a = 0, s = 0;
    if (sscanf(key, "%4x%4x%2x", &e, &a, &s) != 3)
    {
        return false;
    }
    *elemId = (uint16_t) e;
    *attrId = (uint16_t) a;
    *slot = (uint8_t) s;
    return true;
}

iot_err_t IOT_DaoRecordPut(uint16_t eid, uint16_t elemId, uint16_t attrId, uint8_t slot,
                           const IOT_DaoRecordBlock_t *block, bool async)
{
    if (block == NULL || (block->fieldCount > 0 && block->fields == NULL))
    {
        return IOT_ERR_INVALID_ARG;
    }

    const uint16_t payloadBytes = (uint16_t) (block->fieldCount * sizeof(uint16_t));
    const uint16_t blobLen = (uint16_t) (IOT_DAO_RECORD_BLOB_HEADER + payloadBytes);

    uint8_t *blob = (uint8_t *) Mem_SafeMalloc(blobLen, TAG, "recordBlob");
    if (blob == NULL)
    {
        return IOT_ERR_NO_MEM;
    }

    /* Host order throughout. This layout is private -- the message format is core's --
     * so a byte swap here would be work for nothing, and a change to the message format
     * does not invalidate a single stored block. */
    memcpy(&blob[0], &block->checkpoint, sizeof(uint64_t));
    memcpy(&blob[8], &block->sequence, sizeof(uint16_t));
    if (payloadBytes > 0)
    {
        memcpy(&blob[IOT_DAO_RECORD_BLOB_HEADER], block->fields, payloadBytes);
    }

    char ns[16], key[16];
    recordNs(ns, sizeof(ns), eid);
    recordKey(key, sizeof(key), elemId, attrId, slot);

    /* Async copies before queueing, which is what makes a flush from the timer callback
     * safe. The caller must still treat a queue failure as "still dirty": the storage
     * queue is bounded and several instances flushing on one tick can fill it, and
     * clearing a dirty flag on queue rather than on success loses the block. */
    const iot_err_t err = async
        ? IOT_DaoStorageSetAsync(ns, key, IOT_DAO_BLOB, blob, blobLen)
        : IOT_DaoStorageSet(ns, key, IOT_DAO_BLOB, blob, blobLen);

    SAFE_FREE(blob);
    return err;
}

iot_err_t IOT_DaoRecordGet(uint16_t eid, uint16_t elemId, uint16_t attrId, uint8_t slot,
                           uint8_t valueCount, IOT_DaoRecordBlock_t *outBlock)
{
    if (outBlock == NULL || (valueCount != 1 && valueCount != 2))
    {
        return IOT_ERR_INVALID_ARG;
    }
    memset(outBlock, 0, sizeof(*outBlock));

    char ns[16], key[16];
    recordNs(ns, sizeof(ns), eid);
    recordKey(key, sizeof(key), elemId, attrId, slot);

    void *data = NULL;
    uint16_t len = 0;
    iot_err_t err = IOT_DaoStorageGet(ns, key, IOT_DAO_BLOB, &data, &len);
    if (err != IOT_OK)
    {
        return err;
    }

    /*
     * entryCount is derived, never stored, so a length that is not a whole number of
     * entries is a DETECTABLE corruption -- realistically a build whose valueCount for
     * this attrId differs from the writer's.
     *
     * Discard the block and free the slot. Truncating to the last whole entry would keep
     * misreading the same block on every boot, and the app would get a chart built from
     * fields split on the wrong boundary with nothing reported at any layer.
     */
    bool ragged = (len < IOT_DAO_RECORD_BLOB_HEADER);
    if (!ragged)
    {
        const uint16_t payload = (uint16_t) (len - IOT_DAO_RECORD_BLOB_HEADER);
        ragged = (payload % entryBytes(valueCount)) != 0;
    }
    if (ragged)
    {
        IOT_LOGW(TAG, "Discarding record EID %04x elem %04x attr %04x slot %u: %u bytes "
                      "is not a whole number of %u-byte entries",
                 eid, elemId, attrId, (unsigned) slot, (unsigned) len,
                 (unsigned) entryBytes(valueCount));
        SAFE_FREE(data);
        IOT_DaoStorageRemoveByKey(ns, key);
        return IOT_ERR_INVALID_SIZE;
    }

    const uint8_t *blob = (const uint8_t *) data;
    memcpy(&outBlock->checkpoint, &blob[0], sizeof(uint64_t));
    memcpy(&outBlock->sequence, &blob[8], sizeof(uint16_t));

    const uint16_t payloadBytes = (uint16_t) (len - IOT_DAO_RECORD_BLOB_HEADER);
    outBlock->fieldCount = (uint16_t) (payloadBytes / sizeof(uint16_t));

    if (payloadBytes > 0)
    {
        outBlock->fields = (uint16_t *) Mem_SafeMalloc(payloadBytes, TAG, "recordFields");
        if (outBlock->fields == NULL)
        {
            SAFE_FREE(data);
            outBlock->fieldCount = 0;
            return IOT_ERR_NO_MEM;
        }
        memcpy(outBlock->fields, &blob[IOT_DAO_RECORD_BLOB_HEADER], payloadBytes);
    }

    SAFE_FREE(data);
    return IOT_OK;
}

iot_err_t IOT_DaoRecordPutPushed(uint16_t eid, uint16_t elemId, uint16_t attrId, uint16_t sequence, bool async)
{
    char ns[16], key[16];
    recordNs(ns, sizeof(ns), eid);
    pushedKey(key, sizeof(key), elemId, attrId);

    /* Big-endian, unlike the block header: a byte-order-free read of a sequence number
     * is easier to check in a dump. */
    uint8_t value[2] = {(uint8_t) (sequence >> 8), (uint8_t) (sequence & 0xFF)};
    return async ? IOT_DaoStorageSetAsync(ns, key, IOT_DAO_BLOB, value, sizeof(value))
                 : IOT_DaoStorageSet(ns, key, IOT_DAO_BLOB, value, sizeof(value));
}

iot_err_t IOT_DaoRecordGetPushed(uint16_t eid, uint16_t elemId, uint16_t attrId, uint16_t *outSequence)
{
    if (outSequence == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char ns[16], key[16];
    recordNs(ns, sizeof(ns), eid);
    pushedKey(key, sizeof(key), elemId, attrId);

    void *data = NULL;
    uint16_t len = 0;
    iot_err_t err = IOT_DaoStorageGet(ns, key, IOT_DAO_BLOB, &data, &len);
    if (err != IOT_OK)
    {
        return err;
    }
    if (len != 2 || data == NULL)
    {
        /* Free the key so the next boot starts from "never pushed" rather than failing
         * on the same bytes forever. */
        IOT_LOGW(TAG, "Discarding push bookmark '%s' in %s: %u bytes, expected 2", key, ns, (unsigned) len);
        SAFE_FREE(data);
        IOT_DaoStorageRemoveByKey(ns, key);
        return IOT_ERR_INVALID_SIZE;
    }

    const uint8_t *b = (const uint8_t *) data;
    *outSequence = (uint16_t) (((uint16_t) b[0] << 8) | b[1]);
    SAFE_FREE(data);
    return IOT_OK;
}

iot_err_t IOT_DaoRecordRemoveSlot(uint16_t eid, uint16_t elemId, uint16_t attrId, uint8_t slot)
{
    char ns[16], key[16];
    recordNs(ns, sizeof(ns), eid);
    recordKey(key, sizeof(key), elemId, attrId, slot);
    return IOT_DaoStorageRemoveByKey(ns, key);
}

/* Does the namespace hold anything? IOT_ERR_NOT_FOUND = absent or empty, IOT_OK = at least one
 * key, any other error = the probe could not run. The probe enumerates entries and creates
 * nothing; removing instead opens the namespace read-write, which CREATES an absent one, and
 * storage never frees a namespace entry. Callers skip the removal ONLY on NOT_FOUND: a probe that
 * failed says nothing about the data, so they still remove. */
static iot_err_t nsProbe(const char *ns)
{
    char **keys = NULL;
    uint16_t n = 0;
    iot_err_t err = IOT_DaoStorageGetKeyList(ns, &keys, &n);
    if (err != IOT_OK)
    {
        return err;
    }
    for (uint16_t i = 0; i < n; i++)
    {
        SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
    return (n > 0) ? IOT_OK : IOT_ERR_NOT_FOUND;
}

iot_err_t IOT_DaoRecordRemoveDevice(uint16_t eid)
{
    char ns[16];
    recordNs(ns, sizeof(ns), eid);
    if (nsProbe(ns) == IOT_ERR_NOT_FOUND)
    {
        IOT_LOGD(TAG, "No Record data for EID=%04x, nothing to remove", eid);
        return IOT_OK;
    }
    return IOT_DaoStorageRemoveNamespace(ns);
}

iot_err_t IOT_DaoRecordReapV1(uint16_t eid)
{
    char ns[16];
    recordV1Ns(ns, sizeof(ns), eid);

    /* Absence is the normal case -- this runs for every known device at every boot on a
     * V2 build, and only a device that was once on V1 has anything here. Probe first, as
     * IOT_DaoRecordRemoveDevice() does: removing an absent namespace creates it first. Reporting
     * NOT_FOUND as failure would make the boot pass log an error per device. */
    if (nsProbe(ns) == IOT_ERR_NOT_FOUND)
    {
        IOT_LOGD(TAG, "No Record V1 data for EID=%04x, nothing to reap", eid);
        return IOT_OK;
    }
    const iot_err_t err = IOT_DaoStorageRemoveNamespace(ns);
    if (err == IOT_OK)
    {
        IOT_LOGI(TAG, "Reaped Record V1 namespace %s", ns);
    }
    return IOT_OK;
}

iot_err_t IOT_DaoRecordScanDevice(uint16_t eid, IOT_DaoRecordSlotInfo_t *out, uint16_t outCap,
                                  uint16_t *outCount)
{
    if (out == NULL || outCount == NULL || outCap == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }
    *outCount = 0;

    char ns[16];
    recordNs(ns, sizeof(ns), eid);

    /* One call for the whole device. The namespace is per-EID, so this returns every
     * block of every attribute rather than one read per record -- the difference between
     * one flash transaction at boot and 8 attributes x 16 slots of them. */
    char **keys = NULL;
    void **datas = NULL;
    uint16_t *sizes = NULL;
    uint16_t count = 0;
    iot_err_t err = IOT_DaoStorageGetNamespaceData(ns, &keys, &count, &datas, &sizes);
    if (err != IOT_OK)
    {
        /* NOT_FOUND is the normal answer for a device that has never recorded. Anything
         * else means stored history exists and could not be read -- and the caller's
         * reaction is to rebuild nothing, so the history is orphaned without a word. */
        if (err != IOT_ERR_NOT_FOUND)
        {
            IOT_LOGE(TAG, "Cannot read %s (err %d): stored history is unreachable this boot",
                     ns, (int) err);
        }
        return err;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        uint16_t elemId = 0, attrId = 0;
        uint8_t slot = 0;
        if (keys[i] != NULL && keys[i][0] == RECORD_PUSHED_KEY_PREFIX)
        {
            /* A push bookmark shares the namespace but is not a block. Skip it quietly:
             * there is one per series, so a WARN would fire for each on every boot. */
            continue;
        }
        if (!parseKey(keys[i], &elemId, &attrId, &slot))
        {
            IOT_LOGW(TAG, "Ignoring unrecognised key '%s' in %s", keys[i] ? keys[i] : "(null)", ns);
            continue;
        }
        if (sizes[i] < IOT_DAO_RECORD_BLOB_HEADER || datas[i] == NULL)
        {
            IOT_LOGW(TAG, "Ignoring short blob for key '%s' in %s (%u bytes)",
                     keys[i], ns, (unsigned) sizes[i]);
            continue;
        }
        if (*outCount >= outCap)
        {
            /* Say so rather than stopping quietly: the caller sized the array from
             * CONFIG_IOT_RECORD_BLOCKS_PER_ATTR, so an overflow means storage holds more
             * than this build believes it can, and the excess would be invisible. */
            IOT_LOGW(TAG, "%s holds more than %u slots; the rest are ignored this boot",
                     ns, (unsigned) outCap);
            break;
        }

        const uint8_t *blob = (const uint8_t *) datas[i];
        IOT_DaoRecordSlotInfo_t *info = &out[*outCount];
        info->elemId = elemId;
        info->attrId = attrId;
        info->slot = slot;
        memcpy(&info->checkpoint, &blob[0], sizeof(uint64_t));
        memcpy(&info->sequence, &blob[8], sizeof(uint16_t));
        info->fieldCount = (uint16_t) ((sizes[i] - IOT_DAO_RECORD_BLOB_HEADER) / sizeof(uint16_t));
        (*outCount)++;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        SAFE_FREE(keys[i]);
        SAFE_FREE(datas[i]);
    }
    SAFE_FREE(keys);
    SAFE_FREE(datas);
    SAFE_FREE(sizes);

    return IOT_OK;
}

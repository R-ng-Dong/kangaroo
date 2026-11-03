#include "dao/IOT_DaoIRMgmt.h"
#include "dao/IOT_DaoStoresInternal.h"
#include "IOT_Common.h"
#include "IOT_Memory.h"
#include "IOT_ErrorManager.h"
#include "dao/IOT_DaoStorage.h"
#include "IOT_Log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "DaoIR";

// One NVS namespace per device: "ir{eid:04x}" (≤ 6 chars, within NVS 15-char limit).
// Keys within that namespace are short and eid-free:
//   Protocol code (u64): "P{dn:04x}"
//   Rawzip header (blob): "H"
//   Rawzip data  (blob):  "D{dn:04x}"
// This makes per-device removal one RemoveNamespace instead of per-key deletes (after a key-list
// probe that skips a device with no IR data; see nsProbe()).

static void makeNs(uint16_t eid, char *out, size_t outSize)
{
    snprintf(out, outSize, "ir%04x", eid);
}

static void makeProtocolKey(uint16_t dataName, char *out, size_t outSize)
{
    snprintf(out, outSize, "P%04x", dataName);
}

static void makeHeaderKey(char *out, size_t outSize)
{
    snprintf(out, outSize, "H");
}

static void makeDataKey(uint16_t dataName, char *out, size_t outSize)
{
    snprintf(out, outSize, "D%04x", dataName);
}

// ============================================================================
// Initialization
// ============================================================================

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

iot_err_t IOT_DaoIRInit(void)
{
    IOT_LOGI(TAG, "IR DAO initialized");
    return IOT_OK;
}

// ============================================================================
// Batched keyset store (single commit)
// ============================================================================

iot_err_t IOT_DaoIRStoreKeyset(uint16_t eid, const IOT_IRKeysetEntry_t *entries, uint16_t count)
{
    if (entries == NULL || count == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char ns[8];
    makeNs(eid, ns, sizeof(ns));

    // Key strings and batch descriptors must outlive the (blocking) batch call.
    char (*keys)[8] = (char (*)[8]) Mem_SafeMalloc((size_t) count * sizeof(*keys), TAG, "irKeys");
    IOT_DaoBatchEntry_t *batch = (IOT_DaoBatchEntry_t *) Mem_SafeMalloc(
        (size_t) count * sizeof(IOT_DaoBatchEntry_t), TAG, "irBatch");
    if (keys == NULL || batch == NULL)
    {
        SAFE_FREE(keys);
        SAFE_FREE(batch);
        return IOT_ERR_NO_MEM;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        const IOT_IRKeysetEntry_t *e = &entries[i];
        if (e->dataName == IOT_IR_HEADER_DATANAME)
        {
            makeHeaderKey(keys[i], sizeof(keys[i]));
            batch[i].key = keys[i];
            batch[i].type = IOT_DAO_BLOB;
            batch[i].data = e->data;
            batch[i].len = e->dataLen;
        }
        else if (e->isProtocol)
        {
            makeProtocolKey(e->dataName, keys[i], sizeof(keys[i]));
            batch[i].key = keys[i];
            batch[i].type = IOT_DAO_U64;
            batch[i].data = &e->code;
            batch[i].len = sizeof(uint64_t);
        }
        else
        {
            makeDataKey(e->dataName, keys[i], sizeof(keys[i]));
            batch[i].key = keys[i];
            batch[i].type = IOT_DAO_BLOB;
            batch[i].data = e->data;
            batch[i].len = e->dataLen;
        }
    }

    iot_err_t err = IOT_DaoStorageSetBatch(ns, batch, count);
    SAFE_FREE(keys);
    SAFE_FREE(batch);

    if (err == IOT_OK)
    {
        IOT_LOGI(TAG, "Stored keyset: EID=%04x entries=%u (1 commit)", eid, count);
    }
    else
    {
        IOT_LOGE(TAG, "Failed to store keyset: EID=%04x entries=%u", eid, count);
    }
    return err;
}

// ============================================================================
// Protocol-based IR codes
// ============================================================================

iot_err_t IOT_DaoIRSetProtocolCode(uint16_t eid, uint16_t dataName, uint64_t code)
{
    char ns[8], key[8];
    makeNs(eid, ns, sizeof(ns));
    makeProtocolKey(dataName, key, sizeof(key));

    iot_err_t err = IOT_DaoStorageSet(ns, key, IOT_DAO_U64, &code, sizeof(uint64_t));
    return (err == IOT_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoIRGetProtocolCode(uint16_t eid, uint16_t dataName, uint64_t *outCode)
{
    if (outCode == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char ns[8], key[8];
    makeNs(eid, ns, sizeof(ns));
    makeProtocolKey(dataName, key, sizeof(key));

    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_DaoStorageGet(ns, key, IOT_DAO_U64, &data, &dataLen);
    if (err != IOT_OK || data == NULL)
    {
        SAFE_FREE(data);
        return IOT_ERR_NOT_FOUND;
    }

    *outCode = *(uint64_t *)data;
    SAFE_FREE(data);
    return IOT_OK;
}

// ============================================================================
// Rawzip header
// ============================================================================

iot_err_t IOT_DaoIRSetRawZipHeader(uint16_t eid, const uint8_t *header, uint16_t headerLen)
{
    if (header == NULL || headerLen == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char ns[8], key[8];
    makeNs(eid, ns, sizeof(ns));
    makeHeaderKey(key, sizeof(key));

    iot_err_t err = IOT_DaoStorageSet(ns, key, IOT_DAO_BLOB, (void *)header, headerLen);
    return (err == IOT_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoIRGetRawZipHeader(uint16_t eid, uint8_t **outHeader, uint16_t *outHeaderLen)
{
    if (outHeader == NULL || outHeaderLen == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char ns[8], key[8];
    makeNs(eid, ns, sizeof(ns));
    makeHeaderKey(key, sizeof(key));

    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_DaoStorageGet(ns, key, IOT_DAO_BLOB, &data, &dataLen);
    if (err != IOT_OK || data == NULL || dataLen == 0)
    {
        SAFE_FREE(data);
        *outHeader = NULL;
        *outHeaderLen = 0;
        return IOT_ERR_NOT_FOUND;
    }

    *outHeader = (uint8_t *)data;
    *outHeaderLen = dataLen;
    return IOT_OK;
}

// ============================================================================
// Rawzip data blobs
// ============================================================================

iot_err_t IOT_DaoIRSetRawZipData(uint16_t eid, uint16_t dataName, const uint8_t *data, uint16_t dataLen)
{
    if (data == NULL || dataLen == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char ns[8], key[8];
    makeNs(eid, ns, sizeof(ns));
    makeDataKey(dataName, key, sizeof(key));

    iot_err_t err = IOT_DaoStorageSet(ns, key, IOT_DAO_BLOB, (void *)data, dataLen);
    return (err == IOT_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoIRGetRawZipData(uint16_t eid, uint16_t dataName, uint8_t **outData, uint16_t *outDataLen)
{
    if (outData == NULL || outDataLen == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char ns[8], key[8];
    makeNs(eid, ns, sizeof(ns));
    makeDataKey(dataName, key, sizeof(key));

    void *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t err = IOT_DaoStorageGet(ns, key, IOT_DAO_BLOB, &data, &dataLen);
    if (err != IOT_OK || data == NULL || dataLen == 0)
    {
        SAFE_FREE(data);
        *outData = NULL;
        *outDataLen = 0;
        return IOT_ERR_NOT_FOUND;
    }

    *outData = (uint8_t *)data;
    *outDataLen = dataLen;
    return IOT_OK;
}

// ============================================================================
// Cleanup
// ============================================================================

iot_err_t IOT_DaoIRRemoveDevice(uint16_t eid)
{
    char ns[8];
    makeNs(eid, ns, sizeof(ns));
    if (nsProbe(ns) == IOT_ERR_NOT_FOUND)
    {
        IOT_LOGD(TAG, "No IR data for EID=%04x, nothing to remove", eid);
        return IOT_OK;
    }

    // Drop the whole per-device namespace in one call.
    iot_err_t err = IOT_DaoStorageRemoveNamespace(ns);
    if (err != IOT_OK && err != IOT_ERR_NOT_FOUND)
    {
        IOT_LOGE(TAG, "Failed to remove IR namespace for EID=%04x", eid);
        return IOT_ERR_FAIL;
    }
    IOT_LOGI(TAG, "Removed IR namespace for EID=%04x", eid);
    return IOT_OK;
}

iot_err_t IOT_DaoIRFactoryReset(const uint16_t *eids, uint16_t count)
{
    if (eids == NULL || count == 0)
    {
        return IOT_OK;
    }
    for (uint16_t i = 0; i < count; i++)
    {
        char ns[8];
        makeNs(eids[i], ns, sizeof(ns));
        if (nsProbe(ns) == IOT_ERR_NOT_FOUND)
        {
            IOT_LOGD(TAG, "No IR data for EID=%04x, nothing to remove", eids[i]);
            continue;
        }
        IOT_DaoStorageRemoveNamespace(ns);
    }
    IOT_LOGI(TAG, "Factory reset: cleared IR data for %u device(s)", count);
    return IOT_OK;
}

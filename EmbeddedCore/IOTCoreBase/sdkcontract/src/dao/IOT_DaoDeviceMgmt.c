#include "dao/IOT_DaoDeviceMgmt.h"
#include "dao/IOT_DaoStoresInternal.h"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include "IOT_Log.h"
#include "dao/IOT_DaoStorage.h"
#include "dao/IOT_Cached.h"
#include "IOT_Memory.h"
#include "IOT_Osal.h"
#include "IOT_ContractAttrsInternal.h"
#include "IOT_Core.h"
#include "IOT_TimerMgmt.h"

// ============================================================================
// Defines
// ============================================================================

#define DEVICE_IDENTITY_INFO_NAMESPACE "device_identity"
#define DEVICE_COMMON_INFO_NAMESPACE "dev_common_info"
#define DEVICE_ELEMS_STATE_INFO_NAMESPACE "elements_info"
#define DEVICE_MESH_DEVKEY_INFO_NAMESPACE "mesh_devkey_info"

#define MAX_DEVICE_CACHE 20

static const char *TAG = "DAO-DEVICE";

// ============================================================================
// Helper: Convert EID to key string
// ============================================================================

static void eid_to_key(uint16_t eid, char *key, size_t key_size)
{
    snprintf(key, key_size, "%u", eid);
}

// ============================================================================
// Helper: Get EID by MAC address
// ============================================================================

static iot_err_t getEidByMac(uint8_t *mac, uint8_t macSize, uint16_t *outEid)
{
    if (mac == NULL || outEid == NULL)
    {
        IOT_LOGE(TAG, "Invalid argument to getEidByMac");
        return IOT_ERR_INVALID_ARG;
    }

    iot_err_t storageErr = IOT_OK;
    char **eidKeyList = NULL;
    uint16_t eidCount = 0;
    uint8_t **dataList = NULL;
    uint16_t *dataSizes = NULL;

    storageErr =
        IOT_DaoStorageGetNamespaceData(DEVICE_IDENTITY_INFO_NAMESPACE, &eidKeyList, &eidCount, (void ***) &dataList, &dataSizes);
    if (storageErr == IOT_ERR_NOT_FOUND)
    {
        return storageErr;
    }
    else if (storageErr != IOT_OK)
    {
        IOT_LOGE(TAG, "Get eid by mac failed, Failed to get device identity data");
        return storageErr;
    }

    for (uint16_t i = 0; i < eidCount; i++)
    {
        if (dataList == NULL || dataList[i] == NULL)
            continue;

        uint8_t *flatData = (uint8_t *) dataList[i];
        uint8_t storedMacType = flatData[0];
        (void) storedMacType; // Unused for now
        uint8_t storedMacSize = flatData[1];
        uint8_t *storedMac = &flatData[2];

        if (storedMacSize == macSize && memcmp(mac, storedMac, macSize) == 0)
        {
            // Parse EID from key
            *outEid = (uint16_t) atoi(eidKeyList[i]);

            // Free allocated memory
            for (uint16_t j = 0; j < eidCount; j++)
            {
                if (dataList && dataList[j])
                    SAFE_FREE(dataList[j]);
                if (eidKeyList && eidKeyList[j])
                    SAFE_FREE(eidKeyList[j]);
            }
            if (dataList)
                SAFE_FREE(dataList);
            if (dataSizes)
                SAFE_FREE(dataSizes);
            if (eidKeyList)
                SAFE_FREE(eidKeyList);
            return IOT_OK;
        }
    }

    // Not found: free allocated memory
    for (uint16_t j = 0; j < eidCount; j++)
    {
        if (dataList && dataList[j])
            SAFE_FREE(dataList[j]);
        if (eidKeyList && eidKeyList[j])
            SAFE_FREE(eidKeyList[j]);
    }
    if (dataList)
        SAFE_FREE(dataList);
    if (dataSizes)
        SAFE_FREE(dataSizes);
    if (eidKeyList)
        SAFE_FREE(eidKeyList);

    return IOT_ERR_NOT_FOUND;
}

// ============================================================================
// Memory Cleanup Helpers
// ============================================================================

void IOT_DaoDeviceFreeIdentityInfo(IOT_DevIdentityInfo_t *info)
{
    if (info == NULL)
        return;

    if (info->mac != NULL)
    {
        SAFE_FREE(info->mac);
        info->mac = NULL;
    }
    info->macSize = 0;
    info->macType = 0;
}

void IOT_DaoDeviceFreeElemsInfo(IOT_DaoDeviceElemsAttrsInfo_t *elemsInfo)
{
    if (elemsInfo == NULL)
        return;

    if (elemsInfo->elemList != NULL)
    {
        for (uint16_t i = 0; i < elemsInfo->numOfElem; i++)
        {
            IOT_DaoDeviceStateElemState_t *elem = &elemsInfo->elemList[i];
            if (elem->attrList != NULL)
            {
                // Free each attribute value
                for (uint8_t j = 0; j < elem->numOfAttr; j++)
                {
                    if (elem->attrList[j].attrValue != NULL)
                    {
                        SAFE_FREE(elem->attrList[j].attrValue);
                        elem->attrList[j].attrValue = NULL;
                    }
                }
                SAFE_FREE(elem->attrList);
                elem->attrList = NULL;
            }
            elem->numOfAttr = 0;
        }
        SAFE_FREE(elemsInfo->elemList);
        elemsInfo->elemList = NULL;
    }
    elemsInfo->numOfElem = 0;
}

// ============================================================================
// Initialization
// ============================================================================

// ============================================================================
// RAM overlay: values that are not kept across a power cut
// ============================================================================

/* A measured attribute (IOT_AttrIsPersisted() false) changes often and is measured again after a
 * power cut, so its value lives here and never in the flash blob: writing it there wore the flash
 * out once a second for nothing. Reads merge it into the blob's attributes; one with no value here
 * reads as size 0 ("unknown") until the product reports it. */
typedef struct device_ovl_s
{
    struct device_ovl_s *next;
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint16_t size;
    uint8_t *value;
} device_ovl_t;

static device_ovl_t *s_ovl = NULL;
static IOT_OsalMutexHandle_t s_ovlLock = NULL;

static void ovlLock(void)
{
    if (s_ovlLock == NULL)
        s_ovlLock = IOT_OsalMutexCreate();
    if (s_ovlLock != NULL)
        (void) IOT_OsalMutexLock(s_ovlLock, UINT32_MAX);
}

static void ovlUnlock(void)
{
    if (s_ovlLock != NULL)
        (void) IOT_OsalMutexUnlock(s_ovlLock);
}

static device_ovl_t *ovlFindLocked(uint16_t eid, uint16_t elemId, uint16_t attrId)
{
    for (device_ovl_t *n = s_ovl; n != NULL; n = n->next)
    {
        if (n->eid == eid && n->elemId == elemId && n->attrId == attrId)
            return n;
    }
    return NULL;
}

static iot_err_t ovlSet(uint16_t eid, uint16_t elemId, uint16_t attrId, const uint8_t *value, uint16_t size)
{
    uint8_t *copy = Mem_SafeMalloc(size, TAG, "ovl value");
    if (copy == NULL)
        return IOT_ERR_NO_MEM;
    memcpy(copy, value, size);

    ovlLock();
    device_ovl_t *n = ovlFindLocked(eid, elemId, attrId);
    if (n == NULL)
    {
        n = Mem_SafeCalloc(1, sizeof(*n), TAG, "ovl node");
        if (n == NULL)
        {
            ovlUnlock();
            SAFE_FREE(copy);
            return IOT_ERR_NO_MEM;
        }
        n->eid = eid;
        n->elemId = elemId;
        n->attrId = attrId;
        n->next = s_ovl;
        s_ovl = n;
    }
    SAFE_FREE(n->value);
    n->value = copy;
    n->size = size;
    ovlUnlock();
    return IOT_OK;
}

/* eid 0xFFFF: every device. */
static void ovlDrop(uint16_t eid)
{
    ovlLock();
    device_ovl_t **pp = &s_ovl;
    while (*pp != NULL)
    {
        device_ovl_t *n = *pp;
        if (eid == 0xFFFF || n->eid == eid)
        {
            *pp = n->next;
            SAFE_FREE(n->value);
            SAFE_FREE(n);
        }
        else
        {
            pp = &n->next;
        }
    }
    ovlUnlock();
}

// ============================================================================
// Write budget: a device's persisted state is written at most ~60 times an hour
// ============================================================================

/* A control changed by a person writes flash a few dozen times a day. A product or an automation
 * that changes a persisted attribute every few seconds would write once a second, forever. Past
 * DEVICE_PERSIST_BUDGET_PER_HOUR writes in a window, the new blob is held here (readers see it)
 * and committed once per DEVICE_PERSIST_HOLD_MS. A write is counted at most once per flush
 * interval, because the cache coalesces everything inside one flush into one NVS write.
 *
 * The bound is approximate. The cache flushes on its own periodic tick, not one interval after
 * the write, so two changes under a second apart can straddle a tick and cost two writes for one
 * count, and a change just after the last counted write still goes straight to the cache. Worst
 * case is about 1.5x the counted writes (~150 an hour with the hold), still bounded. Counting
 * every change instead would hold a person's quick double tap. */
#define DEVICE_PERSIST_BUDGET_PER_HOUR 60u
#define DEVICE_PERSIST_WINDOW_MS 3600000u
#define DEVICE_PERSIST_HOLD_MS 60000u
#define DEVICE_PERSIST_RETRY_MS 1000u
#define DEVICE_PERSIST_FLUSH_MS 1000u

typedef struct device_budget_s
{
    struct device_budget_s *next;
    uint16_t eid;
    uint16_t count;
    uint32_t windowStartMs;
    uint32_t lastCountedMs;
    bool warned;
    uint8_t *pending; /* held blob, or NULL */
    uint16_t pendingLen;
} device_budget_t;

static device_budget_t *s_budget = NULL;
static volatile IOT_TimerHandle_t s_holdTimer = IOT_TIMER_HANDLE_INVALID;

static device_budget_t *budgetFindLocked(uint16_t eid)
{
    for (device_budget_t *b = s_budget; b != NULL; b = b->next)
    {
        if (b->eid == eid)
            return b;
    }
    return NULL;
}

/* eid 0xFFFF: every device. Held values are dropped, not written. */
static void budgetDrop(uint16_t eid)
{
    ovlLock();
    device_budget_t **pp = &s_budget;
    while (*pp != NULL)
    {
        device_budget_t *b = *pp;
        if (eid == 0xFFFF || b->eid == eid)
        {
            *pp = b->next;
            SAFE_FREE(b->pending);
            SAFE_FREE(b);
        }
        else
        {
            pp = &b->next;
        }
    }
    bool anyPending = false;
    for (device_budget_t *b = s_budget; b != NULL; b = b->next)
        anyPending = anyPending || (b->pending != NULL);
    IOT_TimerHandle_t t = anyPending ? IOT_TIMER_HANDLE_INVALID : s_holdTimer;
    if (!anyPending)
        s_holdTimer = IOT_TIMER_HANDLE_INVALID;
    ovlUnlock();
    if (t != IOT_TIMER_HANDLE_INVALID)
        (void) IOT_TimerCancel(t);
}

static void budgetDropPending(uint16_t eid)
{
    ovlLock();
    device_budget_t *b = budgetFindLocked(eid);
    if (b != NULL)
    {
        SAFE_FREE(b->pending);
        b->pendingLen = 0;
    }
    ovlUnlock();
}

/* The element-state blob as readers must see it: a held one first, else the cache's. */
static iot_err_t readStateBlob(uint16_t eid, const char *key, uint8_t **out, uint16_t *outLen)
{
    ovlLock();
    device_budget_t *b = budgetFindLocked(eid);
    if (b != NULL && b->pending != NULL)
    {
        uint8_t *copy = Mem_SafeMalloc(b->pendingLen, TAG, "held state copy");
        if (copy == NULL)
        {
            ovlUnlock();
            return IOT_ERR_NO_MEM;
        }
        memcpy(copy, b->pending, b->pendingLen);
        *out = copy;
        *outLen = b->pendingLen;
        ovlUnlock();
        return IOT_OK;
    }
    ovlUnlock();
    return IOT_CacheGet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void **) out, outLen);
}

static void holdTimerCb(void *userData);

static void armHoldTimerLocked(uint32_t ms)
{
    if (s_holdTimer == IOT_TIMER_HANDLE_INVALID)
        s_holdTimer = IOT_TimerInsertCb(holdTimerCb, NULL, ms);
}

iot_err_t IOT_DaoDeviceCommitPending(void)
{
    iot_err_t result = IOT_OK;
    ovlLock();
    for (device_budget_t *b = s_budget; b != NULL; b = b->next)
    {
        if (b->pending == NULL)
            continue;
        char key[8];
        eid_to_key(b->eid, key, sizeof(key));
        iot_err_t err = IOT_CacheSet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, b->pending, b->pendingLen);
        if (err == IOT_OK)
        {
            SAFE_FREE(b->pending);
            b->pendingLen = 0;
        }
        else
        {
            IOT_LOGW(TAG, "EID %04x: held element state not written (%d), retrying", b->eid, (int) err);
            result = err;
        }
    }
    bool anyPending = false;
    for (device_budget_t *b = s_budget; b != NULL; b = b->next)
        anyPending = anyPending || (b->pending != NULL);
    if (anyPending)
        armHoldTimerLocked(DEVICE_PERSIST_HOLD_MS);
    ovlUnlock();
    return result;
}

static void holdWork(void *arg)
{
    (void) arg;
    (void) IOT_DaoDeviceCommitPending();
}

/* Timer task: only hands the commit to core's task, or re-arms a short retry. No lock here (the
 * timer task's stack is small and shared): the handle is only cleared or set, and a race at worst
 * arms the commit twice, which writes the same blob twice. */
static void holdTimerCb(void *userData)
{
    (void) userData;
    s_holdTimer = IOT_TIMER_HANDLE_INVALID;
    if (IOT_CoreDeferWork(holdWork, NULL) != IOT_OK)
        s_holdTimer = IOT_TimerInsertCb(holdTimerCb, NULL, DEVICE_PERSIST_RETRY_MS);
}

/* Persist a changed blob, or hold it when this device is over its write budget. */
static iot_err_t persistStateBlob(uint16_t eid, const char *key, uint8_t *blob, uint16_t len, uint16_t attrId)
{
    uint32_t now = IOT_TimerGetCurrentTimeMs();
    ovlLock();
    device_budget_t *b = budgetFindLocked(eid);
    if (b == NULL)
    {
        b = Mem_SafeCalloc(1, sizeof(*b), TAG, "write budget");
        if (b == NULL)
        {
            ovlUnlock();
            return IOT_CacheSet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, blob, len);
        }
        b->eid = eid;
        b->windowStartMs = now;
        b->lastCountedMs = now - DEVICE_PERSIST_FLUSH_MS;
        b->next = s_budget;
        s_budget = b;
    }
    if ((uint32_t) (now - b->windowStartMs) >= DEVICE_PERSIST_WINDOW_MS)
    {
        b->windowStartMs = now;
        b->count = 0;
        b->warned = false;
    }

    /* A change inside the same flush interval as the last counted one joins that NVS write. */
    bool newWrite = (uint32_t) (now - b->lastCountedMs) >= DEVICE_PERSIST_FLUSH_MS;
    bool hold;
    if (b->count >= DEVICE_PERSIST_BUDGET_PER_HOUR && (newWrite || b->pending != NULL))
    {
        hold = true; /* over budget: keep it until the hold timer commits it */
    }
    else
    {
        hold = false; /* under budget (or the window rolled over while holding: the new blob carries
                         the held one) */
        if (newWrite || b->pending != NULL)
        {
            b->count++;
            b->lastCountedMs = now;
        }
    }

    if (!hold)
    {
        SAFE_FREE(b->pending);
        b->pendingLen = 0;
        ovlUnlock();
        return IOT_CacheSet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, blob, len);
    }

    uint8_t *copy = Mem_SafeMalloc(len, TAG, "held state");
    if (copy == NULL)
    {
        ovlUnlock();
        return IOT_ERR_NO_MEM;
    }
    memcpy(copy, blob, len);
    SAFE_FREE(b->pending);
    b->pending = copy;
    b->pendingLen = len;
    if (!b->warned)
    {
        b->warned = true;
        IOT_LOGW(TAG, "EID %04x: over %u flash writes this hour (attr %04x), holding element state %us at a time",
                 eid, (unsigned) DEVICE_PERSIST_BUDGET_PER_HOUR, attrId, (unsigned) (DEVICE_PERSIST_HOLD_MS / 1000u));
    }
    armHoldTimerLocked(DEVICE_PERSIST_HOLD_MS);
    ovlUnlock();
    return IOT_OK;
}

/* Replace every non-persisted attribute of @p out with its RAM value (or size 0), then append the
 * RAM values that have no slot in the blob. On failure @p out stays freeable. */
static iot_err_t ovlMerge(uint16_t eid, IOT_DaoDeviceElemsAttrsInfo_t *out)
{
    iot_err_t err = IOT_OK;
    ovlLock();
    for (uint16_t e = 0; e < out->numOfElem && err == IOT_OK; e++)
    {
        IOT_DaoDeviceStateElemState_t *el = &out->elemList[e];
        for (uint8_t a = 0; a < el->numOfAttr; a++)
        {
            IOT_DaoDeviceStateAttr_t *at = &el->attrList[a];
            if (IOT_AttrIsPersisted(at->attrId))
                continue;
            SAFE_FREE(at->attrValue);
            at->attrSize = 0;
            device_ovl_t *n = ovlFindLocked(eid, el->elemId, at->attrId);
            if (n == NULL)
                continue;
            at->attrValue = Mem_SafeMalloc(n->size, TAG, "ovl merge value");
            if (at->attrValue == NULL)
            {
                err = IOT_ERR_NO_MEM;
                break;
            }
            memcpy(at->attrValue, n->value, n->size);
            at->attrSize = n->size;
        }
        for (device_ovl_t *n = s_ovl; n != NULL && err == IOT_OK; n = n->next)
        {
            if (n->eid != eid || n->elemId != el->elemId)
                continue;
            bool inBlob = false;
            for (uint8_t a = 0; a < el->numOfAttr && !inBlob; a++)
                inBlob = (el->attrList[a].attrId == n->attrId);
            if (inBlob || el->numOfAttr == UINT8_MAX)
                continue;
            uint8_t *value = Mem_SafeMalloc(n->size, TAG, "ovl append value");
            if (value == NULL)
            {
                err = IOT_ERR_NO_MEM;
                break;
            }
            IOT_DaoDeviceStateAttr_t *grown = Mem_SafeRealloc(
                el->attrList, (size_t) (el->numOfAttr + 1) * sizeof(IOT_DaoDeviceStateAttr_t), TAG, "ovl append");
            if (grown == NULL)
            {
                SAFE_FREE(value);
                err = IOT_ERR_NO_MEM;
                break;
            }
            el->attrList = grown;
            memcpy(value, n->value, n->size);
            el->attrList[el->numOfAttr].attrId = n->attrId;
            el->attrList[el->numOfAttr].attrSize = n->size;
            el->attrList[el->numOfAttr].attrValue = value;
            el->numOfAttr++;
        }
    }
    ovlUnlock();
    return err;
}

iot_err_t IOT_DaoDeviceInit(void)
{
    IOT_LOGI(TAG, "Initializing device management");

    /* Here, before any other task can read state: created on first use, two tasks arriving
     * together would each create one. ovlLock() still creates it if init never ran (host tools). */
    if (s_ovlLock == NULL)
        s_ovlLock = IOT_OsalMutexCreate();

    /* Write-back, flushed once a second.
     *
     * The storage write dominates the cost of a control command, so a command writes to RAM
     * and a burst of changes coalesces into one flush. Storage already skips writes of
     * identical values, so this pays off for bursts of CHANGING values; an idle device sees
     * no change.
     *
     * The flush runs on the cache's own worker, never on the timer service task: serialising
     * each dirty entry and waiting on storage does not fit in a timer callback's stack.
     *
     * Why 1 s: a control burst coalesces whole inside it, and a short interval keeps the
     * window in which a power cut loses the last state change small. Controlled restarts
     * flush first, so the exposure is genuine power loss only. */
    const IOT_CacheConfig_t nsConfig = {.nsName = DEVICE_ELEMS_STATE_INFO_NAMESPACE,
                                        .maxEntries = MAX_DEVICE_CACHE,
                                        .flushIntervalMs = 1000,
                                        .policy = IOT_CACHE_WRITE_BACK};

    iot_err_t err = IOT_CacheRegisterNamespace(&nsConfig);
    if (err != IOT_OK && err != IOT_ERR_INVALID_STATE) // Already registered is OK
    {
        IOT_LOGE(TAG, "Failed to register device cache namespace");
        return IOT_ERR_FAIL;
    }
    
    IOT_LOGI(TAG, "Device management initialized");
    return IOT_OK;
}

// ============================================================================
// Identity Info (not cached - rarely accessed)
// ============================================================================

iot_err_t IOT_DaoDeviceSetIdentityInfo(uint16_t eid, IOT_DevIdentityInfo_t *devIdentityInfo)
{
    if (devIdentityInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    uint8_t macType = devIdentityInfo->macType;
    uint8_t macSize = devIdentityInfo->macSize;
    uint8_t *mac = devIdentityInfo->mac;
    uint8_t *devId = devIdentityInfo->devId;

    if (mac == NULL || macSize == 0)
    {
        IOT_LOGE(TAG, "Invalid MAC in identity info");
        return IOT_ERR_INVALID_ARG;
    }

    IOT_LOGI(TAG, "Setting device identity info: Eid=%04x, MacSize=%d", eid, macSize);

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    // Prepare identity data: mactype(1), macsize(1 byte), mac, devId(12 bytes)
    size_t flatDataLen = 1 + 1 + macSize + 12;
    uint8_t *flatData = Mem_SafeMalloc(flatDataLen, TAG, "identity flat data");
    if (flatData == NULL)
    {
        return IOT_ERR_NO_MEM;
    }

    flatData[0] = macType;
    flatData[1] = macSize;
    memcpy(&flatData[2], mac, macSize);
    memcpy(&flatData[2 + macSize], devId, 12);

    iot_err_t storageErr = IOT_DaoStorageSet(DEVICE_IDENTITY_INFO_NAMESPACE, key, IOT_DAO_BLOB, flatData, flatDataLen);
    SAFE_FREE(flatData);

    if (storageErr != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to set device identity info for Eid %04x", eid);
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_DaoDeviceSetIdentityInfoAsync(uint16_t eid, IOT_DevIdentityInfo_t *devIdentityInfo)
{
    if (devIdentityInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    uint8_t macType = devIdentityInfo->macType;
    uint8_t macSize = devIdentityInfo->macSize;
    uint8_t *mac = devIdentityInfo->mac;
    uint8_t *devId = devIdentityInfo->devId;

    if (mac == NULL || macSize == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    // Prepare identity data
    size_t flatDataLen = 1 + 1 + macSize + 12;
    uint8_t *flatData = Mem_SafeMalloc(flatDataLen, TAG, "identity flat data");
    if (flatData == NULL)
    {
        return IOT_ERR_NO_MEM;
    }

    flatData[0] = macType;
    flatData[1] = macSize;
    memcpy(&flatData[2], mac, macSize);
    memcpy(&flatData[2 + macSize], devId, 12);

    iot_err_t storageErr = IOT_DaoStorageSetAsync(DEVICE_IDENTITY_INFO_NAMESPACE, key, IOT_DAO_BLOB, flatData, flatDataLen);
    SAFE_FREE(flatData); // DAO_SetDataAsync copies the data internally

    if (storageErr != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to queue async set identity info for Eid %04x", eid);
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_DaoDeviceGetIdentityInfo(uint16_t eid, IOT_DevIdentityInfo_t *outDevIdentityInfo)
{
    if (outDevIdentityInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    // Initialize output
    memset(outDevIdentityInfo, 0, sizeof(IOT_DevIdentityInfo_t));

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    uint8_t *flatData = NULL;
    uint16_t flatDataLen = 0;
    iot_err_t storageErr =
        IOT_DaoStorageGet(DEVICE_IDENTITY_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void **) &flatData, &flatDataLen);
    if (storageErr == IOT_ERR_NOT_FOUND)
    {
        IOT_LOGW(TAG, "Device identity info for Eid %04x not found", eid);
        return IOT_ERR_DATA_NOT_FOUND;
    }
    else if (storageErr != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get device identity info for Eid %04x", eid);
        return IOT_ERR_FAIL;
    }

    // Validate minimum length: macType(1) + macSize(1) + devId(12)
    if (flatDataLen < 1 + 1 + 12)
    {
        SAFE_FREE(flatData);
        return IOT_ERR_FAIL;
    }

    uint8_t macType = flatData[0];
    uint8_t macSize = flatData[1];

    // Validate total length
    if (flatDataLen != 1 + 1 + macSize + 12)
    {
        SAFE_FREE(flatData);
        return IOT_ERR_FAIL;
    }

    outDevIdentityInfo->macType = macType;
    outDevIdentityInfo->macSize = macSize;
    outDevIdentityInfo->mac = Mem_SafeMalloc(macSize, TAG, "identity mac");
    if (outDevIdentityInfo->mac == NULL)
    {
        SAFE_FREE(flatData);
        return IOT_ERR_NO_MEM;
    }
    memcpy(outDevIdentityInfo->mac, &flatData[2], macSize);
    memcpy(outDevIdentityInfo->devId, &flatData[2 + macSize], 12);

    SAFE_FREE(flatData);
    return IOT_OK;
}

// ============================================================================
// Common Info (not cached - rarely accessed)
// ============================================================================

iot_err_t IOT_DaoDeviceSetCommonInfo(uint16_t eid, IOT_DevCommonInfo_t *devCommonInfo)
{
    if (devCommonInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    IOT_LOGI(TAG, "Setting device common info: Eid=%04x", eid);

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    iot_err_t storageErr = IOT_DaoStorageSet(
        DEVICE_COMMON_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void *) devCommonInfo, sizeof(IOT_DevCommonInfo_t));
    if (storageErr != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to set device common info for Eid %04x", eid);
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

iot_err_t IOT_DaoDeviceSetCommonInfoAsync(uint16_t eid, IOT_DevCommonInfo_t *devCommonInfo)
{
    if (devCommonInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    iot_err_t storageErr = IOT_DaoStorageSetAsync(
        DEVICE_COMMON_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void *) devCommonInfo, sizeof(IOT_DevCommonInfo_t));
    if (storageErr != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to queue async set common info for Eid %04x", eid);
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

iot_err_t IOT_DaoDeviceGetCommonInfo(uint16_t eid, IOT_DevCommonInfo_t *outDevCommonInfo)
{
    if (outDevCommonInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    uint8_t *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t storageErr = IOT_DaoStorageGet(DEVICE_COMMON_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void **) &data, &dataLen);
    if (storageErr == IOT_ERR_NOT_FOUND)
    {
        IOT_LOGW(TAG, "Device common info for Eid %04x not found", eid);
        return IOT_ERR_DATA_NOT_FOUND;
    }
    else if (storageErr != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get device common info for Eid %04x", eid);
        return IOT_ERR_FAIL;
    }

    if (dataLen != sizeof(IOT_DevCommonInfo_t))
    {
        SAFE_FREE(data);
        return IOT_ERR_FAIL;
    }

    memcpy(outDevCommonInfo, data, sizeof(IOT_DevCommonInfo_t));
    SAFE_FREE(data);
    return IOT_OK;
}

// ============================================================================
// Element State (CACHED - frequently accessed)
// ============================================================================

iot_err_t IOT_DaoDeviceAddElementInfo(uint16_t DevEid, IOT_DaoDeviceStateElemState_t *elemInfo)
{
    if (elemInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    iot_err_t iot_err = IOT_OK;
    uint16_t elemId = elemInfo->elemId;
    uint16_t elemType = elemInfo->elemType;
    uint8_t attrCount = elemInfo->numOfAttr;
    IOT_DaoDeviceStateAttr_t *attrList = elemInfo->attrList;

    // IOT_LOGI(TAG,
    //          "Adding element info: DevEid=%04x, ElemId=%04x, ElemType=%04x, AttrCount=%d",
    //          DevEid,
    //          elemId,
    //          elemType,
    //          attrCount);

    char key[8];
    eid_to_key(DevEid, key, sizeof(key));

    // Read old value from cache
    uint8_t *currentState = NULL;
    uint16_t currentStateLen = 0;
    iot_err = readStateBlob(DevEid, key, &currentState, &currentStateLen);
    if (iot_err == IOT_ERR_NOT_FOUND)
    {
        currentState = NULL;
        currentStateLen = 0;
    }
    else if (iot_err != IOT_OK)
    {
        return IOT_ERR_FAIL;
    }

    // Validate attr list
    if (attrCount > 0 && attrList == NULL)
    {
        if (currentState)
            SAFE_FREE(currentState);
        return IOT_ERR_INVALID_ARG;
    }

    // Calculate total additional size for all attributes
    size_t totalAttrBytes = 0;
    for (uint8_t i = 0; i < attrCount; i++)
    {
        uint16_t attrSize = (uint16_t) attrList[i].attrSize;
        // Allow attrSize == 0 for placeholder attributes
        // Only require attrValue if attrSize > 0
        if (attrSize > 0 && attrList[i].attrValue == NULL)
        {
            if (currentState)
                SAFE_FREE(currentState);
            return IOT_ERR_INVALID_ARG;
        }
        totalAttrBytes += 2 + 1 + attrSize; // attrId(2) + size(1) + value
    }

    // Compute new state length
    size_t headerAdd = 2 + 2 + 1; // elemId(2) + elemType(2) + featureCount(1)
    size_t newStateLen_sz = (size_t) currentStateLen + headerAdd + totalAttrBytes;
    if (newStateLen_sz > UINT16_MAX)
    {
        if (currentState)
            SAFE_FREE(currentState);
        return IOT_ERR_FAIL;
    }
    uint16_t newStateLen = (uint16_t) newStateLen_sz;

    uint8_t *newState = Mem_SafeMalloc(newStateLen, TAG, "elem new state");
    if (newState == NULL)
    {
        if (currentState)
            SAFE_FREE(currentState);
        return IOT_ERR_NO_MEM;
    }

    // Copy old data if any
    if (currentState != NULL && currentStateLen > 0)
    {
        memcpy(newState, currentState, currentStateLen);
        SAFE_FREE(currentState);
    }

    // Append new element info
    uint16_t offset = currentStateLen;
    newState[offset] = (elemId >> 8) & 0xFF;
    newState[offset + 1] = elemId & 0xFF;
    newState[offset + 2] = (elemType >> 8) & 0xFF;
    newState[offset + 3] = elemType & 0xFF;
    newState[offset + 4] = attrCount;

    uint16_t writePtr = offset + 5;
    for (uint8_t i = 0; i < attrCount; i++)
    {
        uint16_t attrId = attrList[i].attrId;
        uint16_t attrSize = (uint16_t) attrList[i].attrSize;

        newState[writePtr] = (attrId >> 8) & 0xFF;
        newState[writePtr + 1] = attrId & 0xFF;
        newState[writePtr + 2] = (uint8_t) attrSize;

        // Only copy if there's actual data
        if (attrSize > 0 && attrList[i].attrValue != NULL)
        {
            memcpy(&newState[writePtr + 3], attrList[i].attrValue, attrSize);
        }

        writePtr += 2 + 1 + attrSize;
    }

    // Persist updated state via cache
    iot_err = IOT_CacheSet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, newState, newStateLen);
    if (iot_err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to persist updated element state for DevEid %04x", DevEid);
    }
    else
    {
        budgetDropPending(DevEid); /* the joined blob was built on the held one */
    }
    SAFE_FREE(newState);

    return iot_err;
}

// Parse flat blob into structured form
static iot_err_t parseElemsFromBlob(const uint8_t *data, uint16_t dataLen, IOT_DaoDeviceElemsAttrsInfo_t *outInfo)
{
    if (data == NULL || outInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    if (dataLen == 0)
    {
        outInfo->numOfElem = 0;
        outInfo->elemList = NULL;
        return IOT_OK;
    }

    // First pass: count elements
    uint16_t elemCount = 0;
    uint16_t offset = 0;

    while (offset < dataLen)
    {
        if (offset + 5 > dataLen)
            return IOT_ERR_FAIL;

        uint8_t featureCount = data[offset + 4];
        uint16_t attrOffset = offset + 5;

        for (uint8_t j = 0; j < featureCount; j++)
        {
            if (attrOffset + 3 > dataLen)
                return IOT_ERR_FAIL;
            uint16_t attrSize = (uint16_t) data[attrOffset + 2];
            if (attrOffset + 3 + attrSize > dataLen)
                return IOT_ERR_FAIL;
            attrOffset += 3 + attrSize;
        }
        elemCount++;
        offset = attrOffset;
    }

    // Allocate
    outInfo->elemList = Mem_SafeCalloc(elemCount, sizeof(IOT_DaoDeviceStateElemState_t), TAG, "parse elem list");
    if (outInfo->elemList == NULL)
        return IOT_ERR_NO_MEM;
    outInfo->numOfElem = elemCount;

    // Second pass: populate
    offset = 0;
    uint16_t elemIdx = 0;

    while (offset < dataLen && elemIdx < elemCount)
    {
        IOT_DaoDeviceStateElemState_t *elem = &outInfo->elemList[elemIdx];

        elem->elemId = (data[offset] << 8) | data[offset + 1];
        elem->elemType = (data[offset + 2] << 8) | data[offset + 3];
        elem->numOfAttr = data[offset + 4];

        uint16_t attrOffset = offset + 5;

        if (elem->numOfAttr > 0)
        {
            elem->attrList = Mem_SafeCalloc(elem->numOfAttr, sizeof(IOT_DaoDeviceStateAttr_t), TAG, "parse attr list");
            if (elem->attrList == NULL)
            {
                IOT_DaoDeviceFreeElemsInfo(outInfo);
                return IOT_ERR_NO_MEM;
            }

            for (uint8_t j = 0; j < elem->numOfAttr; j++)
            {
                elem->attrList[j].attrId = (data[attrOffset] << 8) | data[attrOffset + 1];
                elem->attrList[j].attrSize = data[attrOffset + 2];

                if (elem->attrList[j].attrSize > 0)
                {
                    elem->attrList[j].attrValue = Mem_SafeMalloc(elem->attrList[j].attrSize, TAG, "parse attr value");
                    if (elem->attrList[j].attrValue == NULL)
                    {
                        IOT_DaoDeviceFreeElemsInfo(outInfo);
                        return IOT_ERR_NO_MEM;
                    }
                    memcpy(elem->attrList[j].attrValue, &data[attrOffset + 3], elem->attrList[j].attrSize);
                }
                else
                {
                    elem->attrList[j].attrValue = NULL; // Placeholder, size 0
                }

                attrOffset += 3 + elem->attrList[j].attrSize;
            }
        }
        else
        {
            elem->attrList = NULL;
        }

        offset = attrOffset;
        elemIdx++;
    }

    return IOT_OK;
}

// Serialize structured form back to flat blob
static iot_err_t serializeElemsToBlob(const IOT_DaoDeviceElemsAttrsInfo_t *info, uint8_t **outData, uint16_t *outLen)
{
    if (info == NULL || outData == NULL || outLen == NULL)
        return IOT_ERR_INVALID_ARG;

    *outData = NULL;
    *outLen = 0;

    if (info->numOfElem == 0 || info->elemList == NULL)
    {
        return IOT_OK; // Empty but valid
    }

    // Calculate total size
    size_t totalSize = 0;
    for (uint16_t i = 0; i < info->numOfElem; i++)
    {
        totalSize += 5; // elemId(2) + elemType(2) + numAttr(1)
        for (uint8_t j = 0; j < info->elemList[i].numOfAttr; j++)
        {
            totalSize += 3 + info->elemList[i].attrList[j].attrSize; // attrId(2) + size(1) + value
        }
    }

    if (totalSize > UINT16_MAX)
        return IOT_ERR_FAIL;

    uint8_t *data = Mem_SafeMalloc(totalSize, TAG, "serialize elem blob");
    if (data == NULL)
        return IOT_ERR_NO_MEM;

    uint16_t offset = 0;
    for (uint16_t i = 0; i < info->numOfElem; i++)
    {
        IOT_DaoDeviceStateElemState_t *elem = &info->elemList[i];

        data[offset] = (elem->elemId >> 8) & 0xFF;
        data[offset + 1] = elem->elemId & 0xFF;
        data[offset + 2] = (elem->elemType >> 8) & 0xFF;
        data[offset + 3] = elem->elemType & 0xFF;
        data[offset + 4] = elem->numOfAttr;
        offset += 5;

        for (uint8_t j = 0; j < elem->numOfAttr; j++)
        {
            IOT_DaoDeviceStateAttr_t *attr = &elem->attrList[j];
            data[offset] = (attr->attrId >> 8) & 0xFF;
            data[offset + 1] = attr->attrId & 0xFF;
            data[offset + 2] = (uint8_t) attr->attrSize;

            // Only copy if there's actual data
            if (attr->attrSize > 0 && attr->attrValue != NULL)
            {
                memcpy(&data[offset + 3], attr->attrValue, attr->attrSize);
            }

            offset += 3 + attr->attrSize;
        }
    }

    *outData = data;
    *outLen = (uint16_t) totalSize;
    return IOT_OK;
}

iot_err_t IOT_DaoDeviceUpdateElementState(uint16_t eid, IOT_DaoDeviceElemsAttrsInfo_t *elemInfo)
{
    if (elemInfo == NULL || elemInfo->numOfElem == 0 || elemInfo->elemList == NULL)
        return IOT_ERR_INVALID_ARG;

    iot_err_t iot_err = IOT_OK;
    char key[8];
    eid_to_key(eid, key, sizeof(key));

    // Read current state from cache
    uint8_t *currentState = NULL;
    uint16_t currentStateLen = 0;
    iot_err = readStateBlob(eid, key, &currentState, &currentStateLen);
    if (iot_err == IOT_ERR_NOT_FOUND)
    {
        IOT_LOGW(TAG, "No existing device state for EID %04x", eid);
        return IOT_ERR_DATA_NOT_FOUND;
    }
    else if (iot_err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get current device state for EID %04x", eid);
        return IOT_ERR_FAIL;
    }

    // Parse current state into structured form
    IOT_DaoDeviceElemsAttrsInfo_t currentElemsInfo = {0};
    iot_err = parseElemsFromBlob(currentState, currentStateLen, &currentElemsInfo);
    if (iot_err != IOT_OK)
    {
        SAFE_FREE(currentState);
        return iot_err;
    }
    SAFE_FREE(currentState);
    currentState = NULL;

    uint16_t changedAttrId = 0; // first persisted attribute that changed, for the budget warning
    bool anyUpdated = false;  // an attribute MATCHED (or was added) — drives NOT_FOUND
    bool anyChanged = false;  // the blob would actually differ — drives the NVS write

    // Apply updates to the parsed structure
    for (uint16_t e = 0; e < elemInfo->numOfElem; e++)
    {
        IOT_DaoDeviceStateElemState_t *srcElem = &elemInfo->elemList[e];

        if (srcElem->numOfAttr > 0 && srcElem->attrList == NULL)
        {
            IOT_DaoDeviceFreeElemsInfo(&currentElemsInfo);
            return IOT_ERR_INVALID_ARG;
        }

        // Find matching element in current state
        IOT_DaoDeviceStateElemState_t *dstElem = NULL;
        for (uint16_t i = 0; i < currentElemsInfo.numOfElem; i++)
        {
            if (currentElemsInfo.elemList[i].elemId == srcElem->elemId)
            {
                dstElem = &currentElemsInfo.elemList[i];
                break;
            }
        }

        if (dstElem == NULL)
        {
            IOT_LOGW(TAG, "Element %04x not found in device %04x, skipping", srcElem->elemId, eid);
            continue;
        }

        // Update each attribute
        for (uint8_t a = 0; a < srcElem->numOfAttr; a++)
        {
            IOT_DaoDeviceStateAttr_t *srcAttr = &srcElem->attrList[a];

            // Skip if source has no data to update with
            if (srcAttr->attrValue == NULL || srcAttr->attrSize == 0)
            {
                IOT_LOGD(TAG, "Skipping empty update for attr %04x", srcAttr->attrId);
                continue;
            }

            /* A measured value goes to RAM only: no blob change, no flash write. */
            if (!IOT_AttrIsPersisted(srcAttr->attrId))
            {
                iot_err_t oe = ovlSet(eid, srcElem->elemId, srcAttr->attrId, srcAttr->attrValue, srcAttr->attrSize);
                if (oe != IOT_OK)
                {
                    IOT_DaoDeviceFreeElemsInfo(&currentElemsInfo);
                    return oe;
                }
                anyUpdated = true;
                continue;
            }

            // Find matching attribute
            bool attrFound = false;
            for (uint8_t j = 0; j < dstElem->numOfAttr; j++)
            {
                if (dstElem->attrList[j].attrId == srcAttr->attrId)
                {
                    attrFound = true;

                    /* Does this update actually change anything? State is one blob per
                     * device in a write-through namespace, so an update that sets an
                     * attribute to the value it already holds would serialize a
                     * byte-identical blob and burn an NVS erase cycle for nothing. That is
                     * not a rare case: ACTIVE_BY_TRIGGER re-execution, group/broadcast
                     * control, sync-on-off mirroring and the cloud pushing current state
                     * all re-assert the present value. */
                    bool sizeDiffers = (dstElem->attrList[j].attrSize != srcAttr->attrSize ||
                                        dstElem->attrList[j].attrValue == NULL);
                    if (!sizeDiffers &&
                        memcmp(dstElem->attrList[j].attrValue, srcAttr->attrValue, srcAttr->attrSize) == 0)
                    {
                        /* Matched, so this is NOT a "nothing found" case — but there is
                         * nothing to persist. anyChanged stays false. */
                        anyUpdated = true;
                        IOT_LOGD(TAG, "Attr %04x of elem %04x already holds this value — no write",
                                 srcAttr->attrId, dstElem->elemId);
                        break;
                    }

                    // Reallocate if size changed (including from 0 to non-zero)
                    if (sizeDiffers)
                    {
                        uint8_t *newValue = Mem_SafeMalloc(srcAttr->attrSize, TAG, "update attr value");
                        if (newValue == NULL)
                        {
                            IOT_DaoDeviceFreeElemsInfo(&currentElemsInfo);
                            return IOT_ERR_NO_MEM;
                        }
                        // Free old value if it exists
                        if (dstElem->attrList[j].attrValue != NULL)
                        {
                            SAFE_FREE(dstElem->attrList[j].attrValue);
                        }
                        dstElem->attrList[j].attrValue = newValue;
                        dstElem->attrList[j].attrSize = srcAttr->attrSize;
                    }
                    memcpy(dstElem->attrList[j].attrValue, srcAttr->attrValue, srcAttr->attrSize);
                    anyUpdated = true;
                    anyChanged = true;
                    if (changedAttrId == 0)
                        changedAttrId = srcAttr->attrId;
                    IOT_LOGD(TAG, "Updated device %04x elem %04x attr %04x (size %d)", 
                             eid, dstElem->elemId, srcAttr->attrId, srcAttr->attrSize);
                    break;
                }
            }
            if (!attrFound)
            {
                IOT_LOGI(TAG, "Attribute %04x not found in element %04x, adding it", srcAttr->attrId, dstElem->elemId);
                
                // Reallocate attribute list to add one more
                uint8_t newCount = dstElem->numOfAttr + 1;
                IOT_DaoDeviceStateAttr_t *newList = Mem_SafeRealloc(dstElem->attrList, 
                                                                  newCount * sizeof(IOT_DaoDeviceStateAttr_t), 
                                                                  TAG, "upsert attr list");
                if (newList == NULL)
                {
                    IOT_LOGE(TAG, "Failed to reallocate attribute list for upsert");
                    IOT_DaoDeviceFreeElemsInfo(&currentElemsInfo);
                    return IOT_ERR_NO_MEM;
                }
                
                dstElem->attrList = newList;
                dstElem->attrList[dstElem->numOfAttr].attrId = srcAttr->attrId;
                dstElem->attrList[dstElem->numOfAttr].attrSize = srcAttr->attrSize;
                dstElem->attrList[dstElem->numOfAttr].attrValue = Mem_SafeMalloc(srcAttr->attrSize, TAG, "upsert attr val");
                
                if (dstElem->attrList[dstElem->numOfAttr].attrValue == NULL)
                {
                    // Note: currentElemsInfo will free the partially updated list correctly
                    IOT_DaoDeviceFreeElemsInfo(&currentElemsInfo);
                    return IOT_ERR_NO_MEM;
                }
                
                memcpy(dstElem->attrList[dstElem->numOfAttr].attrValue, srcAttr->attrValue, srcAttr->attrSize);
                dstElem->numOfAttr = newCount;
                anyUpdated = true;
                anyChanged = true;
            }
        }
    }

    if (!anyUpdated)
    {
        IOT_LOGW(TAG, "No attributes were updated for device %04x", eid);
        IOT_DaoDeviceFreeElemsInfo(&currentElemsInfo);
        return IOT_ERR_NOT_FOUND;
    }

    /* Everything matched but every value was already what was asked for: the serialized
     * blob would be byte-identical. Skip the write — but still return IOT_OK, because the
     * requested state IS in effect. Callers use the return only to detect "not found", and
     * the state-changed report to the cloud is driven by the caller, not by this write. */
    if (!anyChanged)
    {
        IOT_LOGD(TAG, "Device %04x: no attribute value changed — skipping NVS write", eid);
        IOT_DaoDeviceFreeElemsInfo(&currentElemsInfo);
        return IOT_OK;
    }

    // Serialize back to blob
    uint8_t *newState = NULL;
    uint16_t newStateLen = 0;
    iot_err = serializeElemsToBlob(&currentElemsInfo, &newState, &newStateLen);
    IOT_DaoDeviceFreeElemsInfo(&currentElemsInfo);

    if (iot_err != IOT_OK)
    {
        return iot_err;
    }

    // Persist, or hold when this device is over its write budget
    iot_err = persistStateBlob(eid, key, newState, newStateLen, changedAttrId);
    SAFE_FREE(newState);

    return iot_err;
}

iot_err_t IOT_DaoDeviceUpsertAttribute(uint16_t devEid, uint16_t elemId, uint16_t attrId, const uint8_t *attrData, uint16_t attrDataLen)
{
    IOT_DaoDeviceStateAttr_t attr = {
        .attrId = attrId,
        .attrSize = attrDataLen,
        .attrValue = (uint8_t *)attrData
    };
    IOT_DaoDeviceStateElemState_t elem = {
        .elemId = elemId,
        .numOfAttr = 1,
        .attrList = &attr
    };
    IOT_DaoDeviceElemsAttrsInfo_t info = {
        .numOfElem = 1,
        .elemList = &elem
    };
    
    return IOT_DaoDeviceUpdateElementState(devEid, &info);
}

iot_err_t IOT_DaoDeviceUpsertAttributeAsync(uint16_t devEid, uint16_t elemId, uint16_t attrId, const uint8_t *attrData, uint16_t attrDataLen)
{
    // Synchronous, like IOT_DaoDeviceUpdateElementState: the cache is written to storage later
    return IOT_DaoDeviceUpsertAttribute(devEid, elemId, attrId, attrData, attrDataLen);
}

iot_err_t IOT_DaoDeviceGetDeviceElemsState(uint16_t DevEid, IOT_DaoDeviceElemsAttrsInfo_t *outElemsInfo)
{
    if (outElemsInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    iot_err_t iot_err = IOT_OK;
    outElemsInfo->numOfElem = 0;
    outElemsInfo->elemList = NULL;

    char key[8];
    eid_to_key(DevEid, key, sizeof(key));

    // Read from cache (not direct DAO)
    uint8_t *currentState = NULL;
    uint16_t currentStateLen = 0;
    iot_err = readStateBlob(DevEid, key, &currentState, &currentStateLen);
    if (iot_err == IOT_ERR_NOT_FOUND)
    {
        IOT_LOGW(TAG, "No existing device state for EID %04x", DevEid);
        return IOT_ERR_DATA_NOT_FOUND;
    }
    else if (iot_err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get current device state for EID %04x", DevEid);
        return IOT_ERR_FAIL;
    }

    // First pass: count elements
    uint16_t elemCount = 0;
    uint16_t offset = 0;
    while (offset < currentStateLen)
    {
        if (offset + 5 > currentStateLen)
        {
            IOT_LOGE(TAG, "Truncated element header at offset %u", offset);
            SAFE_FREE(currentState);
            return IOT_ERR_FAIL;
        }

        uint8_t featureCount = currentState[offset + 4];
        uint16_t attrOffset = offset + 5;

        for (uint8_t j = 0; j < featureCount; j++)
        {
            if (attrOffset + 3 > currentStateLen)
            {
                SAFE_FREE(currentState);
                return IOT_ERR_FAIL;
            }
            uint16_t attrSize = (uint16_t) currentState[attrOffset + 2];
            if (attrOffset + 3 + attrSize > currentStateLen)
            {
                SAFE_FREE(currentState);
                return IOT_ERR_FAIL;
            }
            attrOffset += 2 + 1 + attrSize;
        }
        elemCount++;
        offset = attrOffset;
    }

    // Allocate elemList
    outElemsInfo->elemList = Mem_SafeCalloc(elemCount, sizeof(IOT_DaoDeviceStateElemState_t), TAG, "get elem list");
    if (outElemsInfo->elemList == NULL)
    {
        SAFE_FREE(currentState);
        return IOT_ERR_NO_MEM;
    }

    // Second pass: populate elemList
    outElemsInfo->numOfElem = 0;
    offset = 0;
    while (offset < currentStateLen && outElemsInfo->numOfElem < elemCount)
    {
        if (offset + 5 > currentStateLen)
        {
            IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
            SAFE_FREE(currentState);
            return IOT_ERR_FAIL;
        }

        uint16_t elemId = (currentState[offset] << 8) | currentState[offset + 1];
        uint16_t elemType = (currentState[offset + 2] << 8) | currentState[offset + 3];
        uint8_t featureCount = currentState[offset + 4];
        uint16_t attrOffset = offset + 5;

        IOT_DaoDeviceStateElemState_t *elemInfoOut = &outElemsInfo->elemList[outElemsInfo->numOfElem];
        elemInfoOut->elemId = elemId;
        elemInfoOut->elemType = elemType;
        elemInfoOut->numOfAttr = featureCount;
        elemInfoOut->attrList = NULL;

        /* Counted now, so a failure below frees this element too (its attrList is calloc'd,
         * so unfilled entries are NULL and free cleanly). */
        outElemsInfo->numOfElem++;

        // Allocate attrList
        if (featureCount > 0)
        {
            elemInfoOut->attrList = Mem_SafeCalloc(featureCount, sizeof(IOT_DaoDeviceStateAttr_t), TAG, "get attr list");
            if (elemInfoOut->attrList == NULL)
            {
                elemInfoOut->numOfAttr = 0;
                IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
                SAFE_FREE(currentState);
                return IOT_ERR_NO_MEM;
            }
        }

        // Populate attributes
        for (uint8_t j = 0; j < featureCount; j++)
        {
            if (attrOffset + 3 > currentStateLen)
            {
                IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
                SAFE_FREE(currentState);
                return IOT_ERR_FAIL;
            }

            uint16_t attrId = (currentState[attrOffset] << 8) | currentState[attrOffset + 1];
            uint16_t attrSize = (uint16_t) currentState[attrOffset + 2];

            if (attrOffset + 3 + attrSize > currentStateLen)
            {
                IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
                SAFE_FREE(currentState);
                return IOT_ERR_FAIL;
            }

            elemInfoOut->attrList[j].attrId = attrId;
            elemInfoOut->attrList[j].attrSize = attrSize;

            if (attrSize > 0)
            {
                elemInfoOut->attrList[j].attrValue = Mem_SafeMalloc(attrSize, TAG, "get attr value");
                if (elemInfoOut->attrList[j].attrValue == NULL)
                {
                    IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
                    SAFE_FREE(currentState);
                    return IOT_ERR_NO_MEM;
                }
                memcpy(elemInfoOut->attrList[j].attrValue, &currentState[attrOffset + 3], attrSize);
            }
            else
            {
                elemInfoOut->attrList[j].attrValue = NULL; // Placeholder, size 0
            }

            attrOffset += 2 + 1 + attrSize;
        }

        offset = attrOffset;
    }

    SAFE_FREE(currentState);
    iot_err = ovlMerge(DevEid, outElemsInfo);
    if (iot_err != IOT_OK)
        IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
    return iot_err;
}

iot_err_t IOT_DaoDeviceGetElementType(uint16_t devEid, uint16_t elemId, uint16_t *outElemType)
{
    if (outElemType == NULL)
        return IOT_ERR_INVALID_ARG;

    char key[8];
    eid_to_key(devEid, key, sizeof(key));

    // Read from cache
    uint8_t *currentState = NULL;
    uint16_t currentStateLen = 0;
    iot_err_t iot_err =
        IOT_CacheGet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void **) &currentState, &currentStateLen);
    if (iot_err == IOT_ERR_NOT_FOUND)
    {
        IOT_LOGW(TAG, "No existing device state for EID %04x", devEid);
        return IOT_ERR_DATA_NOT_FOUND;
    }
    else if (iot_err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get current device state for EID %04x", devEid);
        return IOT_ERR_FAIL;
    }

    // Search for element by elemId
    uint16_t offset = 0;
    while (offset < currentStateLen)
    {
        if (offset + 5 > currentStateLen)
        {
            SAFE_FREE(currentState);
            return IOT_ERR_FAIL;
        }

        uint16_t currentElemId = (currentState[offset] << 8) | currentState[offset + 1];
        uint16_t currentElemType = (currentState[offset + 2] << 8) | currentState[offset + 3];
        uint8_t featureCount = currentState[offset + 4];

        if (currentElemId == elemId)
        {
            *outElemType = currentElemType;
            SAFE_FREE(currentState);
            return IOT_OK;
        }

        // Skip to next element
        uint16_t attrOffset = offset + 5;
        for (uint8_t j = 0; j < featureCount; j++)
        {
            if (attrOffset + 3 > currentStateLen)
            {
                SAFE_FREE(currentState);
                return IOT_ERR_FAIL;
            }
            uint16_t attrSize = (uint16_t) currentState[attrOffset + 2];
            attrOffset += 2 + 1 + attrSize;
        }
        offset = attrOffset;
    }

    SAFE_FREE(currentState);
    return IOT_ERR_DATA_NOT_FOUND; // Element not found
}

// ============================================================================
// Mesh Key Management
// ============================================================================

iot_err_t IOT_DaoDeviceGetMeshKey(uint16_t eid, uint8_t *outKey, uint8_t outKeyCap, uint8_t *outKeyLen)
{
    if (outKey == NULL || outKeyLen == NULL)
        return IOT_ERR_INVALID_ARG;

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    uint8_t *data = NULL;
    uint16_t dataLen = 0;
    iot_err_t res = IOT_DaoStorageGet(DEVICE_MESH_DEVKEY_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void **) &data, &dataLen);
    if (res == IOT_ERR_NOT_FOUND || data == NULL)
    {
        return IOT_ERR_DATA_NOT_FOUND;
    }
    if (res != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get mesh dev key for Eid %04x", eid);
        return IOT_ERR_FAIL;
    }

    // Bound the copy against the caller's buffer; a buffer smaller than the stored blob
    // returns IOT_ERR_NO_MEM.
    if (dataLen > outKeyCap)
    {
        IOT_LOGE(TAG, "Mesh dev key for Eid %04x is %u bytes, caller buffer holds %u", eid,
                 (unsigned) dataLen, (unsigned) outKeyCap);
        SAFE_FREE(data);
        return IOT_ERR_NO_MEM;
    }

    memcpy(outKey, data, dataLen);
    *outKeyLen = (uint8_t) dataLen;
    SAFE_FREE(data);
    return IOT_OK;
}

iot_err_t IOT_DaoDeviceSetMeshKey(uint16_t eid, uint8_t *meshKey, uint8_t keyLen)
{
    if (meshKey == NULL || keyLen == 0)
        return IOT_ERR_INVALID_ARG;

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    iot_err_t res = IOT_DaoStorageSet(DEVICE_MESH_DEVKEY_INFO_NAMESPACE, key, IOT_DAO_BLOB, meshKey, keyLen);
    if (res != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to set mesh dev key for Eid %04x", eid);
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_DaoDeviceSetMeshKeyAsync(uint16_t eid, uint8_t *meshKey, uint8_t keyLen)
{
    if (meshKey == NULL || keyLen == 0)
        return IOT_ERR_INVALID_ARG;

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    iot_err_t res = IOT_DaoStorageSetAsync(DEVICE_MESH_DEVKEY_INFO_NAMESPACE, key, IOT_DAO_BLOB, meshKey, keyLen);
    if (res != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to queue async set mesh dev key for Eid %04x", eid);
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

// ============================================================================
// Device Removal
// ============================================================================

iot_err_t IOT_DaoDeviceRemoveData(uint16_t eid, IOT_DaoDeviceDataType_t dataType)
{
    iot_err_t res = IOT_OK;
    char key[8];
    eid_to_key(eid, key, sizeof(key));

    if (dataType == DEVICE_DATA_ID || dataType == DEVICE_DATA_ALL)
    {
        res = IOT_DaoStorageRemoveByKey(DEVICE_IDENTITY_INFO_NAMESPACE, key);
        if (res != IOT_OK && res != IOT_ERR_NOT_FOUND)
        {
            IOT_LOGE(TAG, "Failed to remove device identity data for Eid %04x", eid);
            return IOT_ERR_FAIL;
        }
    }

    if (dataType == DEVICE_DATA_COMMON || dataType == DEVICE_DATA_ALL)
    {
        res = IOT_DaoStorageRemoveByKey(DEVICE_COMMON_INFO_NAMESPACE, key);
        if (res != IOT_OK && res != IOT_ERR_NOT_FOUND)
        {
            IOT_LOGE(TAG, "Failed to remove device common info data for Eid %04x", eid);
            return IOT_ERR_FAIL;
        }
    }

    if (dataType == DEVICE_DATA_ELEM_STATE || dataType == DEVICE_DATA_ALL)
    {
        // Invalidate cache entry first
        IOT_CacheInvalidate(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key);
        ovlDrop(eid);
        budgetDrop(eid);

        res = IOT_DaoStorageRemoveByKey(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key);
        if (res != IOT_OK && res != IOT_ERR_NOT_FOUND)
        {
            IOT_LOGE(TAG, "Failed to remove device elements state data for Eid %04x", eid);
            return IOT_ERR_FAIL;
        }
    }

    if (dataType == DEVICE_DATA_MESH_DEVKEY || dataType == DEVICE_DATA_ALL)
    {
        res = IOT_DaoStorageRemoveByKey(DEVICE_MESH_DEVKEY_INFO_NAMESPACE, key);
        if (res != IOT_OK && res != IOT_ERR_NOT_FOUND)
        {
            IOT_LOGE(TAG, "Failed to remove mesh dev key for Eid %04x", eid);
            return IOT_ERR_FAIL;
        }
    }

    if (dataType == DEVICE_DATA_LOG || dataType == DEVICE_DATA_ALL)
    {
        // TODO: implement remove device log data logic
    }

    return IOT_OK;
}

iot_err_t IOT_DaoDeviceRemoveDevice(uint16_t eid)
{
    return IOT_DaoDeviceRemoveData(eid, DEVICE_DATA_ALL);
}

iot_err_t IOT_DaoDeviceRemoveDataAsync(uint16_t eid, IOT_DaoDeviceDataType_t dataType)
{
    char key[8];
    eid_to_key(eid, key, sizeof(key));

    if (dataType == DEVICE_DATA_ID || dataType == DEVICE_DATA_ALL)
    {
        IOT_DaoStorageRemoveByKeyAsync(DEVICE_IDENTITY_INFO_NAMESPACE, key);
    }

    if (dataType == DEVICE_DATA_COMMON || dataType == DEVICE_DATA_ALL)
    {
        IOT_DaoStorageRemoveByKeyAsync(DEVICE_COMMON_INFO_NAMESPACE, key);
    }

    if (dataType == DEVICE_DATA_ELEM_STATE || dataType == DEVICE_DATA_ALL)
    {
        // Invalidate cache entry
        IOT_CacheInvalidate(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key);
        ovlDrop(eid);
        budgetDrop(eid);
        IOT_DaoStorageRemoveByKeyAsync(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key);
    }

    if (dataType == DEVICE_DATA_MESH_DEVKEY || dataType == DEVICE_DATA_ALL)
    {
        IOT_DaoStorageRemoveByKeyAsync(DEVICE_MESH_DEVKEY_INFO_NAMESPACE, key);
    }

    return IOT_OK;
}

// ============================================================================
// Device Lookup
// ============================================================================

bool IOT_DaoDeviceIsDeviceExistByMac(uint8_t *mac, uint16_t *outEid)
{
    if (mac == NULL)
        return false;

    uint16_t eid = 0;
    iot_err_t res = getEidByMac(mac, 6, &eid);
    if (res != IOT_OK)
    {
        return false;
    }

    if (outEid != NULL)
    {
        *outEid = eid;
    }
    return true;
}

bool IOT_DaoDeviceGetEidByDevId(uint8_t *devId, uint16_t *outEid)
{
    if (devId == NULL || outEid == NULL)
        return false;

    char **eidKeyList = NULL;
    uint16_t eidCount = 0;
    uint8_t **dataList = NULL;
    uint16_t *dataSizes = NULL;

    iot_err_t storageErr =
        IOT_DaoStorageGetNamespaceData(DEVICE_IDENTITY_INFO_NAMESPACE, &eidKeyList, &eidCount, (void ***) &dataList, &dataSizes);
    if (storageErr != IOT_OK || eidCount == 0)
    {
        return false;
    }

    bool found = false;
    for (uint16_t i = 0; i < eidCount; i++)
    {
        if (dataList == NULL || dataList[i] == NULL)
            continue;

        uint8_t *flatData = (uint8_t *) dataList[i];
        // Layout: [macType(1), macSize(1), mac(macSize), devId(12)]
        uint8_t macSize = flatData[1];
        uint16_t devIdOffset = 2 + macSize;

        if (dataSizes[i] < devIdOffset + IOT_DAO_DEVICE_ID_LEN)
            continue;

        if (memcmp(devId, &flatData[devIdOffset], IOT_DAO_DEVICE_ID_LEN) == 0)
        {
            *outEid = (uint16_t) atoi(eidKeyList[i]);
            found = true;
            break;
        }
    }

    for (uint16_t j = 0; j < eidCount; j++)
    {
        if (dataList && dataList[j])
            SAFE_FREE(dataList[j]);
        if (eidKeyList && eidKeyList[j])
            SAFE_FREE(eidKeyList[j]);
    }
    if (dataList)
        SAFE_FREE(dataList);
    if (dataSizes)
        SAFE_FREE(dataSizes);
    if (eidKeyList)
        SAFE_FREE(eidKeyList);

    return found;
}

// ============================================================================
// Cache Control
// ============================================================================

iot_err_t IOT_DaoDeviceFlushCache(void)
{
    return IOT_CacheFlush(DEVICE_ELEMS_STATE_INFO_NAMESPACE);
}

iot_err_t IOT_DaoDeviceInvalidateCache(void)
{
    return IOT_CacheInvalidate(DEVICE_ELEMS_STATE_INFO_NAMESPACE, NULL);
}

iot_err_t IOT_DaoDeviceFactoryReset(void){
    iot_err_t iot_err = IOT_OK;

    ovlDrop(0xFFFF);
    budgetDrop(0xFFFF);

    // Remove all namespaces related to device data
    iot_err_t res = IOT_DaoStorageRemoveNamespace(DEVICE_IDENTITY_INFO_NAMESPACE);
    if (res != IOT_OK && res != IOT_ERR_NOT_FOUND)
    {
        IOT_LOGE(TAG, "Failed to remove DEVICE_IDENTITY_INFO_NAMESPACE");
        iot_err = IOT_ERR_FAIL;
    }

    res = IOT_DaoStorageRemoveNamespace(DEVICE_COMMON_INFO_NAMESPACE);
    if (res != IOT_OK && res != IOT_ERR_NOT_FOUND)
    {
        IOT_LOGE(TAG, "Failed to remove DEVICE_COMMON_INFO_NAMESPACE");
        iot_err = IOT_ERR_FAIL;
    }

    res = IOT_DaoStorageRemoveNamespace(DEVICE_ELEMS_STATE_INFO_NAMESPACE);
    if (res != IOT_OK && res != IOT_ERR_NOT_FOUND)
    {
        IOT_LOGE(TAG, "Failed to remove DEVICE_ELEMS_STATE_INFO_NAMESPACE");
        iot_err = IOT_ERR_FAIL;
    }

    res = IOT_DaoStorageRemoveNamespace(DEVICE_MESH_DEVKEY_INFO_NAMESPACE);
    if (res != IOT_OK && res != IOT_ERR_NOT_FOUND)
    {
        IOT_LOGE(TAG, "Failed to remove DEVICE_MESH_DEVKEY_INFO_NAMESPACE");
        iot_err = IOT_ERR_FAIL;
    }

    // Flush cache
    IOT_DaoDeviceFlushCache();

    return iot_err;
}

iot_err_t IOT_DaoDeviceGetAllEids(uint16_t **outEids, uint16_t *outCount)
{
    if (outEids == NULL || outCount == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    *outEids = NULL;
    *outCount = 0;

    char **keyList = NULL;
    uint16_t keyCount = 0;
    void **dataList = NULL;
    uint16_t *dataSizes = NULL;

    iot_err_t err = IOT_DaoStorageGetNamespaceData(DEVICE_IDENTITY_INFO_NAMESPACE,
                                          &keyList, &keyCount, &dataList, &dataSizes);
    if (err != IOT_OK || keyCount == 0)
    {
        return IOT_ERR_NOT_FOUND;
    }

    uint16_t *eids = (uint16_t *) Mem_SafeMalloc(keyCount * sizeof(uint16_t), TAG, "eid list");
    if (eids == NULL)
    {
        goto cleanup;
    }

    uint16_t count = 0;
    for (uint16_t i = 0; i < keyCount; i++)
    {
        if (keyList[i] != NULL)
        {
            eids[count++] = (uint16_t) strtol(keyList[i], NULL, 10);
        }
    }

    *outEids = eids;
    *outCount = count;

cleanup:
    for (uint16_t i = 0; i < keyCount; i++)
    {
        SAFE_FREE(keyList[i]);
        if (dataList != NULL)
        {
            SAFE_FREE(dataList[i]);
        }
    }
    SAFE_FREE(keyList);
    SAFE_FREE(dataList);
    SAFE_FREE(dataSizes);

    return (eids != NULL) ? IOT_OK : IOT_ERR_NO_MEM;
}

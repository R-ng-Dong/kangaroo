#include "dao/IOT_DaoDeviceMgmt.h"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include "IOT_Log.h"
#include "dao/IOT_DaoStorage.h"
#include "dao/IOT_Cached.h"
#include "IOT_Memory.h"

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
                    free(dataList[j]);
                if (eidKeyList && eidKeyList[j])
                    free(eidKeyList[j]);
            }
            if (dataList)
                free(dataList);
            if (dataSizes)
                free(dataSizes);
            if (eidKeyList)
                free(eidKeyList);
            return IOT_OK;
        }
    }

    // Not found: free allocated memory
    for (uint16_t j = 0; j < eidCount; j++)
    {
        if (dataList && dataList[j])
            free(dataList[j]);
        if (eidKeyList && eidKeyList[j])
            free(eidKeyList[j]);
    }
    if (dataList)
        free(dataList);
    if (dataSizes)
        free(dataSizes);
    if (eidKeyList)
        free(eidKeyList);

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
        free(info->mac);
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
                        free(elem->attrList[j].attrValue);
                        elem->attrList[j].attrValue = NULL;
                    }
                }
                free(elem->attrList);
                elem->attrList = NULL;
            }
            elem->numOfAttr = 0;
        }
        free(elemsInfo->elemList);
        elemsInfo->elemList = NULL;
    }
    elemsInfo->numOfElem = 0;
}

// ============================================================================
// Initialization
// ============================================================================

iot_err_t IOT_DaoDeviceInit(void)
{
    IOT_LOGI(TAG, "Initializing device management");

    // Register element state namespace with cache (frequently accessed)
    const IOT_CacheConfig_t nsConfig = {.namespace = DEVICE_ELEMS_STATE_INFO_NAMESPACE,
                                        .maxEntries = MAX_DEVICE_CACHE,
                                        .flushIntervalMs = 0, // Manual flush only
                                        .policy = IOT_CACHE_WRITE_THROUGH};

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
    uint8_t *flatData = malloc(flatDataLen);
    if (flatData == NULL)
    {
        return IOT_ERR_NO_MEM;
    }

    flatData[0] = macType;
    flatData[1] = macSize;
    memcpy(&flatData[2], mac, macSize);
    memcpy(&flatData[2 + macSize], devId, 12);

    iot_err_t storageErr = IOT_DaoStorageSet(DEVICE_IDENTITY_INFO_NAMESPACE, key, IOT_DAO_BLOB, flatData, flatDataLen);
    free(flatData);

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
    uint8_t *flatData = malloc(flatDataLen);
    if (flatData == NULL)
    {
        return IOT_ERR_NO_MEM;
    }

    flatData[0] = macType;
    flatData[1] = macSize;
    memcpy(&flatData[2], mac, macSize);
    memcpy(&flatData[2 + macSize], devId, 12);

    iot_err_t storageErr = IOT_DaoStorageSetAsync(DEVICE_IDENTITY_INFO_NAMESPACE, key, IOT_DAO_BLOB, flatData, flatDataLen);
    free(flatData); // DAO_SetDataAsync copies the data internally

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
        free(flatData);
        return IOT_ERR_FAIL;
    }

    uint8_t macType = flatData[0];
    uint8_t macSize = flatData[1];

    // Validate total length
    if (flatDataLen != 1 + 1 + macSize + 12)
    {
        free(flatData);
        return IOT_ERR_FAIL;
    }

    outDevIdentityInfo->macType = macType;
    outDevIdentityInfo->macSize = macSize;
    outDevIdentityInfo->mac = malloc(macSize);
    if (outDevIdentityInfo->mac == NULL)
    {
        free(flatData);
        return IOT_ERR_NO_MEM;
    }
    memcpy(outDevIdentityInfo->mac, &flatData[2], macSize);
    memcpy(outDevIdentityInfo->devId, &flatData[2 + macSize], 12);

    free(flatData);
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
        free(data);
        return IOT_ERR_FAIL;
    }

    memcpy(outDevCommonInfo, data, sizeof(IOT_DevCommonInfo_t));
    free(data);
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
    iot_err =
        IOT_CacheGet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void **) &currentState, &currentStateLen);
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
            free(currentState);
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
                free(currentState);
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
            free(currentState);
        return IOT_ERR_FAIL;
    }
    uint16_t newStateLen = (uint16_t) newStateLen_sz;

    uint8_t *newState = malloc(newStateLen);
    if (newState == NULL)
    {
        if (currentState)
            free(currentState);
        return IOT_ERR_NO_MEM;
    }

    // Copy old data if any
    if (currentState != NULL && currentStateLen > 0)
    {
        memcpy(newState, currentState, currentStateLen);
        free(currentState);
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
    free(newState);

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
    outInfo->elemList = calloc(elemCount, sizeof(IOT_DaoDeviceStateElemState_t));
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
            elem->attrList = calloc(elem->numOfAttr, sizeof(IOT_DaoDeviceStateAttr_t));
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
                    elem->attrList[j].attrValue = malloc(elem->attrList[j].attrSize);
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

    uint8_t *data = malloc(totalSize);
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
    // IOT_LOGI(TAG, "Updating element state: DevEid=%04x, ElemNum=%d", eid, elemInfo->numOfElem);

    char key[8];
    eid_to_key(eid, key, sizeof(key));

    // Read current state from cache
    uint8_t *currentState = NULL;
    uint16_t currentStateLen = 0;
    iot_err =
        IOT_CacheGet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void **) &currentState, &currentStateLen);
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
        free(currentState);
        return iot_err;
    }
    free(currentState);
    currentState = NULL;

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
            IOT_LOGW(TAG, "Element %04x not found, skipping", srcElem->elemId);
            continue;
        }

        // Update each attribute
        for (uint8_t a = 0; a < srcElem->numOfAttr; a++)
        {
            IOT_DaoDeviceStateAttr_t *srcAttr = &srcElem->attrList[a];

            // Skip if source has no data to update with
            if (srcAttr->attrValue == NULL || srcAttr->attrSize == 0)
                continue;

            // Find matching attribute
            for (uint8_t j = 0; j < dstElem->numOfAttr; j++)
            {
                if (dstElem->attrList[j].attrId == srcAttr->attrId)
                {
                    // Reallocate if size changed (including from 0 to non-zero)
                    if (dstElem->attrList[j].attrSize != srcAttr->attrSize)
                    {
                        uint8_t *newValue = malloc(srcAttr->attrSize);
                        if (newValue == NULL)
                        {
                            IOT_DaoDeviceFreeElemsInfo(&currentElemsInfo);
                            return IOT_ERR_NO_MEM;
                        }
                        // Free old value if it exists
                        if (dstElem->attrList[j].attrValue != NULL)
                        {
                            free(dstElem->attrList[j].attrValue);
                        }
                        dstElem->attrList[j].attrValue = newValue;
                        dstElem->attrList[j].attrSize = srcAttr->attrSize;
                    }
                    memcpy(dstElem->attrList[j].attrValue, srcAttr->attrValue, srcAttr->attrSize);
                    break;
                }
            }
        }
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

    // Persist
    iot_err = IOT_CacheSet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, newState, newStateLen);
    free(newState);

    return iot_err;
}

iot_err_t IOT_DaoDeviceUpdateElementStateAsync(uint16_t eid, IOT_DaoDeviceElemsAttrsInfo_t *elemInfo)
{
    // For async update, we need to read current state first (blocking),
    // then queue the write. This is a limitation - true async would need
    // the DAO task to handle the read-modify-write cycle.
    //
    // For now, we do the update synchronously but queue just the final write.
    // This is still useful for non-time-critical updates from callbacks.

    if (elemInfo == NULL || elemInfo->numOfElem == 0 || elemInfo->elemList == NULL)
        return IOT_ERR_INVALID_ARG;

    // Note: This implementation does a blocking read + async write
    // A fully async version would require more complex queue handling

    // IOT_LOGW(TAG, "UpdateElementStateAsync: performing sync read + async write");

    // For simplicity, just call the sync version
    // TODO: Implement true async read-modify-write if needed
    return IOT_DaoDeviceUpdateElementState(eid, elemInfo);
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
    iot_err =
        IOT_CacheGet(DEVICE_ELEMS_STATE_INFO_NAMESPACE, key, IOT_DAO_BLOB, (void **) &currentState, &currentStateLen);
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
            free(currentState);
            return IOT_ERR_FAIL;
        }

        uint8_t featureCount = currentState[offset + 4];
        uint16_t attrOffset = offset + 5;

        for (uint8_t j = 0; j < featureCount; j++)
        {
            if (attrOffset + 3 > currentStateLen)
            {
                free(currentState);
                return IOT_ERR_FAIL;
            }
            uint16_t attrSize = (uint16_t) currentState[attrOffset + 2];
            if (attrOffset + 3 + attrSize > currentStateLen)
            {
                free(currentState);
                return IOT_ERR_FAIL;
            }
            attrOffset += 2 + 1 + attrSize;
        }
        elemCount++;
        offset = attrOffset;
    }

    // Allocate elemList
    outElemsInfo->elemList = calloc(elemCount, sizeof(IOT_DaoDeviceStateElemState_t));
    if (outElemsInfo->elemList == NULL)
    {
        free(currentState);
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
            free(currentState);
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

        // Allocate attrList
        if (featureCount > 0)
        {
            elemInfoOut->attrList = calloc(featureCount, sizeof(IOT_DaoDeviceStateAttr_t));
            if (elemInfoOut->attrList == NULL)
            {
                IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
                free(currentState);
                return IOT_ERR_NO_MEM;
            }
        }

        // Populate attributes
        for (uint8_t j = 0; j < featureCount; j++)
        {
            if (attrOffset + 3 > currentStateLen)
            {
                IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
                free(currentState);
                return IOT_ERR_FAIL;
            }

            uint16_t attrId = (currentState[attrOffset] << 8) | currentState[attrOffset + 1];
            uint16_t attrSize = (uint16_t) currentState[attrOffset + 2];

            if (attrOffset + 3 + attrSize > currentStateLen)
            {
                IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
                free(currentState);
                return IOT_ERR_FAIL;
            }

            elemInfoOut->attrList[j].attrId = attrId;
            elemInfoOut->attrList[j].attrSize = attrSize;

            if (attrSize > 0)
            {
                elemInfoOut->attrList[j].attrValue = malloc(attrSize);
                if (elemInfoOut->attrList[j].attrValue == NULL)
                {
                    IOT_DaoDeviceFreeElemsInfo(outElemsInfo);
                    free(currentState);
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

        outElemsInfo->numOfElem++;
        offset = attrOffset;
    }

    free(currentState);
    return IOT_OK;
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
            free(currentState);
            return IOT_ERR_FAIL;
        }

        uint16_t currentElemId = (currentState[offset] << 8) | currentState[offset + 1];
        uint16_t currentElemType = (currentState[offset + 2] << 8) | currentState[offset + 3];
        uint8_t featureCount = currentState[offset + 4];

        if (currentElemId == elemId)
        {
            *outElemType = currentElemType;
            free(currentState);
            return IOT_OK;
        }

        // Skip to next element
        uint16_t attrOffset = offset + 5;
        for (uint8_t j = 0; j < featureCount; j++)
        {
            if (attrOffset + 3 > currentStateLen)
            {
                free(currentState);
                return IOT_ERR_FAIL;
            }
            uint16_t attrSize = (uint16_t) currentState[attrOffset + 2];
            attrOffset += 2 + 1 + attrSize;
        }
        offset = attrOffset;
    }

    free(currentState);
    return IOT_ERR_DATA_NOT_FOUND; // Element not found
}

// ============================================================================
// Mesh Key Management
// ============================================================================

iot_err_t IOT_DaoDeviceGetMeshKey(uint16_t eid, uint8_t *outKey, uint8_t *outKeyLen)
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

        if (dataSizes[i] < devIdOffset + DEVICE_ID_LEN)
            continue;

        if (memcmp(devId, &flatData[devIdOffset], DEVICE_ID_LEN) == 0)
        {
            *outEid = (uint16_t) atoi(eidKeyList[i]);
            found = true;
            break;
        }
    }

    for (uint16_t j = 0; j < eidCount; j++)
    {
        if (dataList && dataList[j])
            free(dataList[j]);
        if (eidKeyList && eidKeyList[j])
            free(eidKeyList[j]);
    }
    if (dataList)
        free(dataList);
    if (dataSizes)
        free(dataSizes);
    if (eidKeyList)
        free(eidKeyList);

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

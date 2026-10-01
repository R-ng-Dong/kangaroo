#include "dao/IOT_DaoGroupMgmt.h"
#include "IOT_ErrorManager.h"
#include <string.h>
#include <inttypes.h>
#include "IOT_Log.h"
#include "dao/IOT_DaoStorage.h"
iot_err_t IOT_DaoGroupGetDeviceList(uint16_t groupId, uint8_t **outDeviceList, uint16_t *outGroupListSize)
{
    if (outDeviceList == NULL || outGroupListSize == NULL)
    {
        IOT_LOGE("group", "Invalid arguments to IOT_DaoGroupGetDeviceList");
        return IOT_ERR_FAIL;
    }

    iot_err_t storageErr = IOT_OK;
    char strGroupId[5];
    snprintf(strGroupId, sizeof(strGroupId), "%04x", groupId);

    uint8_t *deviceList = NULL;
    uint16_t dataSize = 0;

    // DAO_GetData allocates the buffer internally and returns it via pointer
    storageErr = IOT_DaoStorageGet("group-v1", strGroupId, IOT_DAO_BLOB, (void **) &deviceList, (uint16_t *) &dataSize);
    if (storageErr != IOT_OK)
    {
        if (storageErr == IOT_ERR_NOT_FOUND)
        {
            *outDeviceList = NULL;
            *outGroupListSize = 0;
            return IOT_ERR_DATA_NOT_FOUND;
        }
        IOT_LOGE("group", "Failed to get group data for Group ID %02x, ERR %d", groupId, storageErr);
        return IOT_ERR_FAIL;
    }

    if (deviceList == NULL || dataSize == 0)
    {
        *outDeviceList = NULL;
        *outGroupListSize = 0;
        return IOT_ERR_DATA_NOT_FOUND;
    }

    // if (dataSize > UINT16_MAX)
    // {
    //     IOT_LOGE("group", "Group data too large for Group ID %02x (size=%zu)", groupId, dataSize);
    //     free(deviceList);
    //     return IOT_ERR_FAIL;
    // }

    *outDeviceList = deviceList;
    *outGroupListSize = (uint16_t) dataSize;
    return IOT_OK;
}
iot_err_t IOT_DaoGroupRemoveDeviceFromGroup(uint8_t groupId, uint16_t eid)
{
    iot_err_t iot_err = IOT_OK;
    iot_err_t storageErr = IOT_OK;
    // get device list for the group
    uint8_t *deviceList = NULL;
    uint16_t groupListSize = 0;
    iot_err = IOT_DaoGroupGetDeviceList(groupId, &deviceList, &groupListSize);
    if(iot_err == IOT_ERR_DATA_NOT_FOUND){
        IOT_LOGW("group", "Group ID %02x not found when removing device EID %04x", groupId, eid);
        return IOT_OK;
    }
    else if (iot_err != IOT_OK)
    {
        IOT_LOGE("group", "Failed to get device list for Group ID %02x", groupId);
        return iot_err;
    }
    if (groupListSize == 0 || deviceList == NULL)
    {
        IOT_LOGW("group", "Group ID %02x has no devices", groupId);
        return IOT_ERR_NOT_FOUND;
    }
    uint16_t index = 0;
    bool isExist = false;
    uint16_t eidElemInfoLen = 0;
    while (index < groupListSize)
    {
        uint16_t currentEid = (deviceList[index] << 8) | deviceList[index + 1];
        eidElemInfoLen = (deviceList[index + 2] << 8) | deviceList[index + 3];
        index += 4 + eidElemInfoLen;
        if (currentEid == eid)
        {
            isExist = true;
            break;
        }
    }

    if (!isExist)
    {
        IOT_LOGW("group", "Device EID %04x not found in Group ID %02x", eid, groupId);
        free(deviceList);
        return IOT_OK;
    }
    uint16_t newSize = groupListSize - (4 + eidElemInfoLen);
    if (newSize == 0)
    {
        // If no devices left, remove the group data entirely
        char strGroupId[5];
        snprintf(strGroupId, sizeof(strGroupId), "%04x", groupId);
        storageErr = IOT_DaoStorageRemoveByKey("group-v1", strGroupId);
        if (storageErr != IOT_OK)
        {
            IOT_LOGE("group", "Failed to delete group data for Group ID %02x", groupId);
            free(deviceList);
            return IOT_ERR_FAIL;
        }
        free(deviceList);
        return IOT_OK;
    }
    // Remove the device from the list by shifting subsequent elements
    memmove(&deviceList[index - (4 + eidElemInfoLen)], &deviceList[index], groupListSize - index);
    groupListSize -= (4 + eidElemInfoLen);
    // Update the group data in DAO
    char strGroupId[5];
    snprintf(strGroupId, sizeof(strGroupId), "%04x", groupId);
    storageErr = IOT_DaoStorageSet("group-v1", strGroupId, IOT_DAO_BLOB, deviceList, groupListSize);
    if (storageErr != IOT_OK)
    {
        IOT_LOGE("group", "Failed to update group data for Group ID %02x", groupId);
        free(deviceList);
        return IOT_ERR_FAIL;
    }
    free(deviceList);
    return IOT_OK;
}
iot_err_t IOT_DaoGroupRemoveDeviceFromAllGroup(uint16_t eid)
{
    iot_err_t iot_err = IOT_OK;
    iot_err_t storageErr = IOT_OK;
    // get list of all groups
    char **groupKeyList = NULL;
    uint16_t groupCount = 0;
    storageErr = IOT_DaoStorageGetKeyList("group-v1", &groupKeyList, &groupCount);
    if (storageErr != IOT_OK)
    {
        IOT_LOGE("group", "Failed to get group key list");
        return IOT_ERR_FAIL;
    }
    for (uint16_t i = 0; i < groupCount; i++)
    {
        uint16_t groupId = (uint16_t) strtol(groupKeyList[i], NULL, 16);
        iot_err = IOT_DaoGroupRemoveDeviceFromGroup((uint8_t) groupId, eid);
        if (iot_err != IOT_OK)
        {
            IOT_LOGE("group", "Failed to remove device EID %04x from Group ID %02x", eid, groupId);
            continue;
        }
        IOT_LOGI("group", "Removed device EID %04x from Group ID %02x", eid, groupId);
        free(groupKeyList[i]);
    }
    free(groupKeyList);
    return IOT_OK;
}
iot_err_t IOT_DaoGroupAddDeviceToGroup(uint16_t groupId, uint16_t eid, uint8_t *elemInfo, uint16_t elemInfoLen)
{
    iot_err_t storageErr = IOT_OK;
    char strGroupId[5];
    snprintf(strGroupId, sizeof(strGroupId), "%04x", groupId);

    uint8_t *deviceList = NULL;
    uint16_t dataSize = 0;

    // DAO_GetData allocates the buffer internally and returns it via pointer
    storageErr = IOT_DaoStorageGet("group-v1", strGroupId, IOT_DAO_BLOB, (void **) &deviceList, &dataSize);

    uint16_t newSize = dataSize + 4 + elemInfoLen;

    if (storageErr == IOT_OK && dataSize > 0)
    {
        deviceList = (uint8_t *) realloc(deviceList, newSize);
        if (deviceList == NULL)
        {
            IOT_LOGE("group", "Failed to reallocate memory for device list of Group ID %04x", groupId);
            return IOT_ERR_NO_MEM;
        }
    }
    else if (storageErr == IOT_ERR_NOT_FOUND || dataSize == 0)
    {
        deviceList = (uint8_t *) malloc(newSize);
        if (deviceList == NULL)
        {
            IOT_LOGE("group", "Failed to allocate memory for device list of Group ID %04x", groupId);
            return IOT_ERR_NO_MEM;
        }
    }
    else
    {
        IOT_LOGE("group", "Failed to get group data for Group ID %04x", groupId);
        return IOT_ERR_FAIL;
    }

    // Append new device entry
    uint16_t offset = (uint16_t) dataSize;
    deviceList[offset] = (eid >> 8) & 0xFF;
    deviceList[offset + 1] = eid & 0xFF;
    deviceList[offset + 2] = (elemInfoLen >> 8) & 0xFF;
    deviceList[offset + 3] = elemInfoLen & 0xFF;
    if (elemInfoLen > 0)
        memcpy(&deviceList[offset + 4], elemInfo, elemInfoLen);

    // Update the group data in DAO
    storageErr = IOT_DaoStorageSet("group-v1", strGroupId, IOT_DAO_BLOB, deviceList, newSize);
    if (storageErr != IOT_OK)
    {
        IOT_LOGE("group", "Failed to update group data for Group ID %02x", groupId);
        free(deviceList);
        return IOT_ERR_FAIL;
    }
    free(deviceList);
    return IOT_OK;
}

iot_err_t IOT_DaoGroupFactoryReset(void){
    iot_err_t storageErr = IOT_DaoStorageRemoveNamespace("group-v1");
    if (storageErr != IOT_OK)
    {
        IOT_LOGE("group", "Failed to factory reset group data: %d", storageErr);
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}
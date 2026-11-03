#include "dao/IOT_DaoSmartMgmt.h"
#include "dao/IOT_DaoSmartTriggerMgmt.h"
#include "IOT_Common.h"
#include "IOT_ErrorManager.h"
#include <inttypes.h>
#include "IOT_Log.h"
#include "dao/IOT_DaoStorage.h"

static const char *TAG = "DaoSmart";

#define SMART_NAMESPACE_PREFIX "S"
#define SMART_META_KEY "m"
#define SMART_META_SIZE 8 // rootEid(2) + prtc(2) + filter(2) + cfm(2)

static void makeNamespace(uint16_t smid, char *out, size_t outSize)
{
    snprintf(out, outSize, SMART_NAMESPACE_PREFIX "%04x", smid);
}

static void makeEidKey(uint16_t eid, char *out, size_t outSize)
{
    snprintf(out, outSize, "%04x", eid);
}

iot_err_t IOT_DaoSmartRemoveDeviceData(uint16_t eid)
{
    // Get all SMIDs that have triggers for this EID — these same SMIDs
    // will also have bind commands for this device
    uint16_t *smids = NULL;
    uint16_t count = 0;
    iot_err_t err = IOT_DaoSmartTriggerGetByEid(eid, &smids, &count);
    if (err != IOT_OK || count == 0)
    {
        IOT_LOGI(TAG, "No smart bind data to remove for EID=%04x", eid);
        return IOT_OK;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        IOT_DaoSmartRemoveBindCmd(smids[i], eid);
    }

    SAFE_FREE(smids);
    IOT_LOGI(TAG, "Removed bind cmds from %u SMIDs for EID=%04x", count, eid);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartFactoryReset(const uint16_t *smids, uint16_t smidCount)
{
    if (smids == NULL || smidCount == 0)
    {
        return IOT_OK;
    }

    for (uint16_t i = 0; i < smidCount; i++)
    {
        IOT_DaoSmartRemoveSmid(smids[i]);
    }

    IOT_LOGI(TAG, "Factory reset: removed %u smart bind namespaces", smidCount);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartSetBindCmd(uint16_t smid, uint16_t eid,
                                  uint16_t rootEid, uint16_t prtc, uint16_t filter,
                                  uint16_t cfm,
                                  uint8_t *cmdData, uint16_t cmdDataLen)
{
    if (cmdData == NULL || cmdDataLen == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char ns[8]; // "S" + 4 hex + null
    makeNamespace(smid, ns, sizeof(ns));

    char eidKey[5];
    makeEidKey(eid, eidKey, sizeof(eidKey));

    // Store command data
    iot_err_t err = IOT_DaoStorageSet(ns, eidKey, IOT_DAO_BLOB, cmdData, cmdDataLen);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to store bind cmd for SMID=%04x EID=%04x: %d", smid, eid, err);
        return IOT_ERR_FAIL;
    }

    // Store/update metadata
    uint8_t meta[SMART_META_SIZE];
    meta[0] = HIGH_BYTE(rootEid);
    meta[1] = LOW_BYTE(rootEid);
    meta[2] = HIGH_BYTE(prtc);
    meta[3] = LOW_BYTE(prtc);
    meta[4] = HIGH_BYTE(filter);
    meta[5] = LOW_BYTE(filter);
    meta[6] = HIGH_BYTE(cfm);
    meta[7] = LOW_BYTE(cfm);

    err = IOT_DaoStorageSet(ns, SMART_META_KEY, IOT_DAO_BLOB, meta, SMART_META_SIZE);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to store metadata for SMID=%04x: %d", smid, err);
        return IOT_ERR_FAIL;
    }

    IOT_LOGI(TAG, "Stored bind cmd for SMID=%04x EID=%04x (%u bytes)", smid, eid, cmdDataLen);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartGetBindCmds(uint16_t smid,
                                   char ***outKeyList, uint16_t *outKeyCount,
                                   void ***outDataList, uint16_t **outDataSizes,
                                   uint16_t *outRootEid, uint16_t *outPrtc,
                                   uint16_t *outCfm)
{
    if (outKeyList == NULL || outKeyCount == NULL || outDataList == NULL || outDataSizes == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    char ns[8];
    makeNamespace(smid, ns, sizeof(ns));

    char **keyList = NULL;
    uint16_t keyCount = 0;
    void **dataList = NULL;
    uint16_t *dataSizes = NULL;

    iot_err_t err = IOT_DaoStorageGetNamespaceData(ns, &keyList, &keyCount, &dataList, &dataSizes);
    if (err != IOT_OK)
    {
        if (err == IOT_ERR_NOT_FOUND)
        {
            *outKeyList = NULL;
            *outKeyCount = 0;
            *outDataList = NULL;
            *outDataSizes = NULL;
            return IOT_ERR_NOT_FOUND;
        }
        IOT_LOGE(TAG, "Failed to get bind cmds for SMID=%04x: %d", smid, err);
        return IOT_ERR_FAIL;
    }

    if (keyCount == 0)
    {
        *outKeyList = NULL;
        *outKeyCount = 0;
        *outDataList = NULL;
        *outDataSizes = NULL;
        return IOT_ERR_NOT_FOUND;
    }

    // Find and extract metadata, separate from command entries
    int metaIdx = -1;
    for (uint16_t i = 0; i < keyCount; i++)
    {
        if (strcmp(keyList[i], SMART_META_KEY) == 0)
        {
            metaIdx = i;
            if (outRootEid != NULL && dataSizes[i] >= 2)
            {
                uint8_t *m = (uint8_t *)dataList[i];
                *outRootEid = BYTES_TO_U16_BE(m[0], m[1]);
            }
            if (outPrtc != NULL && dataSizes[i] >= 4)
            {
                uint8_t *m = (uint8_t *)dataList[i];
                *outPrtc = BYTES_TO_U16_BE(m[2], m[3]);
            }
            if (outCfm != NULL && dataSizes[i] >= 8)
            {
                uint8_t *m = (uint8_t *)dataList[i];
                *outCfm = BYTES_TO_U16_BE(m[6], m[7]);
            }
            else if (outCfm != NULL)
            {
                *outCfm = 0; // old metadata without cfm field
            }
            break;
        }
    }

    // Count command entries (excluding metadata)
    uint16_t cmdCount = keyCount - (metaIdx >= 0 ? 1 : 0);

    if (cmdCount == 0)
    {
        // Only metadata, no commands
        for (uint16_t i = 0; i < keyCount; i++)
        {
            SAFE_FREE(keyList[i]);
            SAFE_FREE(dataList[i]);
        }
        SAFE_FREE(keyList);
        SAFE_FREE(dataList);
        SAFE_FREE(dataSizes);
        *outKeyList = NULL;
        *outKeyCount = 0;
        *outDataList = NULL;
        *outDataSizes = NULL;
        return IOT_ERR_NOT_FOUND;
    }

    // Build output arrays without the metadata entry
    char **cmdKeyList = (char **)Mem_SafeCalloc(cmdCount, sizeof(char *), TAG, "cmd keys");
    void **cmdDataList = (void **)Mem_SafeCalloc(cmdCount, sizeof(void *), TAG, "cmd data");
    uint16_t *cmdDataSizes = (uint16_t *)Mem_SafeCalloc(cmdCount, sizeof(uint16_t), TAG, "cmd sizes");
    if (cmdKeyList == NULL || cmdDataList == NULL || cmdDataSizes == NULL)
    {
        SAFE_FREE(cmdKeyList);
        SAFE_FREE(cmdDataList);
        SAFE_FREE(cmdDataSizes);
        for (uint16_t i = 0; i < keyCount; i++)
        {
            SAFE_FREE(keyList[i]);
            SAFE_FREE(dataList[i]);
        }
        SAFE_FREE(keyList);
        SAFE_FREE(dataList);
        SAFE_FREE(dataSizes);
        return IOT_ERR_NO_MEM;
    }

    uint16_t j = 0;
    for (uint16_t i = 0; i < keyCount; i++)
    {
        if ((int)i == metaIdx)
        {
            SAFE_FREE(keyList[i]);
            SAFE_FREE(dataList[i]);
            continue;
        }
        cmdKeyList[j] = keyList[i];
        cmdDataList[j] = dataList[i];
        cmdDataSizes[j] = dataSizes[i];
        j++;
    }
    SAFE_FREE(keyList);
    SAFE_FREE(dataList);
    SAFE_FREE(dataSizes);

    *outKeyList = cmdKeyList;
    *outKeyCount = cmdCount;
    *outDataList = cmdDataList;
    *outDataSizes = cmdDataSizes;
    return IOT_OK;
}

iot_err_t IOT_DaoSmartRemoveBindCmd(uint16_t smid, uint16_t eid)
{
    char ns[8];
    makeNamespace(smid, ns, sizeof(ns));

    char eidKey[5];
    makeEidKey(eid, eidKey, sizeof(eidKey));

    iot_err_t err = IOT_DaoStorageRemoveByKey(ns, eidKey);
    if (err != IOT_OK && err != IOT_ERR_NOT_FOUND)
    {
        IOT_LOGE(TAG, "Failed to remove bind cmd for SMID=%04x EID=%04x: %d", smid, eid, err);
        return IOT_ERR_FAIL;
    }

    IOT_LOGI(TAG, "Removed bind cmd for SMID=%04x EID=%04x", smid, eid);
    return IOT_OK;
}

iot_err_t IOT_DaoSmartRemoveSmid(uint16_t smid)
{
    char ns[8];
    makeNamespace(smid, ns, sizeof(ns));

    iot_err_t err = IOT_DaoStorageRemoveNamespace(ns);
    if (err != IOT_OK && err != IOT_ERR_NOT_FOUND)
    {
        IOT_LOGE(TAG, "Failed to remove SMID=%04x namespace: %d", smid, err);
        return IOT_ERR_FAIL;
    }

    IOT_LOGI(TAG, "Removed all data for SMID=%04x", smid);
    return IOT_OK;
}

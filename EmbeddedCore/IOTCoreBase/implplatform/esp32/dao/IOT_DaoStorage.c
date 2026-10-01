#include "dao/IOT_DaoStorage.h"
#include "dao_common.h"
#include "esp_err.h"
#include "nvs.h"

// ============================================================================
// ESP32 implementation: wraps dao_common (NVS-backed, task-safe)
// ============================================================================

iot_err_t IOT_DaoStorageGet(const char *ns, const char *key, IOT_DaoType type,
                             void **outData, uint16_t *outLen)
{
    esp_err_t err = DAO_GetData(ns, key, type, outData, outLen);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return IOT_ERR_NOT_FOUND;
    }
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoStorageSet(const char *ns, const char *key, IOT_DaoType type,
                             void *data, uint16_t len)
{
    esp_err_t err = DAO_SetData(ns, key, type, data, len);
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoStorageSetAsync(const char *ns, const char *key, IOT_DaoType type,
                                  void *data, uint16_t len)
{
    esp_err_t err = DAO_SetDataAsync(ns, key, type, data, len);
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoStorageSetBatch(const char *ns, const IOT_DaoBatchEntry_t *entries, uint16_t count)
{
    if (ns == NULL || entries == NULL || count == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }
    // IOT_DaoBatchEntry_t is the shared type (IOT_DaoType.h) — no conversion needed.
    esp_err_t err = DAO_SetDataBatch(ns, entries, count);
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoStorageRemoveByKey(const char *ns, const char *key)
{
    esp_err_t err = DAO_RemoveDataByKey(ns, key);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return IOT_ERR_NOT_FOUND;
    }
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoStorageRemoveByKeyAsync(const char *ns, const char *key)
{
    esp_err_t err = DAO_RemoveDataByKeyAsync(ns, key);
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoStorageCheckExists(const char *ns, const char *key, bool *exists)
{
    esp_err_t err = DAO_CheckDataExistByKey(ns, key, exists);
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoStorageRemoveNamespace(const char *ns)
{
    esp_err_t err = DAO_RemoveNamespace(ns);
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoStorageGetKeyList(const char *ns, char ***outKeyList, uint16_t *outKeyCount)
{
    esp_err_t err = DAO_GetKeyList(ns, outKeyList, outKeyCount);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return IOT_ERR_NOT_FOUND;
    }
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_DaoStorageGetNamespaceData(const char *ns, char ***outKeyList, uint16_t *outKeyCount,
                                          void ***outDataList, uint16_t **outDataSizes)
{
    esp_err_t err = DAO_GetNamespaceData(ns, outKeyList, outKeyCount, outDataList, outDataSizes);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return IOT_ERR_NOT_FOUND;
    }
    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

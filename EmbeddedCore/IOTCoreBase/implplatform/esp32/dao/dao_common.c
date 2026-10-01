#include "dao_common.h"
#include "IOT_Memory.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "dao/IOT_DaoType.h"
#include <string.h>
#include "IOT_Log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define ROGO_NVS_PARTITION "rogo"
#define DAO_QUEUE_SIZE 10
#define DAO_TASK_STACK_SIZE 4096
#define DAO_TASK_PRIORITY 5
#define DAO_QUEUE_SEND_TIMEOUT_MS 100
#define DAO_OPERATION_TIMEOUT_MS 5000

static const char *TAG = "dao_common";

// Operation types
typedef enum {
    DAO_OP_GET_NAMESPACE_DATA,
    DAO_OP_GET_DATA,
    DAO_OP_SET_DATA,
    DAO_OP_SET_BATCH,
    DAO_OP_GET_KEY_LIST,
    DAO_OP_REMOVE_KEY,
    DAO_OP_REMOVE_NAMESPACE,
    DAO_OP_CHECK_KEY_EXIST
} DAO_Operation_t;

// Request structure
typedef struct {
    DAO_Operation_t op;
    char namespace[16];
    char key[16];
    IOT_DaoType type;
    void *data;
    uint16_t len;
    bool free_data;

    // Batch set (DAO_OP_SET_BATCH) — borrowed array, caller-owned for the sync call
    const IOT_DaoBatchEntry_t *batch_entries;
    uint16_t batch_count;

    // Output pointers (for sync operations)
    void **out_data;
    uint16_t *out_len;
    char ***out_key_list;
    uint16_t *out_key_count;
    void ***out_data_list;
    uint16_t **out_data_sizes;
    bool *out_is_exist;
    
    // Completion signaling (for sync operations)
    SemaphoreHandle_t done_sem;
    esp_err_t *result_ptr;  // Pointer to caller's result variable
    
    // Async callback (for async operations)
    DAO_AsyncCallback_t callback;
    void *user_data;
} DAO_Request_t;

// Task and queue handles
static QueueHandle_t dao_queue = NULL;
static TaskHandle_t dao_task_handle = NULL;

// ============================================================================
// Internal implementations (run in DAO task context)
// ============================================================================

static esp_err_t DAO_GetNamespaceData_Internal(
    const char *namespace, char ***outKeyList, uint16_t *outKeyCount, void ***outDataList, uint16_t **outDataSizes)
{
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(ROGO_NVS_PARTITION, namespace, NVS_TYPE_ANY, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        *outKeyList = NULL;
        *outKeyCount = 0;
        *outDataList = NULL;
        *outDataSizes = NULL;
        return err;
    }
    else if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to find entries in namespace %s, err name: %s", namespace, esp_err_to_name(err));
        return err;
    }

    // Count keys
    uint16_t keyCount = 0;
    nvs_iterator_t countIt = it;
    while (err == ESP_OK && countIt != NULL)
    {
        keyCount++;
        err = nvs_entry_next(&countIt);
    }

    // Allocate memory
    *outKeyList = Mem_SafeMalloc(sizeof(char *) * keyCount, TAG, "key list");
    *outDataList = Mem_SafeMalloc(sizeof(void *) * keyCount, TAG, "data list");
    *outDataSizes = Mem_SafeMalloc(sizeof(uint16_t) * keyCount, TAG, "data sizes");
    if (*outKeyList == NULL || *outDataList == NULL || *outDataSizes == NULL)
    {
        IOT_LOGE(TAG, "Failed to allocate memory for namespace data");
        SAFE_FREE(*outKeyList);
        SAFE_FREE(*outDataList);
        SAFE_FREE(*outDataSizes);
        nvs_release_iterator(it);
        return ESP_ERR_NO_MEM;
    }

    // Initialize data list entries to NULL for safe cleanup
    for (uint16_t i = 0; i < keyCount; i++)
    {
        (*outKeyList)[i] = NULL;
        (*outDataList)[i] = NULL;
    }

    *outKeyCount = keyCount;
    uint16_t index = 0;
    err = nvs_entry_find(ROGO_NVS_PARTITION, namespace, NVS_TYPE_ANY, &it);
    
    nvs_handle_t handle;
    err = nvs_open_from_partition(ROGO_NVS_PARTITION, namespace, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Get Namespace data fail, Failed to open namespace %s", namespace);
        SAFE_FREE(*outKeyList);
        SAFE_FREE(*outDataList);
        SAFE_FREE(*outDataSizes);
        nvs_release_iterator(it);
        return err;
    }
    
    while (err == ESP_OK && it != NULL && index < keyCount)
    {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        const char *key = info.key;

        (*outKeyList)[index] = Mem_SafeMalloc(strlen(key) + 1, TAG, "key string");
        if ((*outKeyList)[index] == NULL)
        {
            goto cleanup_error;
        }
        strcpy((*outKeyList)[index], key);

        size_t dataSize = 0;
        err = nvs_get_blob(handle, key, NULL, &dataSize);
        if (err != ESP_OK)
        {
            IOT_LOGE(TAG, "Failed to get data size for key %s", key);
            goto cleanup_error;
        }
        
        (*outDataSizes)[index] = (uint16_t)dataSize;
        (*outDataList)[index] = Mem_SafeMalloc(dataSize, TAG, "data entry");
        if ((*outDataList)[index] == NULL)
        {
            goto cleanup_error;
        }
        
        err = nvs_get_blob(handle, key, (*outDataList)[index], &dataSize);
        if (err != ESP_OK)
        {
            IOT_LOGE(TAG, "Failed to get data for key %s", key);
            goto cleanup_error;
        }
        index++;
        err = nvs_entry_next(&it);
    }
    
    nvs_release_iterator(it);
    nvs_close(handle);
    return ESP_OK;

cleanup_error:
    for (uint16_t i = 0; i < keyCount; i++)
    {
        if ((*outKeyList)[i]) SAFE_FREE((*outKeyList)[i]);
        if ((*outDataList)[i]) SAFE_FREE((*outDataList)[i]);
    }
    SAFE_FREE(*outKeyList);
    SAFE_FREE(*outDataList);
    SAFE_FREE(*outDataSizes);
    *outKeyList = NULL;
    *outDataList = NULL;
    *outDataSizes = NULL;
    *outKeyCount = 0;
    nvs_release_iterator(it);
    nvs_close(handle);
    return ESP_ERR_NO_MEM;
}

static esp_err_t DAO_GetData_Internal(const char *namespace, const char *key, IOT_DaoType type, void **outData, uint16_t *outLen)
{
    if (type == IOT_DAO_STRING || type == IOT_DAO_BLOB)
    {
        if (outLen == NULL)
        {
            IOT_LOGE(TAG, "Get data fail, length pointer is NULL for STRING or BLOB type");
            return ESP_ERR_INVALID_ARG;
        }
    }

    nvs_handle_t handle;
    esp_err_t esp_err = nvs_open_from_partition(ROGO_NVS_PARTITION, namespace, NVS_READONLY, &handle);
    if (esp_err == ESP_ERR_NVS_NOT_FOUND)
    {
        // IOT_LOGI(TAG, "Namespace %s not found", namespace);
        return esp_err;
    }
    else if (esp_err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to open namespace %s: %s", namespace, esp_err_to_name(esp_err));
        return esp_err;
    }

    switch (type)
    {
    case IOT_DAO_U8:
    {
        uint8_t value;
        esp_err = nvs_get_u8(handle, key, &value);
        if (esp_err == ESP_OK)
        {
            *outData = Mem_SafeMalloc(sizeof(uint8_t), TAG, "u8 value");
            if (*outData == NULL)
            {
                nvs_close(handle);
                return ESP_ERR_NO_MEM;
            }
            *(uint8_t *)*outData = value;
        }
        break;
    }
    case IOT_DAO_U16:
    {
        uint16_t value;
        esp_err = nvs_get_u16(handle, key, &value);
        if (esp_err == ESP_OK)
        {
            *outData = Mem_SafeMalloc(sizeof(uint16_t), TAG, "u16 value");
            if (*outData == NULL)
            {
                nvs_close(handle);
                return ESP_ERR_NO_MEM;
            }
            *(uint16_t *)*outData = value;
        }
        break;
    }
    case IOT_DAO_U32:
    {
        uint32_t value;
        esp_err = nvs_get_u32(handle, key, &value);
        if (esp_err == ESP_OK)
        {
            *outData = Mem_SafeMalloc(sizeof(uint32_t), TAG, "u32 value");
            if (*outData == NULL)
            {
                nvs_close(handle);
                return ESP_ERR_NO_MEM;
            }
            *(uint32_t *)*outData = value;
        }
        break;
    }
    case IOT_DAO_U64:
    {
        uint64_t value;
        esp_err = nvs_get_u64(handle, key, &value);
        if (esp_err == ESP_OK)
        {
            *outData = Mem_SafeMalloc(sizeof(uint64_t), TAG, "u64 value");
            if (*outData == NULL)
            {
                nvs_close(handle);
                return ESP_ERR_NO_MEM;
            }
            *(uint64_t *)*outData = value;
        }
        break;
    }
    case IOT_DAO_STRING:
    {
        size_t len = 0;
        esp_err = nvs_get_str(handle, key, NULL, &len);
        if (esp_err == ESP_OK)
        {
            *outData = Mem_SafeMalloc(len, TAG, "blob value");
            if (*outData == NULL)
            {
                nvs_close(handle);
                return ESP_ERR_NO_MEM;
            }
            esp_err = nvs_get_str(handle, key, (char *)*outData, &len);
            *outLen = (uint16_t)len;
        }
        break;
    }
    case IOT_DAO_BLOB:
    {
        size_t len = 0;
        esp_err = nvs_get_blob(handle, key, NULL, &len);
        if (esp_err == ESP_OK)
        {
            *outData = Mem_SafeMalloc(len, TAG, "blob value");
            if (*outData == NULL)
            {
                nvs_close(handle);
                return ESP_ERR_NO_MEM;
            }
            esp_err = nvs_get_blob(handle, key, *outData, &len);
            *outLen = (uint16_t)len;
        }
        break;
    }
    default:
        nvs_close(handle);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (esp_err != ESP_OK && esp_err != ESP_ERR_NVS_NOT_FOUND)
    {
        IOT_LOGE(TAG, "Failed to get data for key %s: %s", key, esp_err_to_name(esp_err));
    }

    nvs_close(handle);
    return esp_err;
}

static esp_err_t DAO_SetData_Internal(const char *namespace, const char *key, IOT_DaoType type, void *data, uint16_t len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(ROGO_NVS_PARTITION, namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Set data fail, Failed to open namespace %s", namespace);
        return err;
    }
    
    switch (type)
    {
    case IOT_DAO_U8:
        err = nvs_set_u8(handle, key, *(uint8_t *)data);
        break;
    case IOT_DAO_U16:
        err = nvs_set_u16(handle, key, *(uint16_t *)data);
        break;
    case IOT_DAO_U32:
        err = nvs_set_u32(handle, key, *(uint32_t *)data);
        break;
    case IOT_DAO_U64:
        err = nvs_set_u64(handle, key, *(uint64_t *)data);
        break;
    case IOT_DAO_STRING:
        err = nvs_set_str(handle, key, (const char *)data);
        break;
    case IOT_DAO_BLOB:
        err = nvs_set_blob(handle, key, data, len);
        break;
    default:
        nvs_close(handle);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Error saving: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t DAO_SetOneEntry(nvs_handle_t handle, const IOT_DaoBatchEntry_t *e)
{
    switch (e->type)
    {
    case IOT_DAO_U8:
        return nvs_set_u8(handle, e->key, *(const uint8_t *)e->data);
    case IOT_DAO_U16:
        return nvs_set_u16(handle, e->key, *(const uint16_t *)e->data);
    case IOT_DAO_U32:
        return nvs_set_u32(handle, e->key, *(const uint32_t *)e->data);
    case IOT_DAO_U64:
        return nvs_set_u64(handle, e->key, *(const uint64_t *)e->data);
    case IOT_DAO_STRING:
        return nvs_set_str(handle, e->key, (const char *)e->data);
    case IOT_DAO_BLOB:
        return nvs_set_blob(handle, e->key, e->data, e->len);
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t DAO_SetDataBatch_Internal(const char *namespace, const IOT_DaoBatchEntry_t *entries, uint16_t count)
{
    if (entries == NULL || count == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(ROGO_NVS_PARTITION, namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Batch set fail, Failed to open namespace %s", namespace);
        return err;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        err = DAO_SetOneEntry(handle, &entries[i]);
        if (err != ESP_OK)
        {
            IOT_LOGE(TAG, "Batch set fail at %u/%u key=%s: %s", i, count, entries[i].key, esp_err_to_name(err));
            nvs_close(handle);  // no commit — leave prior state untouched
            return err;
        }
    }

    err = nvs_commit(handle);  // single commit for the whole batch
    nvs_close(handle);
    IOT_LOGI(TAG, "Batch set: %u entries, 1 commit, ns=%s", count, namespace);
    return err;
}

static esp_err_t DAO_GetKeyList_Internal(const char *namespace, char ***outKeyList, uint16_t *outKeyCount)
{
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(ROGO_NVS_PARTITION, namespace, NVS_TYPE_ANY, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        *outKeyList = NULL;
        *outKeyCount = 0;
        return err;
    }
    else if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to find entries in namespace %s: %s", namespace, esp_err_to_name(err));
        return err;
    }

    uint16_t keyCount = 0;
    nvs_iterator_t countIt = it;
    while (err == ESP_OK && countIt != NULL)
    {
        keyCount++;
        err = nvs_entry_next(&countIt);
    }

    *outKeyList = Mem_SafeMalloc(sizeof(char *) * keyCount, TAG, "key list");
    if (*outKeyList == NULL)
    {
        IOT_LOGE(TAG, "Failed to allocate memory for key list");
        nvs_release_iterator(it);
        return ESP_ERR_NO_MEM;
    }

    // Initialize to NULL for safe cleanup
    for (uint16_t i = 0; i < keyCount; i++)
    {
        (*outKeyList)[i] = NULL;
    }

    *outKeyCount = keyCount;
    uint16_t index = 0;
    err = nvs_entry_find(ROGO_NVS_PARTITION, namespace, NVS_TYPE_ANY, &it);
    while (err == ESP_OK && it != NULL && index < keyCount)
    {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        const char *key = info.key;

        (*outKeyList)[index] = Mem_SafeMalloc(strlen(key) + 1, TAG, "key string");
        if ((*outKeyList)[index] == NULL)
        {
            for (uint16_t i = 0; i < index; i++)
            {
                SAFE_FREE((*outKeyList)[i]);
            }
            SAFE_FREE(*outKeyList);
            *outKeyList = NULL;
            *outKeyCount = 0;
            nvs_release_iterator(it);
            return ESP_ERR_NO_MEM;
        }
        strcpy((*outKeyList)[index], key);
        index++;
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    return ESP_OK;
}

static esp_err_t DAO_RemoveDataByKey_Internal(const char *namespace, const char *key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(ROGO_NVS_PARTITION, namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to open namespace %s", namespace);
        return err;
    }
    
    err = nvs_erase_key(handle, key);
    if(err == ESP_ERR_NVS_NOT_FOUND)
    {
        // Deleting a key that isn't there is the normal outcome of an idempotent delete —
        // callers that sweep a fixed key space (e.g. all 8 trigger seats of an SMID) hit this
        // on every absent slot. Warning-level here buried real faults under false alarms.
        // No flash is touched: nvs_erase_key writes nothing on a miss and we return before
        // nvs_commit(), so this costs no NVS wear. The error code still reaches the caller.
        IOT_LOGD(TAG, "Key %s not found in namespace %s (nothing to erase)", key, namespace);
        nvs_close(handle);
        return err;
    }
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Error erasing key %s: %s", key, esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t DAO_RemoveNamespace_Internal(const char *namespace)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(ROGO_NVS_PARTITION, namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to open namespace %s", namespace);
        return err;
    }
    
    err = nvs_erase_all(handle);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to erase namespace %s", namespace);
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t DAO_CheckDataExistByKey_Internal(const char *namespace, const char *key, bool *isExist)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(ROGO_NVS_PARTITION, namespace, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            *isExist = false;
            return ESP_OK;
        }
        IOT_LOGE(TAG, "Failed to open namespace %s", namespace);
        return err;
    }
    
    nvs_iterator_t it = NULL;
    esp_err_t findErr = nvs_entry_find(ROGO_NVS_PARTITION, namespace, NVS_TYPE_ANY, &it);
    *isExist = false;
    
    while (findErr == ESP_OK && it != NULL)
    {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (strcmp(info.key, key) == 0)
        {
            *isExist = true;
            break;
        }
        findErr = nvs_entry_next(&it);
    }
    
    if (it) nvs_release_iterator(it);
    nvs_close(handle);
    return ESP_OK;
}

// ============================================================================
// DAO Task - processes all requests in safe task context
// ============================================================================

static void dao_task(void *pvParameters)
{
    DAO_Request_t req;
    esp_err_t result;

    while (1)
    {
        if (xQueueReceive(dao_queue, &req, portMAX_DELAY) == pdTRUE)
        {
            switch (req.op)
            {
            case DAO_OP_GET_NAMESPACE_DATA:
                result = DAO_GetNamespaceData_Internal(
                    req.namespace,
                    req.out_key_list,
                    req.out_key_count,
                    req.out_data_list,
                    req.out_data_sizes);
                break;

            case DAO_OP_GET_DATA:
                result = DAO_GetData_Internal(
                    req.namespace,
                    req.key,
                    req.type,
                    req.out_data,
                    req.out_len);
                break;

            case DAO_OP_SET_DATA:
                result = DAO_SetData_Internal(
                    req.namespace,
                    req.key,
                    req.type,
                    req.data,
                    req.len);
                // Free data copied by caller
                if (req.data && req.free_data)
                {
                    SAFE_FREE(req.data);
                }
                break;

            case DAO_OP_SET_BATCH:
                result = DAO_SetDataBatch_Internal(
                    req.namespace,
                    req.batch_entries,
                    req.batch_count);
                // batch_entries are caller-owned (sync) — nothing to free here
                break;

            case DAO_OP_GET_KEY_LIST:
                result = DAO_GetKeyList_Internal(
                    req.namespace,
                    req.out_key_list,
                    req.out_key_count);
                break;

            case DAO_OP_REMOVE_KEY:
                result = DAO_RemoveDataByKey_Internal(
                    req.namespace,
                    req.key);
                break;

            case DAO_OP_REMOVE_NAMESPACE:
                result = DAO_RemoveNamespace_Internal(req.namespace);
                break;

            case DAO_OP_CHECK_KEY_EXIST:
                result = DAO_CheckDataExistByKey_Internal(
                    req.namespace,
                    req.key,
                    req.out_is_exist);
                break;

            default:
                result = ESP_ERR_INVALID_ARG;
                break;
            }

            // Handle completion based on request type
            if (req.done_sem)
            {
                // Synchronous request - signal semaphore
                if (req.result_ptr)
                {
                    *req.result_ptr = result;
                }
                xSemaphoreGive(req.done_sem);
            }
            else if (req.callback)
            {
                // Async request with callback
                req.callback(result, req.user_data);
            }
            // else: fire-and-forget, no notification needed
        }
    }
}

// ============================================================================
// Public API - Initialization
// ============================================================================

esp_err_t DAO_InitStorage(void)
{
    esp_err_t err = nvs_flash_init_partition(ROGO_NVS_PARTITION);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to init NVS partition: %s", esp_err_to_name(err));
        return err;
    }
    
    // Create queue
    dao_queue = xQueueCreate(DAO_QUEUE_SIZE, sizeof(DAO_Request_t));
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "Failed to create DAO queue");
        return ESP_ERR_NO_MEM;
    }
    
    // Create DAO task with large stack
    BaseType_t ret = xTaskCreate(
        dao_task,
        "dao_task",
        DAO_TASK_STACK_SIZE,
        NULL,
        DAO_TASK_PRIORITY,
        &dao_task_handle
    );
    
    if (ret != pdPASS)
    {
        IOT_LOGE(TAG, "Failed to create DAO task");
        vQueueDelete(dao_queue);
        dao_queue = NULL;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t DAO_DeinitStorage(void)
{
    if (dao_task_handle)
    {
        vTaskDelete(dao_task_handle);
        dao_task_handle = NULL;
    }
    
    if (dao_queue)
    {
        vQueueDelete(dao_queue);
        dao_queue = NULL;
    }
    
    esp_err_t err = nvs_flash_erase_partition(ROGO_NVS_PARTITION);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to erase NVS partition: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

// ============================================================================
// Public API - Synchronous (BLOCKING)
// ============================================================================

esp_err_t DAO_GetNamespaceData(const char *namespace, char ***outKeyList, uint16_t *outKeyCount,
                                void ***outDataList, uint16_t **outDataSizes)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_ERR_TIMEOUT;

    DAO_Request_t req = {
        .op = DAO_OP_GET_NAMESPACE_DATA,
        .type = IOT_DAO_BLOB,
        .data = NULL,
        .len = 0,
        .free_data = false,
        .out_data = NULL,
        .out_len = NULL,
        .out_key_list = outKeyList,
        .out_key_count = outKeyCount,
        .out_data_list = outDataList,
        .out_data_sizes = outDataSizes,
        .out_is_exist = NULL,
        .done_sem = xSemaphoreCreateBinary(),
        .result_ptr = &result,
        .callback = NULL,
        .user_data = NULL
    };

    if (!req.done_sem)
    {
        IOT_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    req.key[0] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        vSemaphoreDelete(req.done_sem);
        IOT_LOGE(TAG, "Queue send timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(req.done_sem, pdMS_TO_TICKS(DAO_OPERATION_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Operation timeout");
        result = ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(req.done_sem);
    return result;
}

esp_err_t DAO_GetData(const char *namespace, const char *key, IOT_DaoType type,
                      void **outData, uint16_t *outLen)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_ERR_TIMEOUT;

    DAO_Request_t req = {
        .op = DAO_OP_GET_DATA,
        .type = type,
        .data = NULL,
        .len = 0,
        .free_data = false,
        .out_data = outData,
        .out_len = outLen,
        .out_key_list = NULL,
        .out_key_count = NULL,
        .out_data_list = NULL,
        .out_data_sizes = NULL,
        .out_is_exist = NULL,
        .done_sem = xSemaphoreCreateBinary(),
        .result_ptr = &result,
        .callback = NULL,
        .user_data = NULL
    };

    if (!req.done_sem)
    {
        IOT_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.key[sizeof(req.key) - 1] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        vSemaphoreDelete(req.done_sem);
        IOT_LOGE(TAG, "Queue send timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(req.done_sem, pdMS_TO_TICKS(DAO_OPERATION_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Operation timeout");
        result = ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(req.done_sem);
    return result;
}

esp_err_t DAO_SetData(const char *namespace, const char *key, IOT_DaoType type,
                      void *data, uint16_t len)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Copy data for async processing
    void *data_copy = NULL;
    size_t copy_size = 0;
    
    if (type == IOT_DAO_STRING)
    {
        copy_size = strlen((char *)data) + 1;
        data_copy = Mem_SafeMalloc(copy_size, TAG, "async data copy");
        if (!data_copy)
        {
            IOT_LOGE(TAG, "Failed to allocate memory for string copy");
            return ESP_ERR_NO_MEM;
        }
        strcpy((char *)data_copy, (char *)data);
    }
    else if (type == IOT_DAO_BLOB)
    {
        copy_size = len;
        data_copy = Mem_SafeMalloc(copy_size, TAG, "async data copy");
        if (!data_copy)
        {
            IOT_LOGE(TAG, "Failed to allocate memory for blob copy");
            return ESP_ERR_NO_MEM;
        }
        memcpy(data_copy, data, len);
    }
    else
    {
        // For primitive types
        switch (type)
        {
        case IOT_DAO_U8:
            copy_size = sizeof(uint8_t);
            break;
        case IOT_DAO_U16:
            copy_size = sizeof(uint16_t);
            break;
        case IOT_DAO_U32:
            copy_size = sizeof(uint32_t);
            break;
        case IOT_DAO_U64:
            copy_size = sizeof(uint64_t);
            break;
        default:
            IOT_LOGE(TAG, "Invalid data type");
            return ESP_ERR_INVALID_ARG;
        }
        data_copy = Mem_SafeMalloc(copy_size, TAG, "async data copy");
        if (!data_copy)
        {
            IOT_LOGE(TAG, "Failed to allocate memory for primitive copy");
            return ESP_ERR_NO_MEM;
        }
        memcpy(data_copy, data, copy_size);
    }

    esp_err_t result = ESP_ERR_TIMEOUT;

    DAO_Request_t req = {
        .op = DAO_OP_SET_DATA,
        .type = type,
        .data = data_copy,
        .len = len,
        .free_data = true,
        .out_data = NULL,
        .out_len = NULL,
        .out_key_list = NULL,
        .out_key_count = NULL,
        .out_data_list = NULL,
        .out_data_sizes = NULL,
        .out_is_exist = NULL,
        .done_sem = xSemaphoreCreateBinary(),
        .result_ptr = &result,
        .callback = NULL,
        .user_data = NULL
    };

    if (!req.done_sem)
    {
        IOT_LOGE(TAG, "Failed to create semaphore");
        SAFE_FREE(data_copy);
        return ESP_ERR_NO_MEM;
    }

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.key[sizeof(req.key) - 1] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        vSemaphoreDelete(req.done_sem);
        SAFE_FREE(data_copy);
        IOT_LOGE(TAG, "Queue send timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(req.done_sem, pdMS_TO_TICKS(DAO_OPERATION_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Operation timeout");
        result = ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(req.done_sem);
    return result;
}

esp_err_t DAO_SetDataBatch(const char *namespace, const IOT_DaoBatchEntry_t *entries, uint16_t count)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (entries == NULL || count == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_ERR_TIMEOUT;

    // Sync call: entries/data stay caller-owned for the blocking duration, so the
    // borrowed array pointer is passed through without copying.
    DAO_Request_t req = {
        .op = DAO_OP_SET_BATCH,
        .type = IOT_DAO_BLOB,
        .data = NULL,
        .len = 0,
        .free_data = false,
        .batch_entries = entries,
        .batch_count = count,
        .done_sem = xSemaphoreCreateBinary(),
        .result_ptr = &result,
        .callback = NULL,
        .user_data = NULL
    };

    if (!req.done_sem)
    {
        IOT_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    req.key[0] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        vSemaphoreDelete(req.done_sem);
        IOT_LOGE(TAG, "Queue send timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(req.done_sem, pdMS_TO_TICKS(DAO_OPERATION_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Operation timeout");
        result = ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(req.done_sem);
    return result;
}

esp_err_t DAO_GetKeyList(const char *namespace, char ***outKeyList, uint16_t *outKeyCount)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_ERR_TIMEOUT;

    DAO_Request_t req = {
        .op = DAO_OP_GET_KEY_LIST,
        .type = IOT_DAO_BLOB,
        .data = NULL,
        .len = 0,
        .free_data = false,
        .out_data = NULL,
        .out_len = NULL,
        .out_key_list = outKeyList,
        .out_key_count = outKeyCount,
        .out_data_list = NULL,
        .out_data_sizes = NULL,
        .out_is_exist = NULL,
        .done_sem = xSemaphoreCreateBinary(),
        .result_ptr = &result,
        .callback = NULL,
        .user_data = NULL
    };

    if (!req.done_sem)
    {
        IOT_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    req.key[0] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        vSemaphoreDelete(req.done_sem);
        IOT_LOGE(TAG, "Queue send timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(req.done_sem, pdMS_TO_TICKS(DAO_OPERATION_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Operation timeout");
        result = ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(req.done_sem);
    return result;
}

esp_err_t DAO_RemoveDataByKey(const char *namespace, const char *key)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_ERR_TIMEOUT;

    DAO_Request_t req = {
        .op = DAO_OP_REMOVE_KEY,
        .type = IOT_DAO_BLOB,
        .data = NULL,
        .len = 0,
        .free_data = false,
        .out_data = NULL,
        .out_len = NULL,
        .out_key_list = NULL,
        .out_key_count = NULL,
        .out_data_list = NULL,
        .out_data_sizes = NULL,
        .out_is_exist = NULL,
        .done_sem = xSemaphoreCreateBinary(),
        .result_ptr = &result,
        .callback = NULL,
        .user_data = NULL
    };

    if (!req.done_sem)
    {
        IOT_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.key[sizeof(req.key) - 1] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        vSemaphoreDelete(req.done_sem);
        IOT_LOGE(TAG, "Queue send timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(req.done_sem, pdMS_TO_TICKS(DAO_OPERATION_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Operation timeout");
        result = ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(req.done_sem);
    return result;
}

esp_err_t DAO_RemoveNamespace(const char *namespace)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_ERR_TIMEOUT;

    DAO_Request_t req = {
        .op = DAO_OP_REMOVE_NAMESPACE,
        .type = IOT_DAO_BLOB,
        .data = NULL,
        .len = 0,
        .free_data = false,
        .out_data = NULL,
        .out_len = NULL,
        .out_key_list = NULL,
        .out_key_count = NULL,
        .out_data_list = NULL,
        .out_data_sizes = NULL,
        .out_is_exist = NULL,
        .done_sem = xSemaphoreCreateBinary(),
        .result_ptr = &result,
        .callback = NULL,
        .user_data = NULL
    };

    if (!req.done_sem)
    {
        IOT_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    req.key[0] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        vSemaphoreDelete(req.done_sem);
        IOT_LOGE(TAG, "Queue send timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(req.done_sem, pdMS_TO_TICKS(DAO_OPERATION_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Operation timeout");
        result = ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(req.done_sem);
    return result;
}

esp_err_t DAO_CheckDataExistByKey(const char *namespace, const char *key, bool *isExist)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (isExist == NULL)
    {
        IOT_LOGE(TAG, "isExist pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_ERR_TIMEOUT;

    DAO_Request_t req = {
        .op = DAO_OP_CHECK_KEY_EXIST,
        .type = IOT_DAO_BLOB,
        .data = NULL,
        .len = 0,
        .free_data = false,
        .out_data = NULL,
        .out_len = NULL,
        .out_key_list = NULL,
        .out_key_count = NULL,
        .out_data_list = NULL,
        .out_data_sizes = NULL,
        .out_is_exist = isExist,
        .done_sem = xSemaphoreCreateBinary(),
        .result_ptr = &result,
        .callback = NULL,
        .user_data = NULL
    };

    if (!req.done_sem)
    {
        IOT_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.key[sizeof(req.key) - 1] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        vSemaphoreDelete(req.done_sem);
        IOT_LOGE(TAG, "Queue send timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(req.done_sem, pdMS_TO_TICKS(DAO_OPERATION_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Operation timeout");
        result = ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(req.done_sem);
    return result;
}

// ============================================================================
// Public API - Asynchronous (NON-BLOCKING)
// ============================================================================

/**
 * @brief Helper to copy data for async operations
 */
static esp_err_t DAO_CopyDataForAsync(IOT_DaoType type, void *data, uint16_t len, 
                                       void **data_copy, size_t *copy_size)
{
    if (type == IOT_DAO_STRING)
    {
        *copy_size = strlen((char *)data) + 1;
        *data_copy = Mem_SafeMalloc(*copy_size, TAG, "async data copy");
        if (!*data_copy) return ESP_ERR_NO_MEM;
        strcpy((char *)*data_copy, (char *)data);
    }
    else if (type == IOT_DAO_BLOB)
    {
        *copy_size = len;
        *data_copy = Mem_SafeMalloc(*copy_size, TAG, "async data copy");
        if (!*data_copy) return ESP_ERR_NO_MEM;
        memcpy(*data_copy, data, len);
    }
    else
    {
        switch (type)
        {
        case IOT_DAO_U8:  *copy_size = sizeof(uint8_t); break;
        case IOT_DAO_U16: *copy_size = sizeof(uint16_t); break;
        case IOT_DAO_U32: *copy_size = sizeof(uint32_t); break;
        case IOT_DAO_U64: *copy_size = sizeof(uint64_t); break;
        default: return ESP_ERR_INVALID_ARG;
        }
        *data_copy = Mem_SafeMalloc(*copy_size, TAG, "async data copy");
        if (!*data_copy) return ESP_ERR_NO_MEM;
        memcpy(*data_copy, data, *copy_size);
    }
    return ESP_OK;
}

esp_err_t DAO_SetDataAsync(const char *namespace, const char *key, IOT_DaoType type,
                           void *data, uint16_t len)
{
    return DAO_SetDataWithCallback(namespace, key, type, data, len, NULL, NULL);
}

esp_err_t DAO_SetDataWithCallback(const char *namespace, const char *key, IOT_DaoType type,
                                   void *data, uint16_t len,
                                   DAO_AsyncCallback_t callback, void *user_data)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!namespace || !key || !data)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Copy data for async processing
    void *data_copy = NULL;
    size_t copy_size = 0;
    esp_err_t err = DAO_CopyDataForAsync(type, data, len, &data_copy, &copy_size);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to copy data for async set");
        return err;
    }

    DAO_Request_t req = {
        .op = DAO_OP_SET_DATA,
        .type = type,
        .data = data_copy,
        .len = len,
        .free_data = true,
        .out_data = NULL,
        .out_len = NULL,
        .out_key_list = NULL,
        .out_key_count = NULL,
        .out_data_list = NULL,
        .out_data_sizes = NULL,
        .out_is_exist = NULL,
        .done_sem = NULL,           // No semaphore - async
        .result_ptr = NULL,
        .callback = callback,
        .user_data = user_data
    };

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.key[sizeof(req.key) - 1] = '\0';

    // Non-blocking send with minimal timeout
    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        SAFE_FREE(data_copy);
        IOT_LOGE(TAG, "Queue full, async set failed");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t DAO_RemoveDataByKeyAsync(const char *namespace, const char *key)
{
    return DAO_RemoveDataByKeyWithCallback(namespace, key, NULL, NULL);
}

esp_err_t DAO_RemoveDataByKeyWithCallback(const char *namespace, const char *key,
                                           DAO_AsyncCallback_t callback, void *user_data)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!namespace || !key)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DAO_Request_t req = {
        .op = DAO_OP_REMOVE_KEY,
        .type = IOT_DAO_BLOB,
        .data = NULL,
        .len = 0,
        .free_data = false,
        .out_data = NULL,
        .out_len = NULL,
        .out_key_list = NULL,
        .out_key_count = NULL,
        .out_data_list = NULL,
        .out_data_sizes = NULL,
        .out_is_exist = NULL,
        .done_sem = NULL,           // No semaphore - async
        .result_ptr = NULL,
        .callback = callback,
        .user_data = user_data
    };

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.key[sizeof(req.key) - 1] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Queue full, async remove key failed");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t DAO_RemoveNamespaceAsync(const char *namespace)
{
    return DAO_RemoveNamespaceWithCallback(namespace, NULL, NULL);
}

esp_err_t DAO_RemoveNamespaceWithCallback(const char *namespace,
                                           DAO_AsyncCallback_t callback, void *user_data)
{
    if (!dao_queue)
    {
        IOT_LOGE(TAG, "DAO not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!namespace)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DAO_Request_t req = {
        .op = DAO_OP_REMOVE_NAMESPACE,
        .type = IOT_DAO_BLOB,
        .data = NULL,
        .len = 0,
        .free_data = false,
        .out_data = NULL,
        .out_len = NULL,
        .out_key_list = NULL,
        .out_key_count = NULL,
        .out_data_list = NULL,
        .out_data_sizes = NULL,
        .out_is_exist = NULL,
        .done_sem = NULL,           // No semaphore - async
        .result_ptr = NULL,
        .callback = callback,
        .user_data = user_data
    };

    strncpy(req.namespace, namespace, sizeof(req.namespace) - 1);
    req.namespace[sizeof(req.namespace) - 1] = '\0';
    req.key[0] = '\0';

    if (xQueueSend(dao_queue, &req, pdMS_TO_TICKS(DAO_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        IOT_LOGE(TAG, "Queue full, async remove namespace failed");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}
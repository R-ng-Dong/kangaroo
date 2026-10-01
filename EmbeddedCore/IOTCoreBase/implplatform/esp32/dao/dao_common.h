#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "dao/IOT_DaoType.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Callback type for async operations
// ============================================================================

/**
 * @brief Callback function type for async operations
 * @param result Operation result
 * @param user_data User data passed to async function
 */
typedef void (*DAO_AsyncCallback_t)(esp_err_t result, void *user_data);

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize DAO storage system with dedicated task
 * Must be called before any other DAO functions
 * Creates queue and task for thread-safe operations
 * @return ESP_OK on success
 */
esp_err_t DAO_InitStorage(void);
/**
 * @brief Deinitialize DAO storage system
 * Cleans up task and queue
 * @return ESP_OK on success
 */
esp_err_t DAO_DeinitStorage(void);
// ============================================================================
// Synchronous API (BLOCKING - call from tasks only, NOT from callbacks)
// ============================================================================

/**
 * @brief Get all data from a namespace (BLOCKING)
 * @note Caller must free all allocated memory
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
esp_err_t DAO_GetNamespaceData(const char *namespace, char ***outKeyList, uint16_t *outKeyCount,
                               void ***outDataList, uint16_t **outDataSizes);

/**
 * @brief Get data from NVS storage (BLOCKING)
 * @note Caller must free *outData when done
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
esp_err_t DAO_GetData(const char *namespace, const char *key, IOT_DaoType type, 
                      void **outData, uint16_t *outLen);

/**
 * @brief Set data to NVS storage (BLOCKING)
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
esp_err_t DAO_SetData(const char *namespace, const char *key, IOT_DaoType type,
                      void *data, uint16_t len);

/**
 * @brief Set many keys under one namespace with a SINGLE commit (BLOCKING).
 *
 * Opens the namespace once, writes all @p count entries, and commits once —
 * avoiding the per-entry `nvs_commit` that N separate DAO_SetData calls incur.
 * The caller retains ownership of @p entries and their data for the call.
 *
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 * @return ESP_OK on success; first failing entry's error otherwise (nothing committed).
 */
esp_err_t DAO_SetDataBatch(const char *namespace, const IOT_DaoBatchEntry_t *entries, uint16_t count);

/**
 * @brief Get list of keys in namespace (BLOCKING)
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
esp_err_t DAO_GetKeyList(const char *namespace, char ***outKeyList, uint16_t *outKeyCount);

/**
 * @brief Remove data by key (BLOCKING)
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
esp_err_t DAO_RemoveDataByKey(const char *namespace, const char *key);

/**
 * @brief Remove entire namespace (BLOCKING)
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
esp_err_t DAO_RemoveNamespace(const char *namespace);

/**
 * @brief Check if key exists (BLOCKING)
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
esp_err_t DAO_CheckDataExistByKey(const char *namespace, const char *key, bool *isExist);

// ============================================================================
// Asynchronous API (NON-BLOCKING - safe to call from callbacks)
// ============================================================================

/**
 * @brief Set data to NVS storage asynchronously (NON-BLOCKING, fire-and-forget)
 * @param namespace Namespace
 * @param key Key
 * @param type Data type
 * @param data Data to store (will be copied internally)
 * @param len Data length (for BLOB type)
 * @return ESP_OK if queued successfully, error otherwise
 * @note Safe to call from BLE/MQTT callbacks
 * @note No confirmation of write success - use callback version if needed
 */
esp_err_t DAO_SetDataAsync(const char *namespace, const char *key, IOT_DaoType type,
                           void *data, uint16_t len);

/**
 * @brief Set data with callback notification (NON-BLOCKING)
 * @param namespace Namespace
 * @param key Key
 * @param type Data type
 * @param data Data to store (will be copied internally)
 * @param len Data length (for BLOB type)
 * @param callback Callback function (called from DAO task context)
 * @param user_data User data passed to callback
 * @return ESP_OK if queued successfully
 * @note Safe to call from BLE/MQTT callbacks
 * @note Callback is invoked from DAO task, not caller's context
 */
esp_err_t DAO_SetDataWithCallback(const char *namespace, const char *key, IOT_DaoType type,
                                   void *data, uint16_t len,
                                   DAO_AsyncCallback_t callback, void *user_data);

/**
 * @brief Remove data by key asynchronously (NON-BLOCKING, fire-and-forget)
 * @param namespace Namespace
 * @param key Key
 * @return ESP_OK if queued successfully
 * @note Safe to call from BLE/MQTT callbacks
 */
esp_err_t DAO_RemoveDataByKeyAsync(const char *namespace, const char *key);

/**
 * @brief Remove data by key with callback (NON-BLOCKING)
 * @param namespace Namespace
 * @param key Key
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return ESP_OK if queued successfully
 */
esp_err_t DAO_RemoveDataByKeyWithCallback(const char *namespace, const char *key,
                                           DAO_AsyncCallback_t callback, void *user_data);

/**
 * @brief Remove namespace asynchronously (NON-BLOCKING, fire-and-forget)
 * @param namespace Namespace
 * @return ESP_OK if queued successfully
 */
esp_err_t DAO_RemoveNamespaceAsync(const char *namespace);

/**
 * @brief Remove namespace with callback (NON-BLOCKING)
 * @param namespace Namespace
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return ESP_OK if queued successfully
 */
esp_err_t DAO_RemoveNamespaceWithCallback(const char *namespace,
                                           DAO_AsyncCallback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "dao/IOT_DaoType.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Abstract key-value storage interface for DAO modules.
 *
 * Platform implementation provides the backing store (e.g., NVS on ESP32).
 * All functions use iot_err_t for platform-independent error handling.
 */

// ============================================================================
// Synchronous API (BLOCKING - call from tasks only, NOT from callbacks)
// ============================================================================

/**
 * @brief Read data from persistent storage (BLOCKING)
 * @param ns       Namespace
 * @param key      Key
 * @param type     Data type
 * @param outData  Output data pointer (caller must free)
 * @param outLen   Output data length
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if key doesn't exist
 */
iot_err_t IOT_DaoStorageGet(const char *ns, const char *key, IOT_DaoType type,
                             void **outData, uint16_t *outLen);

/**
 * @brief Write data to persistent storage (BLOCKING)
 * @param ns    Namespace
 * @param key   Key
 * @param type  Data type
 * @param data  Data to store
 * @param len   Data length
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoStorageSet(const char *ns, const char *key, IOT_DaoType type,
                             void *data, uint16_t len);

/**
 * @brief Write many keys under one namespace with a SINGLE commit (BLOCKING).
 *
 * Prefer this over a loop of IOT_DaoStorageSet when writing a batch (e.g. a
 * provisioned code table): the backing store opens the namespace once and
 * commits once, instead of committing per key. Caller retains ownership of
 * @p entries and their data for the duration of the call.
 *
 * @param ns       Namespace
 * @param entries  Array of key/type/data/len entries
 * @param count    Number of entries
 * @return IOT_OK on success; nothing is committed if any entry fails.
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoStorageSetBatch(const char *ns, const IOT_DaoBatchEntry_t *entries, uint16_t count);

// ============================================================================
// Asynchronous API (NON-BLOCKING - safe to call from callbacks)
// ============================================================================

/**
 * @brief Write data to persistent storage asynchronously (NON-BLOCKING)
 * @param ns    Namespace
 * @param key   Key
 * @param type  Data type
 * @param data  Data to store (will be copied internally)
 * @param len   Data length
 * @return IOT_OK if queued successfully
 */
iot_err_t IOT_DaoStorageSetAsync(const char *ns, const char *key, IOT_DaoType type,
                                  void *data, uint16_t len);

// ============================================================================
// Key Operations
// ============================================================================

/**
 * @brief Remove a single key from persistent storage (BLOCKING)
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if key doesn't exist
 */
iot_err_t IOT_DaoStorageRemoveByKey(const char *ns, const char *key);

/**
 * @brief Remove a single key asynchronously (NON-BLOCKING)
 * @return IOT_OK if queued successfully
 */
iot_err_t IOT_DaoStorageRemoveByKeyAsync(const char *ns, const char *key);

/**
 * @brief Check if a key exists (BLOCKING)
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoStorageCheckExists(const char *ns, const char *key, bool *exists);

// ============================================================================
// Namespace Operations
// ============================================================================

/**
 * @brief Remove entire namespace from persistent storage (BLOCKING)
 * @param ns  Namespace to erase
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoStorageRemoveNamespace(const char *ns);

/**
 * @brief Get all keys in a namespace (BLOCKING)
 * @param ns           Namespace
 * @param outKeyList   Output array of key strings (caller must free each + array)
 * @param outKeyCount  Output key count
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoStorageGetKeyList(const char *ns, char ***outKeyList, uint16_t *outKeyCount);

/**
 * @brief Get all key-value pairs in a namespace (BLOCKING)
 * @param ns           Namespace
 * @param outKeyList   Output array of key strings (caller must free)
 * @param outKeyCount  Output key count
 * @param outDataList  Output array of data pointers (caller must free)
 * @param outDataSizes Output array of data sizes (caller must free)
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if namespace empty
 */
iot_err_t IOT_DaoStorageGetNamespaceData(const char *ns, char ***outKeyList, uint16_t *outKeyCount,
                                          void ***outDataList, uint16_t **outDataSizes);

#ifdef __cplusplus
}
#endif

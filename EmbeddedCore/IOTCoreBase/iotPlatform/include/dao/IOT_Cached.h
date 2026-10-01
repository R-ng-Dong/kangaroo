#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dao/IOT_DaoType.h"
#include "IOT_ErrorManager.h"
#ifdef __cplusplus
extern "C"
{
#endif

    // ============================================================================
    // Error codes (use same as iot_osal.h if already defined)
    // ============================================================================

#ifndef IOT_OK
    typedef enum
    {
        IOT_OK = 0,
        IOT_ERR_NO_MEM,
        IOT_ERR_INVALID_ARG,
        IOT_ERR_TIMEOUT,
        IOT_ERR_FAIL,
        IOT_ERR_INVALID_STATE,
        IOT_ERR_NOT_FOUND
    } iot_err_t;
#endif

    // ============================================================================
    // Configuration
    // ============================================================================

#define IOT_CACHE_MAX_NAMESPACES 8
#define IOT_CACHE_MAX_KEY_LEN 16
#define IOT_CACHE_MAX_NAMESPACE_LEN 16

    typedef enum
    {
        IOT_CACHE_WRITE_THROUGH, // Write to NVS immediately on set
        IOT_CACHE_WRITE_BACK     // Write to NVS only on flush or when evicted
    } IOT_CacheWritePolicy_t;

    typedef struct
    {
        const char *namespace;
        uint16_t maxEntries;           // Max cached items for this namespace
        uint32_t flushIntervalMs;      // Auto-flush interval (0 = manual only)
        IOT_CacheWritePolicy_t policy; // Write policy
    } IOT_CacheConfig_t;

    // ============================================================================
    // Cache Statistics (for debugging/monitoring)
    // ============================================================================

    typedef struct
    {
        uint32_t hits;
        uint32_t misses;
        uint32_t writes;
        uint32_t flushes;
        uint32_t evictions;
        uint16_t current_entries;
        uint16_t dirty_entries;
    } IOT_CacheStats_t;

    // ============================================================================
    // Initialization
    // ============================================================================

    /**
     * @brief Initialize the cache system
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheInit(void);

    /**
     * @brief Deinitialize the cache system and flush all dirty data
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheDeinit(void);

    /**
     * @brief Register a namespace for caching
     * @param config Namespace configuration
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheRegisterNamespace(const IOT_CacheConfig_t *config);

    /**
     * @brief Unregister a namespace (flushes dirty data first)
     * @param namespace Namespace to unregister
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheUnregisterNamespace(const char *namespace);

    // ============================================================================
    // Data Access
    // ============================================================================

    /**
     * @brief Get data from cache (or NVS if not cached)
     * @param namespace Namespace
     * @param key Key
     * @param type Data type
     * @param outData Output data pointer (caller must free)
     * @param outLen Output data length (required for STRING/BLOB)
     * @return IOT_OK on success, IOT_ERR_NOT_FOUND if key doesn't exist
     */
    iot_err_t IOT_CacheGet(const char *namespace, const char *key, IOT_DaoType type, void **outData, uint16_t *outLen);

    /**
     * @brief Set data to cache (and optionally NVS based on policy)
     * @param namespace Namespace
     * @param key Key
     * @param type Data type
     * @param data Data to store
     * @param len Data length (for BLOB type)
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheSet(const char *namespace, const char *key, IOT_DaoType type, void *data, uint16_t len);

    /**
     * @brief Delete data from cache and NVS
     * @param namespace Namespace
     * @param key Key
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheDelete(const char *namespace, const char *key);

    /**
     * @brief Check if key exists in cache or NVS
     * @param namespace Namespace
     * @param key Key
     * @param exists Output: true if exists
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheExists(const char *namespace, const char *key, bool *exists);

    // ============================================================================
    // Cache Control
    // ============================================================================

    /**
     * @brief Flush dirty entries to NVS
     * @param namespace Namespace to flush (NULL = flush all namespaces)
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheFlush(const char *namespace);

    /**
     * @brief Invalidate cache entry (will reload from NVS on next access)
     * @param namespace Namespace
     * @param key Key (NULL = invalidate entire namespace)
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheInvalidate(const char *namespace, const char *key);

    /**
     * @brief Preload all keys from namespace into cache
     * @param namespace Namespace to preload
     * @return IOT_OK on success
     */
    iot_err_t IOT_CachePreload(const char *namespace);

    /**
     * @brief Clear all cache entries (optionally flush first)
     * @param flush_first If true, flush dirty entries before clearing
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheClear(bool flush_first);

    // ============================================================================
    // Statistics & Debugging
    // ============================================================================

    /**
     * @brief Get cache statistics for a namespace
     * @param namespace Namespace (NULL = global stats)
     * @param stats Output statistics
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheGetStats(const char *namespace, IOT_CacheStats_t *stats);

    /**
     * @brief Reset cache statistics
     * @param namespace Namespace (NULL = reset all)
     * @return IOT_OK on success
     */
    iot_err_t IOT_CacheResetStats(const char *namespace);

    /**
     * @brief Print cache status to log (for debugging)
     * @param namespace Namespace (NULL = print all)
     */
    void IOT_CachePrintStatus(const char *namespace);

#ifdef __cplusplus
}
#endif
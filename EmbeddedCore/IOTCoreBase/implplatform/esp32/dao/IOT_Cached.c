#include "dao/IOT_Cached.h"
#include "IOT_Memory.h"
#include "IOT_Osal.h"
#include "dao_common.h"
#include "nvs.h"
#include "IOT_Log.h"
#include <string.h>
#include <stdlib.h>
#if defined(CONFIG_IOT_HEAP_PROBE_ENABLED) && defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "iot_cached";

// ============================================================================
// Internal structures
// ============================================================================

typedef struct IOT_CacheEntry
{
    char key[IOT_CACHE_MAX_KEY_LEN];
    IOT_DaoType type;
    void *data;
    uint16_t data_len;
    bool dirty;                  // Modified but not written to NVS
    bool valid;                  // Entry is in use
    uint32_t last_access;        // For LRU eviction
    struct IOT_CacheEntry *next; // For hash chain
    struct IOT_CacheEntry *lru_prev;
    struct IOT_CacheEntry *lru_next;
} IOT_CacheEntry_t;

typedef struct
{
    char namespace[IOT_CACHE_MAX_NAMESPACE_LEN];
    IOT_CacheConfig_t config;
    IOT_CacheEntry_t *entries;        // Array of entries
    IOT_CacheEntry_t *hash_table[16]; // Simple hash table for fast lookup
    IOT_CacheEntry_t *lru_head;       // Most recently used
    IOT_CacheEntry_t *lru_tail;       // Least recently used
    uint16_t entry_count;
    IOT_CacheStats_t stats;
    IOT_OsalTimerHandle_t flush_timer;
    volatile bool flush_pending;   /* set by the timer cb, cleared by the flush worker */
    bool registered;
} IOT_CacheNamespace_t;

typedef struct
{
    IOT_CacheNamespace_t namespaces[IOT_CACHE_MAX_NAMESPACES];
    IOT_OsalMutexHandle_t mutex;
    bool initialized;
    IOT_CacheStats_t global_stats;
} IOT_CacheContext_t;

static IOT_CacheContext_t s_cache_ctx = {0};

// ============================================================================
// Helper functions
// ============================================================================

static uint8_t hash_key(const char *key)
{
    uint32_t hash = 5381;
    int c;
    while ((c = *key++))
    {
        hash = ((hash << 5) + hash) + c;
    }
    return hash & 0x0F; // 16 buckets
}

static IOT_CacheNamespace_t *find_namespace(const char *namespace)
{
    for (int i = 0; i < IOT_CACHE_MAX_NAMESPACES; i++)
    {
        if (s_cache_ctx.namespaces[i].registered && strcmp(s_cache_ctx.namespaces[i].namespace, namespace) == 0)
        {
            return &s_cache_ctx.namespaces[i];
        }
    }
    return NULL;
}

static IOT_CacheEntry_t *find_entry(IOT_CacheNamespace_t *ns, const char *key)
{
    uint8_t hash = hash_key(key);
    IOT_CacheEntry_t *entry = ns->hash_table[hash];

    while (entry)
    {
        if (entry->valid && strcmp(entry->key, key) == 0)
        {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static void lru_remove(IOT_CacheNamespace_t *ns, IOT_CacheEntry_t *entry)
{
    if (entry->lru_prev)
    {
        entry->lru_prev->lru_next = entry->lru_next;
    }
    else
    {
        ns->lru_head = entry->lru_next;
    }

    if (entry->lru_next)
    {
        entry->lru_next->lru_prev = entry->lru_prev;
    }
    else
    {
        ns->lru_tail = entry->lru_prev;
    }

    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

static void lru_add_front(IOT_CacheNamespace_t *ns, IOT_CacheEntry_t *entry)
{
    entry->lru_prev = NULL;
    entry->lru_next = ns->lru_head;

    if (ns->lru_head)
    {
        ns->lru_head->lru_prev = entry;
    }
    ns->lru_head = entry;

    if (!ns->lru_tail)
    {
        ns->lru_tail = entry;
    }
}

static void lru_move_to_front(IOT_CacheNamespace_t *ns, IOT_CacheEntry_t *entry)
{
    if (entry == ns->lru_head)
    {
        return; // Already at front
    }
    lru_remove(ns, entry);
    lru_add_front(ns, entry);
}

static void hash_add(IOT_CacheNamespace_t *ns, IOT_CacheEntry_t *entry)
{
    uint8_t hash = hash_key(entry->key);
    entry->next = ns->hash_table[hash];
    ns->hash_table[hash] = entry;
}

static void hash_remove(IOT_CacheNamespace_t *ns, IOT_CacheEntry_t *entry)
{
    uint8_t hash = hash_key(entry->key);
    IOT_CacheEntry_t **pp = &ns->hash_table[hash];

    while (*pp)
    {
        if (*pp == entry)
        {
            *pp = entry->next;
            entry->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }
}

static void free_entry_data(IOT_CacheEntry_t *entry)
{
    if (entry->data)
    {
        SAFE_FREE(entry->data);
        entry->data = NULL;
    }
    entry->data_len = 0;
}

static iot_err_t flush_entry(IOT_CacheNamespace_t *ns, IOT_CacheEntry_t *entry)
{
    if (!entry->valid || !entry->dirty)
    {
        return IOT_OK;
    }

    esp_err_t err = DAO_SetData(ns->namespace, entry->key, entry->type, entry->data, entry->data_len);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to flush entry %s/%s: %s", ns->namespace, entry->key, esp_err_to_name(err));
        return IOT_ERR_FAIL;
    }

    entry->dirty = false;
    ns->stats.flushes++;
    s_cache_ctx.global_stats.flushes++;

    if (ns->stats.dirty_entries > 0)
    {
        ns->stats.dirty_entries--;
    }

    return IOT_OK;
}

static iot_err_t evict_lru(IOT_CacheNamespace_t *ns)
{
    IOT_CacheEntry_t *victim = ns->lru_tail;

    if (!victim)
    {
        return IOT_ERR_FAIL;
    }

    // Flush if dirty
    if (victim->dirty)
    {
        iot_err_t err = flush_entry(ns, victim);
        if (err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to flush during eviction");
            return err;
        }
    }

    // Remove from data structures
    lru_remove(ns, victim);
    hash_remove(ns, victim);
    free_entry_data(victim);

    victim->valid = false;
    victim->key[0] = '\0';
    ns->entry_count--;
    ns->stats.evictions++;
    s_cache_ctx.global_stats.evictions++;

    IOT_LOGD(TAG, "Evicted entry from %s, count=%d", ns->namespace, ns->entry_count);

    return IOT_OK;
}

static IOT_CacheEntry_t *allocate_entry(IOT_CacheNamespace_t *ns)
{
    // Find free slot
    for (int i = 0; i < ns->config.maxEntries; i++)
    {
        if (!ns->entries[i].valid)
        {
            return &ns->entries[i];
        }
    }

    // Need to evict
    if (evict_lru(ns) == IOT_OK)
    {
        // Try again
        for (int i = 0; i < ns->config.maxEntries; i++)
        {
            if (!ns->entries[i].valid)
            {
                return &ns->entries[i];
            }
        }
    }

    return NULL;
}

static size_t get_type_size(IOT_DaoType type)
{
    switch (type)
    {
    case IOT_DAO_U8:
        return sizeof(uint8_t);
    case IOT_DAO_U16:
        return sizeof(uint16_t);
    case IOT_DAO_U32:
        return sizeof(uint32_t);
    case IOT_DAO_U64:
        return sizeof(uint64_t);
    default:
        return 0; // STRING and BLOB have variable size
    }
}

static iot_err_t copy_data(void **dest, uint16_t *dest_len, IOT_DaoType type, void *src, uint16_t src_len)
{
    size_t size;

    if (type == IOT_DAO_STRING)
    {
        size = strlen((char *) src) + 1;
    }
    else if (type == IOT_DAO_BLOB)
    {
        size = src_len;
    }
    else
    {
        size = get_type_size(type);
    }

    if (size == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    *dest = Mem_SafeMalloc(size, TAG, "cache data copy");
    if (!*dest)
    {
        return IOT_ERR_NO_MEM;
    }

    memcpy(*dest, src, size);
    *dest_len = (uint16_t) size;

    return IOT_OK;
}

/* ---------------------------------------------------------------------------
 * Auto-flush: request on the timer, WRITE on a worker task
 *
 * IOT_CacheFlush() serializes each dirty entry and calls DAO_SetData(), which hands
 * the write to the DAO task and blocks on a semaphore until the NVS write completes.
 * That does not fit the FreeRTOS timer service task's stack: doing it inline from the
 * timer callback crash-looped all three bench boards on 2026-08-04 ("Stack protection
 * fault ... task Tmr Svc"), and one board reached 7/8 on the boot counter before it
 * was halted. So the timer only raises a flag and pokes a worker that owns a stack
 * sized for the job.
 * ------------------------------------------------------------------------- */

#define CACHE_FLUSH_TASK_STACK 4096
#define CACHE_FLUSH_TASK_PRIO  4

static IOT_OsalTaskHandle_t s_flush_task = NULL;
static IOT_OsalSemaphoreHandle_t s_flush_signal = NULL;

/* Flush every namespace whose timer asked for it. Runs on the worker task (and is
 * called directly by host tests, which have no tasks). */
static void cache_flush_pending(void)
{
    for (int i = 0; i < IOT_CACHE_MAX_NAMESPACES; i++)
    {
        IOT_CacheNamespace_t *ns = &s_cache_ctx.namespaces[i];
        if (ns->registered && ns->flush_pending)
        {
            ns->flush_pending = false;
            IOT_CacheFlush(ns->namespace);
        }
    }
}

static void cache_flush_task(void *param)
{
    (void) param;
    for (;;)
    {
        if (IOT_OsalSemaphoreTake(s_flush_signal, UINT32_MAX) == IOT_OK)
        {
            cache_flush_pending();
#if defined(CONFIG_IOT_HEAP_PROBE_ENABLED) && defined(ESP_PLATFORM)
            /* The previous attempt at write-back died of stack exhaustion in the timer
             * task, so do not assume this one fits either.
             *
             * Report the RUNNING MINIMUM, not the first sample: the first flush after boot
             * usually has nothing dirty and reports almost the full stack free, which says
             * nothing about the worst case. Only a flush carrying many dirty entries
             * exercises the depth we actually need to size for. */
            static uint32_t s_min_free = UINT32_MAX;
            uint32_t freeNow = (uint32_t) (uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
            if (freeNow < s_min_free)
            {
                s_min_free = freeNow;
                IOT_LOGW(TAG, "flush worker stack: new low %u bytes unused of %u",
                         (unsigned) freeNow, (unsigned) CACHE_FLUSH_TASK_STACK);
            }
#endif
        }
    }
}

static void flush_timer_callback(IOT_OsalTimerHandle_t timer, void *param)
{
    (void) timer;
    IOT_CacheNamespace_t *ns = (IOT_CacheNamespace_t *) param;

    /* Nothing dirty means nothing to write, so do not wake the worker at all — otherwise
     * an idle device pays a task wake plus a full namespace scan on every tick, forever.
     * This matters more at a 1 s interval than it did at 5 s. */
    if (ns && ns->registered && ns->stats.dirty_entries > 0)
    {
        /* Flag + poke only. No serialization, no DAO round-trip, no blocking —
         * this runs on the timer service task. */
        ns->flush_pending = true;
        if (s_flush_signal)
        {
            IOT_OsalSemaphoreGive(s_flush_signal);
        }
    }
}

/* Create the worker on first use. Returns false if it could not be created, which
 * the caller treats as "auto-flush unavailable" rather than flushing inline. */
static bool cache_flush_worker_ensure(void)
{
    if (s_flush_task != NULL)
    {
        return true;
    }
    if (s_flush_signal == NULL)
    {
        s_flush_signal = IOT_OsalSemaphoreCreateBinary();
        if (s_flush_signal == NULL)
        {
            IOT_LOGE(TAG, "Failed to create cache flush signal");
            return false;
        }
    }
    IOT_OsalTaskConfig_t cfg = {
        .name = "cache_flush",
        .stack_size = CACHE_FLUSH_TASK_STACK,
        .priority = CACHE_FLUSH_TASK_PRIO,
        .param = NULL,
    };
    if (IOT_OsalTaskCreate(cache_flush_task, &cfg, &s_flush_task) != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to create cache flush task");
        s_flush_task = NULL;
        return false;
    }
    return true;
}

// ============================================================================
// Public API Implementation
// ============================================================================

iot_err_t IOT_CacheInit(void)
{
    if (s_cache_ctx.initialized)
    {
        IOT_LOGW(TAG, "Cache already initialized");
        return IOT_OK;
    }

    memset(&s_cache_ctx, 0, sizeof(s_cache_ctx));

    s_cache_ctx.mutex = IOT_OsalMutexCreate();
    if (!s_cache_ctx.mutex)
    {
        IOT_LOGE(TAG, "Failed to create cache mutex");
        return IOT_ERR_NO_MEM;
    }

    s_cache_ctx.initialized = true;
    IOT_LOGI(TAG, "Cache system initialized");

    return IOT_OK;
}

iot_err_t IOT_CacheDeinit(void)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_OK;
    }
    // Flush all dirty data
    IOT_CacheFlush(NULL);

    // Free all namespaces
    for (int i = 0; i < IOT_CACHE_MAX_NAMESPACES; i++)
    {
        if (s_cache_ctx.namespaces[i].registered)
        {
            IOT_CacheUnregisterNamespace(s_cache_ctx.namespaces[i].namespace);
        }
    }

    if (s_cache_ctx.mutex)
    {
        IOT_OsalMutexDelete(s_cache_ctx.mutex);
        s_cache_ctx.mutex = NULL;
    }

    s_cache_ctx.initialized = false;
    IOT_LOGI(TAG, "Cache system deinitialized");

    return IOT_OK;
}

iot_err_t IOT_CacheRegisterNamespace(const IOT_CacheConfig_t *config)
{
    if (!s_cache_ctx.initialized)
    {
        IOT_LOGE(TAG, "Cache not initialized");
        return IOT_ERR_INVALID_STATE;
    }

    if (!config || !config->namespace || config->maxEntries == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (strlen(config->namespace) >= IOT_CACHE_MAX_NAMESPACE_LEN)
    {
        IOT_LOGE(TAG, "Namespace name too long");
        return IOT_ERR_INVALID_ARG;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    // Check if already registered
    if (find_namespace(config->namespace))
    {
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
        IOT_LOGW(TAG, "Namespace %s already registered", config->namespace);
        return IOT_OK;
    }

    // Find free slot
    IOT_CacheNamespace_t *ns = NULL;
    for (int i = 0; i < IOT_CACHE_MAX_NAMESPACES; i++)
    {
        if (!s_cache_ctx.namespaces[i].registered)
        {
            ns = &s_cache_ctx.namespaces[i];
            break;
        }
    }

    if (!ns)
    {
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
        IOT_LOGE(TAG, "No free namespace slots");
        return IOT_ERR_NO_MEM;
    }

    // Initialize namespace
    memset(ns, 0, sizeof(IOT_CacheNamespace_t));
    strncpy(ns->namespace, config->namespace, IOT_CACHE_MAX_NAMESPACE_LEN - 1);
    ns->namespace[IOT_CACHE_MAX_NAMESPACE_LEN - 1] = '\0';
    memcpy(&ns->config, config, sizeof(IOT_CacheConfig_t));
    ns->config.namespace = ns->namespace; // Point to our copy

    // Allocate entries
    ns->entries = Mem_SafeCalloc(config->maxEntries, sizeof(IOT_CacheEntry_t), TAG, "cache entries");
    if (!ns->entries)
    {
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
        IOT_LOGE(TAG, "Failed to allocate cache entries");
        return IOT_ERR_NO_MEM;
    }

    // Create flush timer if needed
    if (config->flushIntervalMs > 0 && config->policy == IOT_CACHE_WRITE_BACK)
    {
        /* The worker must exist before the timer can fire, or the first tick would
         * raise a flag nobody drains. */
        if (!cache_flush_worker_ensure())
        {
            IOT_LOGE(TAG, "No flush worker — auto-flush disabled for %s", config->namespace);
        }

        ns->flush_timer = IOT_OsalTimerCreate("cache_flush",
                                              config->flushIntervalMs,
                                              true, // Auto-reload
                                              flush_timer_callback,
                                              ns);

        if (ns->flush_timer)
        {
            IOT_OsalTimerStart(ns->flush_timer, 100);
        }
    }

    ns->registered = true;

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    IOT_LOGI(TAG,
             "Registered namespace: %s (max=%d, policy=%s)",
             config->namespace,
             config->maxEntries,
             config->policy == IOT_CACHE_WRITE_THROUGH ? "write-through" : "write-back");

    return IOT_OK;
}

iot_err_t IOT_CacheUnregisterNamespace(const char *namespace)
{
    if (!s_cache_ctx.initialized || !namespace)
    {
        return IOT_ERR_INVALID_ARG;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    IOT_CacheNamespace_t *ns = find_namespace(namespace);
    if (!ns)
    {
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
        return IOT_ERR_NOT_FOUND;
    }

    // Flush dirty entries
    for (int i = 0; i < ns->config.maxEntries; i++)
    {
        if (ns->entries[i].valid && ns->entries[i].dirty)
        {
            flush_entry(ns, &ns->entries[i]);
        }
        free_entry_data(&ns->entries[i]);
    }

    // Stop and delete timer
    if (ns->flush_timer)
    {
        IOT_OsalTimerStop(ns->flush_timer, 100);
        IOT_OsalTimerDelete(ns->flush_timer);
        ns->flush_timer = NULL;
    }

    // Free entries array
    SAFE_FREE(ns->entries);
    ns->entries = NULL;
    ns->registered = false;

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    IOT_LOGI(TAG, "Unregistered namespace: %s", namespace);

    return IOT_OK;
}

iot_err_t IOT_CacheGet(const char *namespace, const char *key, IOT_DaoType type, void **outData, uint16_t *outLen)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_ERR_INVALID_STATE;
    }

    if (!namespace || !key || !outData)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if ((type == IOT_DAO_STRING || type == IOT_DAO_BLOB) && !outLen)
    {
        return IOT_ERR_INVALID_ARG;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    IOT_CacheNamespace_t *ns = find_namespace(namespace);

    // If namespace not registered, go directly to DAO
    if (!ns)
    {
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
        esp_err_t err = DAO_GetData(namespace, key, type, outData, outLen);
        return (err == ESP_OK) ? IOT_OK : (err == ESP_ERR_NVS_NOT_FOUND) ? IOT_ERR_NOT_FOUND : IOT_ERR_FAIL;
    }

    // Look in cache first
    IOT_CacheEntry_t *entry = find_entry(ns, key);

    if (entry)
    {
        // Cache hit
        ns->stats.hits++;
        s_cache_ctx.global_stats.hits++;
        entry->last_access = IOT_OsalGetTickCount();
        lru_move_to_front(ns, entry);

        // Copy data to output
        iot_err_t err = copy_data(outData, outLen, entry->type, entry->data, entry->data_len);

        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
        return err;
    }

    // Cache miss - load from NVS
    ns->stats.misses++;
    s_cache_ctx.global_stats.misses++;

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    void *data = NULL;
    uint16_t len = 0;
    esp_err_t err = DAO_GetData(namespace, key, type, &data, &len);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return IOT_ERR_NOT_FOUND;
    }
    else if (err != ESP_OK)
    {
        return IOT_ERR_FAIL;
    }

    // Add to cache
    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    ns = find_namespace(namespace); // Re-find after unlock
    if (ns)
    {
        entry = allocate_entry(ns);
        if (entry)
        {
            strncpy(entry->key, key, IOT_CACHE_MAX_KEY_LEN - 1);
            entry->type = type;
            entry->data = data;
            entry->data_len = len;
            entry->dirty = false;
            entry->valid = true;
            entry->last_access = IOT_OsalGetTickCount();

            hash_add(ns, entry);
            lru_add_front(ns, entry);
            ns->entry_count++;
            ns->stats.current_entries = ns->entry_count;

            // Copy for output (we gave original to cache)
            iot_err_t copy_err = copy_data(outData, outLen, type, data, len);
            IOT_OsalMutexUnlock(s_cache_ctx.mutex);
            return copy_err;
        }
    }

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    // Couldn't cache, just return loaded data
    *outData = data;
    if (outLen)
    {
        *outLen = len;
    }

    return IOT_OK;
}

iot_err_t IOT_CacheSet(const char *namespace, const char *key, IOT_DaoType type, void *data, uint16_t len)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_ERR_INVALID_STATE;
    }

    if (!namespace || !key || !data)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (strlen(key) >= IOT_CACHE_MAX_KEY_LEN)
    {
        IOT_LOGE(TAG, "Key too long: %s", key);
        return IOT_ERR_INVALID_ARG;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    IOT_CacheNamespace_t *ns = find_namespace(namespace);

    // If namespace not registered, go directly to DAO
    if (!ns)
    {
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
        esp_err_t err = DAO_SetData(namespace, key, type, data, len);
        return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
    }

    // Look for existing entry
    IOT_CacheEntry_t *entry = find_entry(ns, key);

    if (!entry)
    {
        // Allocate new entry
        entry = allocate_entry(ns);
        if (!entry)
        {
            IOT_OsalMutexUnlock(s_cache_ctx.mutex);
            IOT_LOGE(TAG, "Failed to allocate cache entry");
            return IOT_ERR_NO_MEM;
        }

        strncpy(entry->key, key, IOT_CACHE_MAX_KEY_LEN - 1);
        entry->valid = true;
        hash_add(ns, entry);
        lru_add_front(ns, entry);
        ns->entry_count++;
        ns->stats.current_entries = ns->entry_count;
    }
    else
    {
        // Update existing - free old data
        free_entry_data(entry);
        lru_move_to_front(ns, entry);
    }

    // Copy new data
    iot_err_t err = copy_data(&entry->data, &entry->data_len, type, data, len);
    if (err != IOT_OK)
    {
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
        return err;
    }

    entry->type = type;
    entry->last_access = IOT_OsalGetTickCount();

    ns->stats.writes++;
    s_cache_ctx.global_stats.writes++;

    // Handle write policy
    if (ns->config.policy == IOT_CACHE_WRITE_THROUGH)
    {
        // Write immediately
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);

        esp_err_t dao_err = DAO_SetData(namespace, key, type, data, len);
        if (dao_err != ESP_OK)
        {
            IOT_LOGE(TAG, "Write-through failed for %s/%s", namespace, key);
            return IOT_ERR_FAIL;
        }
        entry->dirty = false;
    }
    else
    {
        // Write-back: mark dirty
        if (!entry->dirty)
        {
            entry->dirty = true;
            ns->stats.dirty_entries++;
        }
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
    }

    return IOT_OK;
}

iot_err_t IOT_CacheDelete(const char *namespace, const char *key)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_ERR_INVALID_STATE;
    }

    if (!namespace || !key)
    {
        return IOT_ERR_INVALID_ARG;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    IOT_CacheNamespace_t *ns = find_namespace(namespace);

    if (ns)
    {
        IOT_CacheEntry_t *entry = find_entry(ns, key);
        if (entry)
        {
            lru_remove(ns, entry);
            hash_remove(ns, entry);
            free_entry_data(entry);

            if (entry->dirty && ns->stats.dirty_entries > 0)
            {
                ns->stats.dirty_entries--;
            }

            entry->valid = false;
            entry->dirty = false;
            entry->key[0] = '\0';
            ns->entry_count--;
            ns->stats.current_entries = ns->entry_count;
        }
    }

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    // Delete from NVS
    esp_err_t err = DAO_RemoveDataByKey(namespace, key);

    return (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_CacheExists(const char *namespace, const char *key, bool *exists)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_ERR_INVALID_STATE;
    }

    if (!namespace || !key || !exists)
    {
        return IOT_ERR_INVALID_ARG;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    IOT_CacheNamespace_t *ns = find_namespace(namespace);

    if (ns)
    {
        IOT_CacheEntry_t *entry = find_entry(ns, key);
        if (entry)
        {
            *exists = true;
            IOT_OsalMutexUnlock(s_cache_ctx.mutex);
            return IOT_OK;
        }
    }

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    // Check NVS
    esp_err_t err = DAO_CheckDataExistByKey(namespace, key, exists);

    return (err == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_CacheFlush(const char *namespace)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_ERR_INVALID_STATE;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    iot_err_t result = IOT_OK;

    for (int i = 0; i < IOT_CACHE_MAX_NAMESPACES; i++)
    {
        IOT_CacheNamespace_t *ns = &s_cache_ctx.namespaces[i];

        if (!ns->registered)
        {
            continue;
        }

        if (namespace && strcmp(ns->namespace, namespace) != 0)
        {
            continue;
        }
        // IOT_LOGI(TAG, "Flushing cache namespace: %s", ns->namespace);
        for (int j = 0; j < ns->config.maxEntries; j++)
        {
            IOT_CacheEntry_t *entry = &ns->entries[j];
            if (entry->valid && entry->dirty)
            {
                if (flush_entry(ns, entry) != IOT_OK)
                {
                    result = IOT_ERR_FAIL;
                }
            }
        }

        if (namespace)
        {
            break; // Only flush specified namespace
        }
    }

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    return result;
}

iot_err_t IOT_CacheInvalidate(const char *namespace, const char *key)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_ERR_INVALID_STATE;
    }

    if (!namespace)
    {
        return IOT_ERR_INVALID_ARG;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    IOT_CacheNamespace_t *ns = find_namespace(namespace);
    if (!ns)
    {
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);
        return IOT_ERR_NOT_FOUND;
    }

    if (key)
    {
        // Invalidate single entry
        IOT_CacheEntry_t *entry = find_entry(ns, key);
        if (entry)
        {
            // Flush if dirty before invalidating
            if (entry->dirty)
            {
                flush_entry(ns, entry);
            }

            lru_remove(ns, entry);
            hash_remove(ns, entry);
            free_entry_data(entry);
            entry->valid = false;
            entry->key[0] = '\0';
            ns->entry_count--;
            ns->stats.current_entries = ns->entry_count;
        }
    }
    else
    {
        // Invalidate entire namespace
        for (int i = 0; i < ns->config.maxEntries; i++)
        {
            IOT_CacheEntry_t *entry = &ns->entries[i];

            if (entry->valid)
            {
                if (entry->dirty)
                {
                    flush_entry(ns, entry);
                }

                lru_remove(ns, entry);
                hash_remove(ns, entry);
                free_entry_data(entry);
                entry->valid = false;
                entry->key[0] = '\0';
            }
        }
        ns->entry_count = 0;
        ns->stats.current_entries = 0;
        ns->stats.dirty_entries = 0;
        ns->lru_head = NULL;
        ns->lru_tail = NULL;
        memset(ns->hash_table, 0, sizeof(ns->hash_table));
    }

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    return IOT_OK;
}

iot_err_t IOT_CachePreload(const char *namespace)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_ERR_INVALID_STATE;
    }

    if (!namespace)
    {
        return IOT_ERR_INVALID_ARG;
    }

    // Get all data from namespace
    char **keyList = NULL;
    uint16_t keyCount = 0;
    void **dataList = NULL;
    uint16_t *dataSizes = NULL;

    esp_err_t err = DAO_GetNamespaceData(namespace, &keyList, &keyCount, &dataList, &dataSizes);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        IOT_LOGI(TAG, "Namespace %s is empty, nothing to preload", namespace);
        return IOT_OK;
    }
    else if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to get namespace data for preload: %s", namespace);
        return IOT_ERR_FAIL;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    IOT_CacheNamespace_t *ns = find_namespace(namespace);
    if (!ns)
    {
        IOT_OsalMutexUnlock(s_cache_ctx.mutex);

        // Free loaded data
        for (int i = 0; i < keyCount; i++)
        {
            SAFE_FREE(keyList[i]);
            SAFE_FREE(dataList[i]);
        }
        SAFE_FREE(keyList);
        SAFE_FREE(dataList);
        SAFE_FREE(dataSizes);

        IOT_LOGW(TAG, "Namespace %s not registered for caching", namespace);
        return IOT_ERR_NOT_FOUND;
    }

    uint16_t loaded = 0;

    for (int i = 0; i < keyCount && loaded < ns->config.maxEntries; i++)
    {
        // Skip if already in cache
        if (find_entry(ns, keyList[i]))
        {
            SAFE_FREE(keyList[i]);
            SAFE_FREE(dataList[i]);
            continue;
        }

        IOT_CacheEntry_t *entry = allocate_entry(ns);
        if (!entry)
        {
            IOT_LOGW(TAG, "Cache full during preload, loaded %d/%d", loaded, keyCount);
            SAFE_FREE(keyList[i]);
            SAFE_FREE(dataList[i]);
            continue;
        }

        strncpy(entry->key, keyList[i], IOT_CACHE_MAX_KEY_LEN - 1);
        entry->type = IOT_DAO_BLOB; // Namespace data comes as blob
        entry->data = dataList[i];  // Take ownership
        entry->data_len = dataSizes[i];
        entry->dirty = false;
        entry->valid = true;
        entry->last_access = IOT_OsalGetTickCount();

        hash_add(ns, entry);
        lru_add_front(ns, entry);
        ns->entry_count++;
        loaded++;

        SAFE_FREE(keyList[i]); // Free key string (we copied it)
        // Don't free dataList[i] - entry took ownership
    }

    ns->stats.current_entries = ns->entry_count;

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    // Free remaining data if any
    for (int i = loaded; i < keyCount; i++)
    {
        if (keyList[i])
            SAFE_FREE(keyList[i]);
        if (dataList[i])
            SAFE_FREE(dataList[i]);
    }
    SAFE_FREE(keyList);
    SAFE_FREE(dataList);
    SAFE_FREE(dataSizes);

    IOT_LOGI(TAG, "Preloaded %d entries for namespace %s", loaded, namespace);

    return IOT_OK;
}

iot_err_t IOT_CacheClear(bool flush_first)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_ERR_INVALID_STATE;
    }

    if (flush_first)
    {
        IOT_CacheFlush(NULL);
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    for (int i = 0; i < IOT_CACHE_MAX_NAMESPACES; i++)
    {
        IOT_CacheNamespace_t *ns = &s_cache_ctx.namespaces[i];

        if (!ns->registered)
        {
            continue;
        }

        for (int j = 0; j < ns->config.maxEntries; j++)
        {
            IOT_CacheEntry_t *entry = &ns->entries[j];

            if (entry->valid)
            {
                free_entry_data(entry);
                entry->valid = false;
                entry->dirty = false;
                entry->key[0] = '\0';
            }
        }

        ns->entry_count = 0;
        ns->lru_head = NULL;
        ns->lru_tail = NULL;
        memset(ns->hash_table, 0, sizeof(ns->hash_table));
        ns->stats.current_entries = 0;
        ns->stats.dirty_entries = 0;
    }

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    IOT_LOGI(TAG, "Cache cleared");

    return IOT_OK;
}

iot_err_t IOT_CacheGetStats(const char *namespace, IOT_CacheStats_t *stats)
{
    if (!s_cache_ctx.initialized || !stats)
    {
        return IOT_ERR_INVALID_ARG;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    if (!namespace)
    {
        memcpy(stats, &s_cache_ctx.global_stats, sizeof(IOT_CacheStats_t));

        // Calculate totals
        stats->current_entries = 0;
        stats->dirty_entries = 0;
        for (int i = 0; i < IOT_CACHE_MAX_NAMESPACES; i++)
        {
            if (s_cache_ctx.namespaces[i].registered)
            {
                stats->current_entries += s_cache_ctx.namespaces[i].stats.current_entries;
                stats->dirty_entries += s_cache_ctx.namespaces[i].stats.dirty_entries;
            }
        }
    }
    else
    {
        IOT_CacheNamespace_t *ns = find_namespace(namespace);
        IOT_LOGE(TAG, "Getting stats for namespace: %s", ns ? ns->namespace : "NULL");
        if (!ns)
        {
            IOT_OsalMutexUnlock(s_cache_ctx.mutex);
            return IOT_ERR_NOT_FOUND;
        }
        memcpy(stats, &ns->stats, sizeof(IOT_CacheStats_t));
    }

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    return IOT_OK;
}

iot_err_t IOT_CacheResetStats(const char *namespace)
{
    if (!s_cache_ctx.initialized)
    {
        return IOT_ERR_INVALID_STATE;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    if (!namespace)
    {
        // Reset all
        memset(&s_cache_ctx.global_stats, 0, sizeof(IOT_CacheStats_t));
        for (int i = 0; i < IOT_CACHE_MAX_NAMESPACES; i++)
        {
            if (s_cache_ctx.namespaces[i].registered)
            {
                uint16_t current = s_cache_ctx.namespaces[i].stats.current_entries;
                uint16_t dirty = s_cache_ctx.namespaces[i].stats.dirty_entries;
                memset(&s_cache_ctx.namespaces[i].stats, 0, sizeof(IOT_CacheStats_t));
                s_cache_ctx.namespaces[i].stats.current_entries = current;
                s_cache_ctx.namespaces[i].stats.dirty_entries = dirty;
            }
        }
    }
    else
    {
        IOT_CacheNamespace_t *ns = find_namespace(namespace);
        if (!ns)
        {
            IOT_OsalMutexUnlock(s_cache_ctx.mutex);
            return IOT_ERR_NOT_FOUND;
        }
        uint16_t current = ns->stats.current_entries;
        uint16_t dirty = ns->stats.dirty_entries;
        memset(&ns->stats, 0, sizeof(IOT_CacheStats_t));
        ns->stats.current_entries = current;
        ns->stats.dirty_entries = dirty;
    }

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);

    return IOT_OK;
}

void IOT_CachePrintStatus(const char *namespace)
{
    if (!s_cache_ctx.initialized)
    {
        IOT_LOGW(TAG, "Cache not initialized");
        return;
    }

    IOT_OsalMutexLock(s_cache_ctx.mutex, UINT32_MAX);

    IOT_LOGI(TAG, "=== Cache Status ===");

    for (int i = 0; i < IOT_CACHE_MAX_NAMESPACES; i++)
    {
        IOT_CacheNamespace_t *ns = &s_cache_ctx.namespaces[i];

        if (!ns->registered)
        {
            continue;
        }

        if (namespace && strcmp(ns->namespace, namespace) != 0)
        {
            continue;
        }

        IOT_LOGI(TAG, "Namespace: %s", ns->namespace);
        IOT_LOGI(TAG,
                 "  Entries: %d/%d (dirty: %d)",
                 ns->stats.current_entries,
                 ns->config.maxEntries,
                 ns->stats.dirty_entries);
        IOT_LOGI(TAG,
                 "  Hits: %lu, Misses: %lu, Hit rate: %.1f%%",
                 (unsigned long) ns->stats.hits,
                 (unsigned long) ns->stats.misses,
                 ns->stats.hits + ns->stats.misses > 0
                     ? (float) ns->stats.hits / (ns->stats.hits + ns->stats.misses) * 100
                     : 0.0f);
        IOT_LOGI(TAG,
                 "  Writes: %lu, Flushes: %lu, Evictions: %lu",
                 (unsigned long) ns->stats.writes,
                 (unsigned long) ns->stats.flushes,
                 (unsigned long) ns->stats.evictions);
        IOT_LOGI(TAG, "  Policy: %s", ns->config.policy == IOT_CACHE_WRITE_THROUGH ? "write-through" : "write-back");
    }

    IOT_LOGI(TAG, "===================");

    IOT_OsalMutexUnlock(s_cache_ctx.mutex);
}
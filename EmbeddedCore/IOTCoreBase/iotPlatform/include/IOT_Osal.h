#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "IOT_ErrorManager.h"
#ifdef __cplusplus
extern "C"
{
#endif

    // ============================================================================
    // Platform-independent types
    // ============================================================================

    typedef void *IOT_OsalTaskHandle_t;
    typedef void *IOT_OsalQueueHandle_t;
    typedef void *IOT_OsalSemaphoreHandle_t;
    typedef void *IOT_OsalMutexHandle_t;
    typedef void *IOT_OsalTimerHandle_t;

    typedef void (*IOT_OsalTaskFunc_t)(void *param);
    typedef void (*IOT_OsalTimerCallback_t)(IOT_OsalTimerHandle_t timer, void *param);

    // ============================================================================
    // Error codes
    // ============================================================================

#ifndef IOT_OK
    typedef enum
    {
        IOT_OK = 0,
        IOT_ERR_NO_MEM,
        IOT_ERR_INVALID_ARG,
        IOT_ERR_TIMEOUT,
        IOT_ERR_FAIL,
        IOT_ERR_INVALID_STATE
    } iot_err_t;
#endif
    // ============================================================================
    // Configuration
    // ============================================================================

    typedef struct
    {
        const char *name;
        uint32_t stack_size; /**< Stack size in BYTES (not words). */
        uint8_t priority;
        void *param;
    } IOT_OsalTaskConfig_t;

    // ============================================================================
    // Time utils
    // ============================================================================

    /**
     * @brief Get current tick count
     */
    uint32_t IOT_OsalGetTickCount(void);

    /**
     * @brief Convert milliseconds to ticks
     */
    uint32_t IOT_OsalMsToTicks(uint32_t ms);

    /**
     * @brief Delay current task
     * @param ms Delay time in milliseconds
     */
    void IOT_OsalDelay(uint32_t ms);

    /**
     * @brief Delay until specific tick count (for periodic tasks)
     * @param prev_wake_time Pointer to previous wake time, updated by function
     * @param ms Period in milliseconds
     */
    void IOT_OsalDelayUntil(uint32_t *prev_wake_time, uint32_t ms);

    // ============================================================================
    // Task management
    // ============================================================================

    /**
     * @brief Create a new task
     * @param func Task function
     * @param config Task configuration
     * @param handle Output task handle (can be NULL if not needed)
     * @return IOT_OK on success
     */
    iot_err_t
    IOT_OsalTaskCreate(IOT_OsalTaskFunc_t func, const IOT_OsalTaskConfig_t *config, IOT_OsalTaskHandle_t *handle);

    /**
     * @brief Delete a task
     * @param handle Task handle (NULL to delete current task)
     */
    void IOT_OsalTaskDelete(IOT_OsalTaskHandle_t handle);

    /**
     * @brief Suspend a task
     */
    void IOT_OsalTaskSuspend(IOT_OsalTaskHandle_t handle);

    /**
     * @brief Resume a task
     */
    void IOT_OsalTaskResume(IOT_OsalTaskHandle_t handle);

    /**
     * @brief Get current task handle
     */
    IOT_OsalTaskHandle_t IOT_OsalTaskGetCurrent(void);

    /**
     * @brief Yield to other tasks
     */
    void IOT_OsalTaskYield(void);

    // ============================================================================
    // Queue management
    // ============================================================================

    /**
     * @brief Create a queue
     * @param length Maximum number of items
     * @param item_size Size of each item in bytes
     * @return Queue handle or NULL on failure
     */
    IOT_OsalQueueHandle_t IOT_OsalQueueCreate(uint32_t length, uint32_t item_size);

    /**
     * @brief Delete a queue
     */
    void IOT_OsalQueueDelete(IOT_OsalQueueHandle_t queue);

    /**
     * @brief Send item to queue (blocking)
     * @param queue Queue handle
     * @param item Pointer to item to send
     * @param timeout_ms Timeout in milliseconds (0 = no wait, UINT32_MAX = wait forever)
     * @return IOT_OK on success, IOT_ERR_TIMEOUT on timeout
     */
    iot_err_t IOT_OsalQueueSend(IOT_OsalQueueHandle_t queue, const void *item, uint32_t timeout_ms);

    /**
     * @brief Send item to front of queue
     */
    iot_err_t IOT_OsalQueueSendToFront(IOT_OsalQueueHandle_t queue, const void *item, uint32_t timeout_ms);

    /**
     * @brief Receive item from queue (blocking)
     * @param queue Queue handle
     * @param item Pointer to buffer to receive item
     * @param timeout_ms Timeout in milliseconds (0 = no wait, UINT32_MAX = wait forever)
     * @return IOT_OK on success, IOT_ERR_TIMEOUT on timeout
     */
    iot_err_t IOT_OsalQueueReceive(IOT_OsalQueueHandle_t queue, void *item, uint32_t timeout_ms);

    /**
     * @brief Peek item from queue without removing
     */
    iot_err_t IOT_OsalQueuePeek(IOT_OsalQueueHandle_t queue, void *item, uint32_t timeout_ms);

    /**
     * @brief Get number of items in queue
     */
    uint32_t IOT_OsalQueueGetCount(IOT_OsalQueueHandle_t queue);

    /**
     * @brief Reset queue to empty
     */
    void IOT_OsalQueueReset(IOT_OsalQueueHandle_t queue);

    /**
     * @brief Check if queue is full
     */
    bool IOT_OsalQueueIsFull(IOT_OsalQueueHandle_t queue);

    /**
     * @brief Check if queue is empty
     */
    bool IOT_OsalQueueIsEmpty(IOT_OsalQueueHandle_t queue);

    /**
     * @brief Send item to queue from ISR
     * @param queue Queue handle
     * @param item Pointer to item to send
     * @param higher_priority_task_woken Set to true if a higher priority task was woken
     * @return IOT_OK on success
     */
    iot_err_t IOT_OsalQueueSendFromISR(IOT_OsalQueueHandle_t queue, const void *item, bool *higher_priority_task_woken);

    /**
     * @brief Receive item from queue from ISR
     */
    iot_err_t IOT_OsalQueueReceiveFromISR(IOT_OsalQueueHandle_t queue, void *item, bool *higher_priority_task_woken);

    // ============================================================================
    // Semaphore management
    // ============================================================================

    /**
     * @brief Create a binary semaphore
     * @return Semaphore handle or NULL on failure
     */
    IOT_OsalSemaphoreHandle_t IOT_OsalSemaphoreCreateBinary(void);

    /**
     * @brief Create a counting semaphore
     * @param max_count Maximum count value
     * @param initial_count Initial count value
     */
    IOT_OsalSemaphoreHandle_t IOT_OsalSemaphoreCreateCounting(uint32_t max_count, uint32_t initial_count);

    /**
     * @brief Delete a semaphore
     */
    void IOT_OsalSemaphoreDelete(IOT_OsalSemaphoreHandle_t sem);

    /**
     * @brief Take (wait for) semaphore
     * @param sem Semaphore handle
     * @param timeout_ms Timeout in milliseconds
     * @return IOT_OK on success, IOT_ERR_TIMEOUT on timeout
     */
    iot_err_t IOT_OsalSemaphoreTake(IOT_OsalSemaphoreHandle_t sem, uint32_t timeout_ms);

    /**
     * @brief Give (signal) semaphore
     * @return IOT_OK on success
     */
    iot_err_t IOT_OsalSemaphoreGive(IOT_OsalSemaphoreHandle_t sem);

    /**
     * @brief Get semaphore count
     */
    uint32_t IOT_OsalSemaphoreGetCount(IOT_OsalSemaphoreHandle_t sem);

    /**
     * @brief Give semaphore from ISR
     */
    iot_err_t IOT_OsalSemaphoreGiveFromISR(IOT_OsalSemaphoreHandle_t sem, bool *higher_priority_task_woken);

    // ============================================================================
    // Mutex management
    // ============================================================================

    /**
     * @brief Create a mutex
     * @return Mutex handle or NULL on failure
     */
    IOT_OsalMutexHandle_t IOT_OsalMutexCreate(void);

    /**
     * @brief Create a recursive mutex
     */
    IOT_OsalMutexHandle_t IOT_OsalMutexCreateRecursive(void);

    /**
     * @brief Delete a mutex
     */
    void IOT_OsalMutexDelete(IOT_OsalMutexHandle_t mutex);

    /**
     * @brief Lock mutex
     * @param mutex Mutex handle
     * @param timeout_ms Timeout in milliseconds
     * @return IOT_OK on success, IOT_ERR_TIMEOUT on timeout
     */
    iot_err_t IOT_OsalMutexLock(IOT_OsalMutexHandle_t mutex, uint32_t timeout_ms);

    /**
     * @brief Unlock mutex
     */
    iot_err_t IOT_OsalMutexUnlock(IOT_OsalMutexHandle_t mutex);

    /**
     * @brief Lock recursive mutex
     */
    iot_err_t IOT_OsalMutexLockRecursive(IOT_OsalMutexHandle_t mutex, uint32_t timeout_ms);

    /**
     * @brief Unlock recursive mutex
     */
    iot_err_t IOT_OsalMutexUnlockRecursive(IOT_OsalMutexHandle_t mutex);

    // ============================================================================
    // Software Timer management
    // ============================================================================

    /**
     * @brief Create a software timer
     * @param name Timer name
     * @param period_ms Timer period in milliseconds
     * @param auto_reload true for periodic, false for one-shot
     * @param callback Timer callback function
     * @param param User parameter passed to callback
     * @return Timer handle or NULL on failure
     */
    IOT_OsalTimerHandle_t IOT_OsalTimerCreate(
        const char *name, uint32_t period_ms, bool auto_reload, IOT_OsalTimerCallback_t callback, void *param);

    /**
     * @brief Delete a timer
     */
    void IOT_OsalTimerDelete(IOT_OsalTimerHandle_t timer);

    /**
     * @brief Start a timer
     * @param timeout_ms Timeout to wait for command to be accepted
     */
    iot_err_t IOT_OsalTimerStart(IOT_OsalTimerHandle_t timer, uint32_t timeout_ms);

    /**
     * @brief Stop a timer
     */
    iot_err_t IOT_OsalTimerStop(IOT_OsalTimerHandle_t timer, uint32_t timeout_ms);

    /**
     * @brief Reset a timer (restart from current time)
     */
    iot_err_t IOT_OsalTimerReset(IOT_OsalTimerHandle_t timer, uint32_t timeout_ms);

    /**
     * @brief Change timer period
     */
    iot_err_t IOT_OsalTimerChangePeriod(IOT_OsalTimerHandle_t timer, uint32_t new_period_ms, uint32_t timeout_ms);

    /**
     * @brief Check if timer is active
     */
    bool IOT_OsalTimerIsActive(IOT_OsalTimerHandle_t timer);

    // ============================================================================
    // Critical section
    // ============================================================================

    /**
     * @brief Enter critical section (disable interrupts)
     */
    void IOT_OsalEnterCritical(void);

    /**
     * @brief Exit critical section (restore interrupts)
     */
    void IOT_OsalExitCritical(void);

    // ============================================================================
    // Memory management
    // ============================================================================

    /**
     * @brief Get free heap size
     */
    uint32_t IOT_OsalGetFreeHeapSize(void);

    /**
     * @brief Get total heap size
     */
    uint32_t IOT_OsalGetTotalHeapSize(void);

    /**
     * @brief Get minimum ever free heap size
     */
    uint32_t IOT_OsalGetMinFreeHeapSize(void);

    /**
     * @brief Heap occupancy, including the two numbers that distinguish
     *        fragmentation from exhaustion.
     *
     * `totalFreeBytes` can stay flat while `largestFreeBlock` collapses — that is
     * fragmentation, and `largestFreeBlock` is the number a TLS session actually
     * needs. A rising `freeBlocks` alongside a falling `largestFreeBlock` says the
     * heap is being cut into more, smaller holes.
     */
    typedef struct
    {
        uint32_t totalFreeBytes;   /**< sum of all free bytes */
        uint32_t largestFreeBlock; /**< biggest single allocatable block */
        uint32_t minimumFreeBytes; /**< low-water mark of totalFreeBytes since boot */
        uint32_t freeBlocks;       /**< number of free holes in the heap */
        uint32_t allocatedBlocks;  /**< number of live allocations */
    } IOT_OsalHeapInfo_t;

    /**
     * @brief Read current heap occupancy.
     * @return IOT_OK, or IOT_ERR_NOT_SUPPORTED on a platform without the counters
     *         (in which case unavailable fields are left zero).
     */
    iot_err_t IOT_OsalGetHeapInfo(IOT_OsalHeapInfo_t *outInfo);

    /**
     * @brief Get system uptime in microseconds since boot
     */
    int64_t IOT_OsalGetUptimeUs(void);

    /**
     * @brief Write a human-readable task list (name/state/priority/stack/num) into
     *        `buf`. For diagnostics (e.g. the service console `tasks` command).
     *        Requires the platform's task-trace facility; writes an explanatory
     *        message into `buf` if unavailable. `buf` is always NUL-terminated.
     */
    void IOT_OsalGetTaskList(char *buf, size_t bufLen);

    // ============================================================================
    // ISR utils
    // ============================================================================

    /**
     * @brief Yield from ISR if needed (call at end of ISR if higher_priority_task_woken is true)
     */
    void IOT_OsalYieldFromISR(bool higher_priority_task_woken);

#ifdef __cplusplus
}
#endif
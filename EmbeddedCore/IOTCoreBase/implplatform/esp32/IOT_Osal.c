#include "IOT_Osal.h"
#include "IOT_Memory.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "IOT_Log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdlib.h>

static const char *TAG = "OSAL";

// ============================================================================
// Helper macros
// ============================================================================

#define TIMEOUT_TO_TICKS(ms) ((ms) == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(ms))

// ============================================================================
// Timer wrapper structure (to store callback and param)
// ============================================================================

typedef struct
{
    IOT_OsalTimerCallback_t callback;
    void *param;
} IOT_OsalTimerWrapper_t;

static void iot_osal_timer_callback_wrapper(TimerHandle_t xTimer)
{
    IOT_OsalTimerWrapper_t *wrapper = (IOT_OsalTimerWrapper_t *) pvTimerGetTimerID(xTimer);
    if (wrapper && wrapper->callback)
    {
        wrapper->callback((IOT_OsalTimerHandle_t) xTimer, wrapper->param);
    }
}

// ============================================================================
// Time utils
// ============================================================================

uint32_t IOT_OsalGetTickCount(void)
{
    return (uint32_t) xTaskGetTickCount();
}

uint32_t IOT_OsalMsToTicks(uint32_t ms)
{
    return pdMS_TO_TICKS(ms);
}

void IOT_OsalDelay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void IOT_OsalDelayUntil(uint32_t *prev_wake_time, uint32_t ms)
{
    TickType_t ticks = *prev_wake_time;
    vTaskDelayUntil(&ticks, pdMS_TO_TICKS(ms));
    *prev_wake_time = (uint32_t) ticks;
}

// ============================================================================
// Task management
// ============================================================================

iot_err_t IOT_OsalTaskCreate(IOT_OsalTaskFunc_t func, const IOT_OsalTaskConfig_t *config, IOT_OsalTaskHandle_t *handle)
{
    if (func == NULL || config == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    TaskHandle_t task_handle = NULL;
    /* stack_size is BYTES, and ESP-IDF's xTaskCreate takes BYTES — unlike vanilla
     * FreeRTOS, where usStackDepth is a word count. Dividing by sizeof(StackType_t)
     * here handed every task a QUARTER of its requested stack: the mesh provisioning
     * task asked for 2048 and ran on 512, which is what finally showed up as
     * "stack overflow in task mesh_prov" the first time the hub actually joined a
     * mesh (2026-08-27). The Linux OSAL always passed bytes straight through to
     * pthread_attr_setstacksize, so bytes was already the contract. */
    BaseType_t ret = xTaskCreate(func,
                                 config->name ? config->name : "unnamed",
                                 config->stack_size,
                                 config->param,
                                 config->priority,
                                 &task_handle);

    if (ret != pdPASS)
    {
        IOT_LOGE(TAG, "Failed to create task: %s", config->name ? config->name : "unnamed");
        return IOT_ERR_NO_MEM;
    }

    if (handle)
    {
        *handle = (IOT_OsalTaskHandle_t) task_handle;
    }

    return IOT_OK;
}

void IOT_OsalTaskDelete(IOT_OsalTaskHandle_t handle)
{
    vTaskDelete((TaskHandle_t) handle);
}

void IOT_OsalTaskSuspend(IOT_OsalTaskHandle_t handle)
{
    vTaskSuspend((TaskHandle_t) handle);
}

void IOT_OsalTaskResume(IOT_OsalTaskHandle_t handle)
{
    vTaskResume((TaskHandle_t) handle);
}

IOT_OsalTaskHandle_t IOT_OsalTaskGetCurrent(void)
{
    return (IOT_OsalTaskHandle_t) xTaskGetCurrentTaskHandle();
}

void IOT_OsalTaskYield(void)
{
    taskYIELD();
}

// ============================================================================
// Queue management
// ============================================================================

IOT_OsalQueueHandle_t IOT_OsalQueueCreate(uint32_t length, uint32_t item_size)
{
    return (IOT_OsalQueueHandle_t) xQueueCreate(length, item_size);
}

void IOT_OsalQueueDelete(IOT_OsalQueueHandle_t queue)
{
    if (queue)
    {
        vQueueDelete((QueueHandle_t) queue);
    }
}

iot_err_t IOT_OsalQueueSend(IOT_OsalQueueHandle_t queue, const void *item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xQueueSend((QueueHandle_t) queue, item, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalQueueSendToFront(IOT_OsalQueueHandle_t queue, const void *item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xQueueSendToFront((QueueHandle_t) queue, item, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalQueueReceive(IOT_OsalQueueHandle_t queue, void *item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xQueueReceive((QueueHandle_t) queue, item, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalQueuePeek(IOT_OsalQueueHandle_t queue, void *item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xQueuePeek((QueueHandle_t) queue, item, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

uint32_t IOT_OsalQueueGetCount(IOT_OsalQueueHandle_t queue)
{
    if (queue == NULL)
    {
        return 0;
    }
    return (uint32_t) uxQueueMessagesWaiting((QueueHandle_t) queue);
}

void IOT_OsalQueueReset(IOT_OsalQueueHandle_t queue)
{
    if (queue)
    {
        xQueueReset((QueueHandle_t) queue);
    }
}

bool IOT_OsalQueueIsFull(IOT_OsalQueueHandle_t queue)
{
    if (queue == NULL)
    {
        return true;
    }
    return uxQueueSpacesAvailable((QueueHandle_t) queue) == 0;
}

bool IOT_OsalQueueIsEmpty(IOT_OsalQueueHandle_t queue)
{
    if (queue == NULL)
    {
        return true;
    }
    return uxQueueMessagesWaiting((QueueHandle_t) queue) == 0;
}

iot_err_t IOT_OsalQueueSendFromISR(IOT_OsalQueueHandle_t queue, const void *item, bool *higher_priority_task_woken)
{
    if (queue == NULL || item == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t ret = xQueueSendFromISR((QueueHandle_t) queue, item, &xHigherPriorityTaskWoken);

    if (higher_priority_task_woken)
    {
        *higher_priority_task_woken = (xHigherPriorityTaskWoken == pdTRUE);
    }

    return (ret == pdTRUE) ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_OsalQueueReceiveFromISR(IOT_OsalQueueHandle_t queue, void *item, bool *higher_priority_task_woken)
{
    if (queue == NULL || item == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t ret = xQueueReceiveFromISR((QueueHandle_t) queue, item, &xHigherPriorityTaskWoken);

    if (higher_priority_task_woken)
    {
        *higher_priority_task_woken = (xHigherPriorityTaskWoken == pdTRUE);
    }

    return (ret == pdTRUE) ? IOT_OK : IOT_ERR_FAIL;
}

// ============================================================================
// Semaphore management
// ============================================================================

IOT_OsalSemaphoreHandle_t IOT_OsalSemaphoreCreateBinary(void)
{
    return (IOT_OsalSemaphoreHandle_t) xSemaphoreCreateBinary();
}

IOT_OsalSemaphoreHandle_t IOT_OsalSemaphoreCreateCounting(uint32_t max_count, uint32_t initial_count)
{
    return (IOT_OsalSemaphoreHandle_t) xSemaphoreCreateCounting(max_count, initial_count);
}

void IOT_OsalSemaphoreDelete(IOT_OsalSemaphoreHandle_t sem)
{
    if (sem)
    {
        vSemaphoreDelete((SemaphoreHandle_t) sem);
    }
}

iot_err_t IOT_OsalSemaphoreTake(IOT_OsalSemaphoreHandle_t sem, uint32_t timeout_ms)
{
    if (sem == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake((SemaphoreHandle_t) sem, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalSemaphoreGive(IOT_OsalSemaphoreHandle_t sem)
{
    if (sem == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xSemaphoreGive((SemaphoreHandle_t) sem) != pdTRUE)
    {
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

uint32_t IOT_OsalSemaphoreGetCount(IOT_OsalSemaphoreHandle_t sem)
{
    if (sem == NULL)
    {
        return 0;
    }
    return (uint32_t) uxSemaphoreGetCount((SemaphoreHandle_t) sem);
}

iot_err_t IOT_OsalSemaphoreGiveFromISR(IOT_OsalSemaphoreHandle_t sem, bool *higher_priority_task_woken)
{
    if (sem == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t ret = xSemaphoreGiveFromISR((SemaphoreHandle_t) sem, &xHigherPriorityTaskWoken);

    if (higher_priority_task_woken)
    {
        *higher_priority_task_woken = (xHigherPriorityTaskWoken == pdTRUE);
    }

    return (ret == pdTRUE) ? IOT_OK : IOT_ERR_FAIL;
}

// ============================================================================
// Mutex management
// ============================================================================

IOT_OsalMutexHandle_t IOT_OsalMutexCreate(void)
{
    return (IOT_OsalMutexHandle_t) xSemaphoreCreateMutex();
}

IOT_OsalMutexHandle_t IOT_OsalMutexCreateRecursive(void)
{
    return (IOT_OsalMutexHandle_t) xSemaphoreCreateRecursiveMutex();
}

void IOT_OsalMutexDelete(IOT_OsalMutexHandle_t mutex)
{
    if (mutex)
    {
        vSemaphoreDelete((SemaphoreHandle_t) mutex);
    }
}

iot_err_t IOT_OsalMutexLock(IOT_OsalMutexHandle_t mutex, uint32_t timeout_ms)
{
    if (mutex == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake((SemaphoreHandle_t) mutex, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalMutexUnlock(IOT_OsalMutexHandle_t mutex)
{
    if (mutex == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xSemaphoreGive((SemaphoreHandle_t) mutex) != pdTRUE)
    {
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalMutexLockRecursive(IOT_OsalMutexHandle_t mutex, uint32_t timeout_ms)
{
    if (mutex == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xSemaphoreTakeRecursive((SemaphoreHandle_t) mutex, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalMutexUnlockRecursive(IOT_OsalMutexHandle_t mutex)
{
    if (mutex == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xSemaphoreGiveRecursive((SemaphoreHandle_t) mutex) != pdTRUE)
    {
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

// ============================================================================
// Software Timer management
// ============================================================================

IOT_OsalTimerHandle_t IOT_OsalTimerCreate(
    const char *name, uint32_t period_ms, bool auto_reload, IOT_OsalTimerCallback_t callback, void *param)
{
    IOT_OsalTimerWrapper_t *wrapper = Mem_SafeMalloc(sizeof(IOT_OsalTimerWrapper_t), TAG, "timer wrapper");
    if (wrapper == NULL)
    {
        IOT_LOGE(TAG, "Failed to allocate timer wrapper");
        return NULL;
    }

    wrapper->callback = callback;
    wrapper->param = param;

    TimerHandle_t timer = xTimerCreate(name ? name : "timer",
                                       pdMS_TO_TICKS(period_ms),
                                       auto_reload ? pdTRUE : pdFALSE,
                                       wrapper,
                                       iot_osal_timer_callback_wrapper);

    if (timer == NULL)
    {
        SAFE_FREE(wrapper);
        IOT_LOGE(TAG, "Failed to create timer: %s", name ? name : "timer");
        return NULL;
    }

    return (IOT_OsalTimerHandle_t) timer;
}

void IOT_OsalTimerDelete(IOT_OsalTimerHandle_t timer)
{
    if (timer)
    {
        IOT_OsalTimerWrapper_t *wrapper = (IOT_OsalTimerWrapper_t *) pvTimerGetTimerID((TimerHandle_t) timer);
        xTimerDelete((TimerHandle_t) timer, portMAX_DELAY);
        if (wrapper)
        {
            SAFE_FREE(wrapper);
        }
    }
}

iot_err_t IOT_OsalTimerStart(IOT_OsalTimerHandle_t timer, uint32_t timeout_ms)
{
    if (timer == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xTimerStart((TimerHandle_t) timer, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalTimerStop(IOT_OsalTimerHandle_t timer, uint32_t timeout_ms)
{
    if (timer == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xTimerStop((TimerHandle_t) timer, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalTimerReset(IOT_OsalTimerHandle_t timer, uint32_t timeout_ms)
{
    if (timer == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xTimerReset((TimerHandle_t) timer, TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

iot_err_t IOT_OsalTimerChangePeriod(IOT_OsalTimerHandle_t timer, uint32_t new_period_ms, uint32_t timeout_ms)
{
    if (timer == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (xTimerChangePeriod((TimerHandle_t) timer, pdMS_TO_TICKS(new_period_ms), TIMEOUT_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return IOT_ERR_TIMEOUT;
    }

    return IOT_OK;
}

bool IOT_OsalTimerIsActive(IOT_OsalTimerHandle_t timer)
{
    if (timer == NULL)
    {
        return false;
    }
    return xTimerIsTimerActive((TimerHandle_t) timer) != pdFALSE;
}

// ============================================================================
// Critical section
// ============================================================================

static portMUX_TYPE iot_osal_spinlock = portMUX_INITIALIZER_UNLOCKED;

void IOT_OsalEnterCritical(void)
{
    portENTER_CRITICAL(&iot_osal_spinlock);
}

void IOT_OsalExitCritical(void)
{
    portEXIT_CRITICAL(&iot_osal_spinlock);
}

// ============================================================================
// Memory management
// ============================================================================

uint32_t IOT_OsalGetFreeHeapSize(void)
{
    return (uint32_t) xPortGetFreeHeapSize();
}

uint32_t IOT_OsalGetTotalHeapSize(void)
{
    return (uint32_t) heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
}

uint32_t IOT_OsalGetMinFreeHeapSize(void)
{
    return (uint32_t) xPortGetMinimumEverFreeHeapSize();
}

iot_err_t IOT_OsalGetHeapInfo(IOT_OsalHeapInfo_t *outInfo)
{
    if (outInfo == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    /* MALLOC_CAP_8BIT is the pool the TLS session and every payload buffer come
     * from, so it is the one worth reporting — MALLOC_CAP_DEFAULT would fold in
     * regions those allocations can never use. */
    multi_heap_info_t info = {0};
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);

    outInfo->totalFreeBytes = (uint32_t) info.total_free_bytes;
    outInfo->largestFreeBlock = (uint32_t) info.largest_free_block;
    outInfo->minimumFreeBytes = (uint32_t) info.minimum_free_bytes;
    outInfo->freeBlocks = (uint32_t) info.free_blocks;
    outInfo->allocatedBlocks = (uint32_t) info.allocated_blocks;
    return IOT_OK;
}

int64_t IOT_OsalGetUptimeUs(void)
{
    return esp_timer_get_time();
}

void IOT_OsalGetTaskList(char *buf, size_t bufLen)
{
    if (buf == NULL || bufLen == 0)
    {
        return;
    }
    buf[0] = '\0';
#if (configUSE_TRACE_FACILITY == 1) && (configUSE_STATS_FORMATTING_FUNCTIONS == 1)
    /* vTaskList needs ~40 bytes/task; guard against overflow of the caller buffer. */
    size_t needed = (size_t) uxTaskGetNumberOfTasks() * 48 + 48;
    if (bufLen < needed)
    {
        snprintf(buf, bufLen, "task buffer too small (%u tasks, need ~%u bytes)\n",
                 (unsigned) uxTaskGetNumberOfTasks(), (unsigned) needed);
        return;
    }
    const char *header = "Task            State Prio Stack Num\n";
    size_t hlen = strlen(header);
    memcpy(buf, header, hlen + 1);
    vTaskList(buf + hlen);
#else
    snprintf(buf, bufLen, "task list unavailable (enable CONFIG_FREERTOS_USE_TRACE_FACILITY "
                          "+ CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS)\n");
#endif
}

// ============================================================================
// ISR utils
// ============================================================================

void IOT_OsalYieldFromISR(bool higher_priority_task_woken)
{
    if (higher_priority_task_woken)
    {
        portYIELD_FROM_ISR();
    }
}
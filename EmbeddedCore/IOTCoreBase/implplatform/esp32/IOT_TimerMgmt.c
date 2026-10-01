#include "IOT_TimerMgmt.h"
#include "IOT_Memory.h"
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <unistd.h>
#include "esp_sntp.h"
#include "IOT_Log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

static const char *TAG = "IOT_TimerMgmt";
static volatile bool s_time_synced = false;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Min-heap structure for timer management
typedef struct
{
    IOT_TimerEventCall *events;
    size_t size;
    size_t capacity;
} TimerHeap;

static void swap(IOT_TimerEventCall *a, IOT_TimerEventCall *b)
{
    IOT_TimerEventCall temp = *a;
    *a = *b;
    *b = temp;
}

static TimerHeap *heap;
static SemaphoreHandle_t s_timeMutex;
static SemaphoreHandle_t s_heapMutex;
static IOT_TimerHandle_t s_nextTimerId; /* monotonic; assigned under s_heapMutex */

static void heapifyUp(size_t idx)
{
    while (idx > 0)
    {
        size_t parent = (idx - 1) / 2;
        if (heap->events[idx].timeMs >= heap->events[parent].timeMs)
            break;
        swap(&heap->events[idx], &heap->events[parent]);
        idx = parent;
    }
}

static void heapifyDown(size_t idx)
{
    while (true)
    {
        size_t smallest = idx;
        size_t left = 2 * idx + 1;
        size_t right = 2 * idx + 2;

        if (left < heap->size && heap->events[left].timeMs < heap->events[smallest].timeMs)
            smallest = left;

        if (right < heap->size && heap->events[right].timeMs < heap->events[smallest].timeMs)
            smallest = right;

        if (smallest == idx)
            break;

        swap(&heap->events[idx], &heap->events[smallest]);
        idx = smallest;
    }
}

// ============================================================================
// TIMER HEAP API
// ============================================================================

// Create a new timer heap
static TimerHeap *createTimerHeap(size_t initialCapacity)
{
    if (initialCapacity == 0)
        initialCapacity = 16;

    TimerHeap *h = Mem_SafeMalloc(sizeof(TimerHeap), TAG, "timer heap");
    if (!h)
        return NULL;

    h->events = Mem_SafeMalloc(sizeof(IOT_TimerEventCall) * initialCapacity, TAG, "timer events");
    if (!h->events)
    {
        SAFE_FREE(h);
        return NULL;
    }

    h->size = 0;
    h->capacity = initialCapacity;
    return h;
}

// Insert a new timer at any time. Returns a handle (> 0) or IOT_TIMER_HANDLE_INVALID.
IOT_TimerHandle_t IOT_TimerInsertCb(IOT_TimerEventCb cb, void *userData, uint32_t timeMs)
{
    if (!heap || !cb)
        return IOT_TIMER_HANDLE_INVALID;

    uint32_t fireTime = IOT_TimerGetCurrentTimeMs() + timeMs;

    xSemaphoreTake(s_heapMutex, portMAX_DELAY);

    // Resize if needed
    if (heap->size >= heap->capacity)
    {
        size_t newCapacity = heap->capacity * 2;
        IOT_TimerEventCall *newEvents = Mem_SafeRealloc(heap->events, sizeof(IOT_TimerEventCall) * newCapacity, TAG, "timer events grow");
        if (!newEvents)
        {
            xSemaphoreGive(s_heapMutex);
            return IOT_TIMER_HANDLE_INVALID;
        }
        heap->events = newEvents;
        heap->capacity = newCapacity;
    }

    // Allocate a fresh monotonic handle (never 0, which is reserved for INVALID).
    IOT_TimerHandle_t id = ++s_nextTimerId;
    if (id == IOT_TIMER_HANDLE_INVALID)
        id = ++s_nextTimerId;

    // Add to end and bubble up
    heap->events[heap->size].cb = cb;
    heap->events[heap->size].userData = userData;
    heap->events[heap->size].timeMs = fireTime;
    heap->events[heap->size].id = id;

    heapifyUp(heap->size);
    heap->size++;

    xSemaphoreGive(s_heapMutex);

    return id;
}

// Cancel a pending timer by handle. IOT_OK if removed, IOT_ERR_NOT_FOUND otherwise.
iot_err_t IOT_TimerCancel(IOT_TimerHandle_t handle)
{
    if (!heap || handle == IOT_TIMER_HANDLE_INVALID)
        return IOT_ERR_NOT_FOUND;

    iot_err_t result = IOT_ERR_NOT_FOUND;

    xSemaphoreTake(s_heapMutex, portMAX_DELAY);

    for (size_t i = 0; i < heap->size; i++)
    {
        if (heap->events[i].id == handle)
        {
            // Remove element at i: move the last element here, shrink, then restore heap order.
            heap->size--;
            if (i < heap->size)
            {
                heap->events[i] = heap->events[heap->size];
                heapifyDown(i);
                heapifyUp(i);
            }
            result = IOT_OK;
            break; // handles are unique
        }
    }

    xSemaphoreGive(s_heapMutex);

    return result;
}

// Fire the next timer if it's due (callback invoked outside lock to avoid deadlock)
static bool fireTimer(uint32_t currentTimeMs)
{
    xSemaphoreTake(s_heapMutex, portMAX_DELAY);

    if (!heap || heap->size == 0 || heap->events[0].timeMs > currentTimeMs)
    {
        xSemaphoreGive(s_heapMutex);
        return false;
    }

    // Extract the timer
    IOT_TimerEventCall evt = heap->events[0];

    // Move last element to root and heapify down
    heap->size--;
    if (heap->size > 0)
    {
        heap->events[0] = heap->events[heap->size];
        heapifyDown(0);
    }

    xSemaphoreGive(s_heapMutex);

    // Call the callback outside the lock (callback may re-insert timers)
    evt.cb(evt.userData);

    return true;
}

// Get current heap size
size_t IOT_TimerGetCounting(void)
{
    return heap ? heap->size : 0;
}

// Clear all pending timers (thread keeps running safely)
void IOT_TimerDeinit(void)
{
    if (heap && s_heapMutex)
    {
        xSemaphoreTake(s_heapMutex, portMAX_DELAY);
        heap->size = 0;
        xSemaphoreGive(s_heapMutex);
    }
}

// ============================================================================
// TIME UTILITIES
// ============================================================================

static uint64_t startTimeMs = 0;
#define HEAP_TIMER_CAPACITY 15

uint32_t IOT_TimerGetCurrentTimeMs(void)
{ // elapsed time since start
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nowMs = (uint64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    xSemaphoreTake(s_timeMutex, portMAX_DELAY);
    uint32_t result = (uint32_t) (nowMs - startTimeMs);
    xSemaphoreGive(s_timeMutex);
    return result;
}

static void timerThread(void *arg)
{
    while (true)
    {
        uint32_t currentTime = IOT_TimerGetCurrentTimeMs();
        // Fire all timers that are due
        while (fireTimer(currentTime))
        {
            // Keep firing until no more timers are due
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static bool s_timerInited = false;

void IOT_TimerCallbackSupportInit(void)
{
    if (s_timerInited)
    {
        /* Already running — just clear pending timers */
        xSemaphoreTake(s_heapMutex, portMAX_DELAY);
        if (heap)
        {
            heap->size = 0;
        }
        xSemaphoreGive(s_heapMutex);
        return;
    }

    s_timeMutex = xSemaphoreCreateMutex();
    s_heapMutex = xSemaphoreCreateMutex();
    heap = createTimerHeap(HEAP_TIMER_CAPACITY);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    xSemaphoreTake(s_timeMutex, portMAX_DELAY);
    startTimeMs = (uint64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    xSemaphoreGive(s_timeMutex);
    xTaskCreate(timerThread, "timer", 6 * 1024, NULL, 5, NULL);
    s_timerInited = true;
}

void time_sync_callback(struct timeval *tv)
{
    s_time_synced = true;
    IOT_LOGI(TAG, "Time synchronized");
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    IOT_LOGI(TAG, "Current time: %s", asctime(&timeinfo));
}

iot_err_t IOT_TimerInit(void)
{
    if (s_time_synced)
    {
        IOT_LOGI(TAG, "Time already synchronized");
        return IOT_OK;
    }

    /* Tear down any previous SNTP session (e.g. from a WiFi reconnect where
       sync hadn't completed yet).  Safe to call even if not initialized. */
    esp_netif_sntp_deinit();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NULL);

    config.smooth_sync = false;
    config.server_from_dhcp = false;
    config.wait_for_sync = false; /* Don't block — caller may be the event-loop task */
    config.start = true;
    config.sync_cb = time_sync_callback;
    config.renew_servers_after_new_IP = false;

    config.num_of_servers = 2;
    config.servers[0] = "129.6.15.28"; // NIST
    config.servers[1] = "216.239.35.0"; // Google
    s_time_synced = false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
    IOT_LOGI(TAG, "SNTP initialized, request time sync!");

    return IOT_OK;
}

void IOT_TimerDelayMs(uint32_t delayMs)
{
    vTaskDelay(pdMS_TO_TICKS(delayMs));
}

iot_err_t IOT_TimerSetTimeZone(const char *timeZone)
{
    if (timeZone == NULL)
        return IOT_ERR_INVALID_ARG;

    setenv("TZ", timeZone, 1);
    tzset();
    return IOT_OK;
}

iot_err_t IOT_TimerGetUnixTimeMs(uint8_t *outTimeBytes)
{ // 8 bytes for uint64_t in milliseconds
    time_t now;
    time(&now);
    uint64_t nowMs = (uint64_t) now * 1000; // Convert seconds to milliseconds
    if (outTimeBytes)
    {
        for (int i = 0; i < 8; i++)
        {
            outTimeBytes[7 - i] = (nowMs >> (i * 8)) & 0xFF; // big-endian
        }
        return IOT_OK;
    }
    return IOT_ERR_INVALID_ARG;
}

iot_err_t IOT_TimerGetU64UnixTimeMs(uint64_t *outUnixTimeMs)
{
    if (outUnixTimeMs == NULL)
        return IOT_ERR_INVALID_ARG;

    time_t now;
    time(&now);
    *outUnixTimeMs = (uint64_t) now * 1000; // Convert seconds to milliseconds

    return IOT_OK;
}

bool IOT_TimerIsTimeSynced(void)
{
    return s_time_synced;
}

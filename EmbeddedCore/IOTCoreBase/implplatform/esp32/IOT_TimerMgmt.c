#include "IOT_TimerMgmt.h"
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
#include "ntp_plan.h"
#include "timer_heap.h"
#include "task_stacks.h"

/* The server list is chosen by ntp_plan.c (host-tested) and only applied here. It must fit
 * esp_sntp_config_t's fixed servers[] array, sized by CONFIG_LWIP_SNTP_MAX_SERVERS, so that
 * limit is passed to the planner and the plan is built to fit it.
 *
 * This was briefly a compile-time #error demanding each board raise the Kconfig to 4. That
 * was wrong: it broke the build of all eight other targets at once, and would have broken
 * every partner build too, since implplatform ships. A library fits what it is given. */
#ifndef CONFIG_LWIP_SNTP_MAX_SERVERS
#define CONFIG_LWIP_SNTP_MAX_SERVERS 1 /* IDF's own default when the symbol is absent */
#endif

/* Configured servers, in preference order: FASTEST FIRST, names as fallback.
 *
 * Slot 1 is a direct anycast IP on purpose. lwIP tries these in order and only falls
 * through on timeout, so whatever is first decides the normal-case latency -- and an IP
 * needs no DNS round-trip. Historical captures put GOT_IP -> "Time synchronized" at 1-4 s
 * with IP literals, and the clock is on the critical path: MQTT waits only
 * MQTT_TIME_SYNC_MAX_WAIT_MS (15 s) before connecting with a 1970 clock and failing cert
 * validation. Speed here is correctness, not comfort.
 *
 * The DNS names follow as the robustness half, which the original two-hard-coded-IPs
 * version had none of: if that address ever goes stale, a pool name still resolves and the
 * device still syncs -- slower, but it syncs. Overridable per board via Kconfig. */
#ifndef CONFIG_IOT_NTP_SERVER_1
#define CONFIG_IOT_NTP_SERVER_1 "216.239.35.0"
#endif
#ifndef CONFIG_IOT_NTP_SERVER_2
#define CONFIG_IOT_NTP_SERVER_2 "vn.pool.ntp.org"
#endif
#ifndef CONFIG_IOT_NTP_SERVER_3
#define CONFIG_IOT_NTP_SERVER_3 "pool.ntp.org"
#endif

/* Whether to ask the DHCP-supplied server (option 42) FIRST.
 *
 * OFF by default, and that default is measured, not taste. lwIP installs the DHCP server at
 * slot 0 and tries slot 0 first, so on a network that does NOT send option 42 -- which is
 * the common case, where the client asks and the router returns
 * nothing -- it burns SNTP retry timeouts on an EMPTY slot before reaching our names at
 * slot 1. Measured on hardware 2026-10-09: GOT_IP -> "Time synchronized" went from 1-4 s
 * (nine historical captures) to 34 s with DHCP-first enabled.
 *
 * That is not a cosmetic delay. MQTT waits only MQTT_TIME_SYNC_MAX_WAIT_MS (15 s) for the
 * clock and then connects anyway, so a 34 s sync GUARANTEES the first handshake runs with a
 * 1970 clock, where the CA is not yet valid and cannot serve as a trust anchor -- reported
 * as `TLS cert verify flags: 0x8 NOT_TRUSTED`. Enable this only on a site that actually
 * serves NTP by DHCP and blocks outbound UDP/123. */
#if defined(CONFIG_LWIP_DHCP_GET_NTP_SRV) && defined(CONFIG_IOT_NTP_USE_DHCP_SERVER)
#define IOT_NTP_DHCP_SUPPORTED true
#else
#define IOT_NTP_DHCP_SUPPORTED false
#endif

static const char *TAG = "IOT_TimerMgmt";
static volatile bool s_time_synced = false;

// ============================================================================
// TIMER HEAP
// ============================================================================

/* The heap itself is timer_heap.c (pure C, host-tested). This file owns the lock, the
 * handle counter and the task that fires due entries. */
static TimerHeap_t s_heap;
static bool s_heapReady = false;
static SemaphoreHandle_t s_heapMutex;
static IOT_TimerHandle_t s_nextTimerId; /* monotonic; assigned under s_heapMutex */

#define HEAP_TIMER_CAPACITY 15

/* Milliseconds of uptime, 64-bit: the heap's timebase. Lock-free and valid before any
 * init, since esp_timer runs from boot. */
static uint64_t uptimeMs(void)
{
    return (uint64_t) (esp_timer_get_time() / 1000);
}

// Insert a new timer at any time. Returns a handle (> 0) or IOT_TIMER_HANDLE_INVALID.
IOT_TimerHandle_t IOT_TimerInsertCb(IOT_TimerEventCb cb, void *userData, uint32_t timeMs)
{
    /* First, before any lock: called ahead of IOT_TimerCallbackSupportInit() this must
     * fail cleanly rather than take a mutex that does not exist yet. */
    if (!s_heapReady || !cb)
        return IOT_TIMER_HANDLE_INVALID;

    TimerHeapEntry_t entry = {.cb = cb, .userData = userData, .dueMs = uptimeMs() + timeMs};

    xSemaphoreTake(s_heapMutex, portMAX_DELAY);

    // Allocate a fresh monotonic handle (never 0, which is reserved for INVALID).
    IOT_TimerHandle_t id = ++s_nextTimerId;
    if (id == IOT_TIMER_HANDLE_INVALID)
        id = ++s_nextTimerId;
    entry.id = id;
    bool pushed = TimerHeap_Push(&s_heap, &entry);

    xSemaphoreGive(s_heapMutex);

    return pushed ? id : IOT_TIMER_HANDLE_INVALID;
}

// Cancel a pending timer by handle. IOT_OK if removed, IOT_ERR_NOT_FOUND otherwise.
iot_err_t IOT_TimerCancel(IOT_TimerHandle_t handle)
{
    if (!s_heapReady || handle == IOT_TIMER_HANDLE_INVALID)
        return IOT_ERR_NOT_FOUND;

    xSemaphoreTake(s_heapMutex, portMAX_DELAY);
    bool removed = TimerHeap_Remove(&s_heap, handle);
    xSemaphoreGive(s_heapMutex);

    return removed ? IOT_OK : IOT_ERR_NOT_FOUND;
}

// Fire the next timer if it's due (callback invoked outside lock to avoid deadlock)
static bool fireTimer(uint64_t nowMs)
{
    TimerHeapEntry_t evt;

    xSemaphoreTake(s_heapMutex, portMAX_DELAY);
    bool due = TimerHeap_PopDue(&s_heap, nowMs, &evt);
    xSemaphoreGive(s_heapMutex);

    if (!due)
        return false;

    // Call the callback outside the lock (callback may re-insert timers)
    evt.cb(evt.userData);

    return true;
}

// Get current heap size
size_t IOT_TimerGetCounting(void)
{
    return s_heapReady ? s_heap.size : 0;
}

// Clear all pending timers (thread keeps running safely)
void IOT_TimerDeinit(void)
{
    if (s_heapReady)
    {
        xSemaphoreTake(s_heapMutex, portMAX_DELAY);
        TimerHeap_Clear(&s_heap);
        xSemaphoreGive(s_heapMutex);
    }
}

// ============================================================================
// TIME UTILITIES
// ============================================================================

uint32_t IOT_TimerGetCurrentTimeMs(void)
{
    /* Uptime in ms, truncated to 32 bits: wraps every ~49.7 days, so it is for durations
     * only (`end - start` survives one wrap). The timer heap and Record use the 64-bit
     * clock.
     *
     * No lock and no origin. This used to subtract a startTimeMs stamped at init, under a
     * mutex IOT_CoreInit() creates -- so a caller earlier than that aborted on a NULL
     * semaphore, and one that read before the stamp saw the clock step backward. */
    return (uint32_t) uptimeMs();
}

static void timerThread(void *arg)
{
    while (true)
    {
        uint64_t nowMs = uptimeMs();
        // Fire all timers that are due
        while (fireTimer(nowMs))
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
        IOT_TimerDeinit();
        return;
    }

    s_heapMutex = xSemaphoreCreateMutex();
    /* Ready only with both: a heap without its mutex would hand every caller a NULL
     * semaphore, the crash this file has already had once. */
    s_heapReady = (s_heapMutex != NULL) && TimerHeap_Init(&s_heap, HEAP_TIMER_CAPACITY);
    xTaskCreate(timerThread, "timer", IOT_STACK_TIMER, NULL, 5, NULL);
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
    config.wait_for_sync = false; /* Don't block — caller may be the event-loop task */
    config.start = true;
    config.sync_cb = time_sync_callback;

    /* Ask the DHCP-supplied server first where there is one, then our configured names,
     * then a literal. This used to be two hard-coded IPs with server_from_dhcp and
     * renew_servers_after_new_IP both false, which meant a network that blocks outbound
     * UDP/123 but serves NTP locally could never sync -- and a device that cannot sync
     * cannot validate a certificate, so it never reaches the cloud at all. */
    static const char *const configuredServers[] = {
        CONFIG_IOT_NTP_SERVER_1,
        CONFIG_IOT_NTP_SERVER_2,
        CONFIG_IOT_NTP_SERVER_3,
    };
    IOT_NtpPlan_t plan;
    IOT_NtpBuildPlan(&plan, IOT_NTP_DHCP_SUPPORTED, configuredServers,
                     sizeof(configuredServers) / sizeof(configuredServers[0]),
                     (uint8_t) CONFIG_LWIP_SNTP_MAX_SERVERS);

    config.server_from_dhcp = plan.serverFromDhcp;
    config.renew_servers_after_new_IP = plan.serverFromDhcp; /* re-read on a network change */
    config.index_of_first_server = plan.indexOfFirstServer;
    config.num_of_servers = plan.numOfServers;

    /* Copy DENSELY from index 0. esp_netif_sntp applies index_of_first_server itself when
     * it installs the names (sntp_setservername(i + index_of_first_server, servers[i])),
     * and it strdup()s every entry in 0..num_of_servers -- so starting this loop at
     * index_of_first_server leaves servers[0] NULL and crashes in ROM on strdup(NULL).
     * That is exactly what 2.0.3.0 did, immediately after GOT_IP. */
    for (uint8_t i = 0; i < plan.numOfServers; i++)
    {
        config.servers[i] = plan.servers[i];
        IOT_LOGI(TAG, "NTP server[%u] (lwip slot %u) = %s", (unsigned) i,
                 (unsigned) (i + plan.indexOfFirstServer), plan.servers[i]);
    }

    s_time_synced = false;

    /* NOT ESP_ERROR_CHECK: this used to abort the chip when SNTP init failed. Rebooting
     * over a failed time sync is strictly worse than running without one -- the MQTT layer
     * already waits and then connects anyway (MQTT_TIME_SYNC_MAX_WAIT_MS). */
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "SNTP init failed (%d) — clock will not sync; the cloud "
                      "connection depends on it", (int) err);
        return IOT_ERR_FAIL;
    }

    IOT_LOGI(TAG, "SNTP initialized (%u server(s), dhcp=%d), request time sync!",
             (unsigned) plan.numOfServers, (int) plan.serverFromDhcp);

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

iot_err_t IOT_TimerGetU64UnixTimeMs(uint64_t *outUnixTimeMs)
{
    if (outUnixTimeMs == NULL)
        return IOT_ERR_INVALID_ARG;

    /* gettimeofday, not time(): time() * 1000 made every "millisecond" end in 000, so a
     * Record checkpoint corrected against it landed up to 999 ms early, and the device
     * time sent to the app was whole seconds. The Linux implementation was already
     * millisecond-exact. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *outUnixTimeMs = (uint64_t) tv.tv_sec * 1000ULL + (uint64_t) tv.tv_usec / 1000ULL;

    return IOT_OK;
}

iot_err_t IOT_TimerGetUnixTimeMs(uint8_t *outTimeBytes)
{ // 8 bytes for uint64_t in milliseconds, big-endian
    if (outTimeBytes == NULL)
        return IOT_ERR_INVALID_ARG;

    uint64_t nowMs = 0;
    iot_err_t err = IOT_TimerGetU64UnixTimeMs(&nowMs);
    if (err != IOT_OK)
        return err;
    for (int i = 0; i < 8; i++)
    {
        outTimeBytes[7 - i] = (nowMs >> (i * 8)) & 0xFF;
    }
    return IOT_OK;
}

bool IOT_TimerIsTimeSynced(void)
{
    return s_time_synced;
}

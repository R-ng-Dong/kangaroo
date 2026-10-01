#include "dao/IOT_DaoMgmt.h"
#include "IOT_Log.h"
#include "IOT_Esp32Drivers.h"
#include "network/IOT_NetworkMgmt.h"
#include "IOT_Platform.h"
#include "ble/IOT_BleMgmt.h"
#if CONFIG_IOT_LOG_TCP_ENABLED
#include "log/IOT_LogTcp.h"
#endif

/* Platform bring-up dominates heap use on a C3 — measured at ~138 KB of ~221 KB total.
 * These probes split that between drivers, NVS, WiFi and BLE so the cost is attributed
 * rather than guessed. Gated by the same dev-only flag as the report-path probe
 * (default n), so shipped builds are unaffected.
 *
 * Uses the ESP-IDF heap API directly rather than iotcore's IOT_HeapProbe: the
 * iotplatform component does not carry iotcore's include path, and adding a
 * cross-component dependency for a diagnostic is not worth it. */
#ifdef CONFIG_IOT_HEAP_PROBE_ENABLED
#include "esp_heap_caps.h"
static uint32_t s_platFree;
static uint32_t s_platLargest;
static void platHeapMark(void)
{
    s_platFree = (uint32_t) heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    s_platLargest = (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
}
/* Log the change since the previous mark, then re-mark, so consecutive calls read as
 * per-phase deltas rather than cumulative ones. */
static void platHeapLog(const char *label)
{
    uint32_t f = (uint32_t) heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    uint32_t l = (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    IOT_LOGW("heapprobe", "%s | free %u->%u (%d) largest %u->%u (%d)", label,
             (unsigned) s_platFree, (unsigned) f, (int) (f - s_platFree),
             (unsigned) s_platLargest, (unsigned) l, (int) (l - s_platLargest));
    s_platFree = f;
    s_platLargest = l;
}
#else
static inline void platHeapMark(void)
{
}
static inline void platHeapLog(const char *label)
{
    (void) label;
}
#endif

#if !defined(CONFIG_BT_ENABLED) && !defined(CONFIG_IOT_TCP_SERVER_ENABLED)
#error "No provisioning channel: enable Bluetooth (menuconfig) or CONFIG_IOT_TCP_SERVER_ENABLED"
#endif

/* Everything except BLE. Split out so the lean OTA boot can bring up storage and
 * the network without paying for the BLE stack — the download then has far more
 * contiguous heap than the ~16.7 KB TLS needs, which is what the old in-place
 * path was releasing BLE mid-flight to obtain. */
static iot_err_t initPlatformCore(void)
{
    log_init();
    platHeapMark();
    IOT_Esp32DriversInit();
    platHeapLog("plat/drivers");

    iot_err_t err = IOT_DaoInit();
    if (err != IOT_OK)
    {
        IOT_LOGE("platform", "Failed to init dao manager");
        return err;
    }
    platHeapLog("plat/DaoInit (nvs)");

    IOT_NetworkIface ifaces[] = {{.type = WIFI, .number = 0}, {.type = NETWORK_TYPE_NONE, .number = 0}};
    err = IOT_NwkInit(ifaces);
    if (err != IOT_OK)
    {
        IOT_LOGE("platform", "Failed to init network manager");
        return err;
    }
    platHeapLog("plat/NwkInit (wifi)");
#if CONFIG_IOT_LOG_TCP_ENABLED
    IOT_LogTcpInit(); /* register IP_EVENT handler; server starts on GOT_IP */
#endif
    return IOT_OK;
}

iot_err_t IOT_InitPlatform(void)
{
    iot_err_t err = initPlatformCore();
    if (err != IOT_OK)
    {
        return err;
    }

    err = IOT_BleInit();
    if (err != IOT_OK)
    {
        IOT_LOGE("platform", "Failed to init ble manager");
        return err;
    }
    platHeapLog("plat/BleInit");
    IOT_LOGI("Platform", "Platform initialized successfully");
    return IOT_OK;
}

iot_err_t IOT_InitPlatformLean(void)
{
    iot_err_t err = initPlatformCore();
    if (err != IOT_OK)
    {
        return err;
    }
    IOT_LOGI("Platform", "Platform initialized (lean: no BLE)");
    return IOT_OK;
}

iot_err_t IOT_DeinitPlatform(void)
{
    IOT_BleDeinit();
    IOT_NetworkIface ifaces[] = {{.type = WIFI, .number = 0}, {.type = NETWORK_TYPE_NONE, .number = 0}};
    IOT_NwkDeinit(ifaces);
    IOT_DaoDeinit();
    log_cleanup();
    return IOT_OK;
}
/**
 * @file IOT_MdnsMgmt.c
 * @brief ESP32 mDNS implementation
 *
 * Implements the IOT_MdnsMgmt.h abstract interface using ESP-IDF's mDNS component.
 * Used for local network service advertisement so the mobile app can discover
 * devices on the same LAN without cloud involvement.
 */

#include "IOT_MdnsMgmt.h"
#include "IOT_MdnsDriver.h"
#include "IOT_Log.h"

#include "esp_err.h"
#include "mdns.h"

#include <string.h>

static const char *TAG = "IOT_MDNS";

static bool s_initialized = false;

// ============================================================================
// Service Lifecycle
// ============================================================================

static iot_err_t esp32MdnsStartService(const char *hostname, const char *domain)
{
    if (s_initialized)
    {
        IOT_LOGW(TAG, "mDNS already initialized");
        return IOT_OK;
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "mdns_init failed: %d", err);
        return IOT_ERR_FAIL;
    }

    if (hostname != NULL)
    {
        err = mdns_hostname_set(hostname);
        if (err != ESP_OK)
        {
            IOT_LOGE(TAG, "mdns_hostname_set failed: %d", err);
            mdns_free();
            return IOT_ERR_FAIL;
        }
        IOT_LOGI(TAG, "mDNS hostname: %s", hostname);
    }

    s_initialized = true;
    return IOT_OK;
}

static iot_err_t esp32MdnsStopService(void)
{
    if (!s_initialized)
    {
        return IOT_OK;
    }

    mdns_free();
    s_initialized = false;
    IOT_LOGI(TAG, "mDNS stopped");
    return IOT_OK;
}

// ============================================================================
// Service Advertisement
// ============================================================================

static iot_err_t esp32MdnsAddAdvertisement(char *serviceName, char *serviceType, uint16_t port)
{
    if (!s_initialized)
    {
        IOT_LOGE(TAG, "mDNS not initialized");
        return IOT_ERR_FAIL;
    }

    esp_err_t err = mdns_service_add(serviceName, serviceType, "_tcp", port, NULL, 0);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "mdns_service_add failed: %d", err);
        return IOT_ERR_FAIL;
    }

    IOT_LOGI(TAG, "Advertised: %s %s port %d", serviceName ? serviceName : "(default)", serviceType, port);
    return IOT_OK;
}

static iot_err_t esp32MdnsRemoveAdvertisement(const char *serviceName)
{
    if (!s_initialized)
    {
        return IOT_OK;
    }

    /* ESP-IDF removes by service type, not name — remove all matching TCP services */
    esp_err_t err = mdns_service_remove_all();
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "mdns_service_remove failed: %d", err);
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

static iot_err_t esp32MdnsRemoveAllAdvertisements(void)
{
    if (!s_initialized)
    {
        return IOT_OK;
    }

    mdns_service_remove_all();
    IOT_LOGI(TAG, "All advertisements removed");
    return IOT_OK;
}

// ============================================================================
// Service Discovery (not used in current flow — stub implementation)
// ============================================================================

static iot_err_t esp32MdnsStartDiscovery(char *serviceType)
{
    IOT_LOGW(TAG, "mDNS discovery not implemented");
    return IOT_ERR_NOT_SUPPORTED;
}

static iot_err_t esp32MdnsStopDiscovery(void)
{
    return IOT_ERR_NOT_SUPPORTED;
}

// ============================================================================
// Event Callbacks (not used in current flow — stub implementation)
// ============================================================================

static iot_err_t esp32MdnsRegisterEventCallback(mdns_event_status_type_t eventType, mdns_event_callback_t cb, void *arg)
{
    return IOT_ERR_NOT_SUPPORTED;
}

static iot_err_t esp32MdnsUnregisterEventCallback(mdns_event_status_type_t eventType)
{
    return IOT_ERR_NOT_SUPPORTED;
}

/* ── driver registration ─────────────────────────────────────────────────── */

static const IOT_MdnsDriver_t s_esp32Driver = {
    .startService            = esp32MdnsStartService,
    .stopService             = esp32MdnsStopService,
    .addAdvertisement        = esp32MdnsAddAdvertisement,
    .removeAdvertisement     = esp32MdnsRemoveAdvertisement,
    .removeAllAdvertisements = esp32MdnsRemoveAllAdvertisements,
    .startDiscovery          = esp32MdnsStartDiscovery,
    .stopDiscovery           = esp32MdnsStopDiscovery,
    .registerEventCallback   = esp32MdnsRegisterEventCallback,
    .unregisterEventCallback = esp32MdnsUnregisterEventCallback,
};

void IOT_Esp32MdnsMgmtRegister(void)
{
    IOT_MdnsRegisterDriver(&s_esp32Driver);
}

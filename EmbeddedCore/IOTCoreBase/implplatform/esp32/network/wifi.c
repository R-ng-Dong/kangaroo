#include "network/wifi.h"

// wifi Interface implementation for ESP32

#include "IOT_ErrorManager.h"
#include "IOT_Common.h"
#include "esp_event.h"
#include "IOT_Log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include "stdio.h"

typedef enum
{
    IOT_WPA3_SAE_PWE_HUNT_AND_PECK,
    IOT_WPA3_SAE_PWE_UNSPECIFIED
} iot_wifi_sae_pwe_method_t;

typedef struct
{
    IOT_WifiEventCb cb;
    bool isRegistered;
} wifi_cb_mgmt_t;

static wifi_cb_mgmt_t *wifiCallbackMgmt = NULL;

static iot_wifi_auth_mode_t default_auth_mode = IOT_WIFI_AUTH_OPEN;
static iot_wifi_sae_pwe_method_t default_sae_pwe_method = IOT_WPA3_SAE_PWE_HUNT_AND_PECK;
static volatile wifi_event_status_t currentWifiStatus = WIFI_EVENT_STATUS_DISCONNECTED;
static volatile bool wifiAutoReconnect = true;

static void iotInternalEventCallHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            currentWifiStatus = WIFI_EVENT_STATUS_DISCONNECTED;
            if (wifiCallbackMgmt[WIFI_EVENT_STATUS_DISCONNECTED].isRegistered &&
                wifiCallbackMgmt[WIFI_EVENT_STATUS_DISCONNECTED].cb != NULL)
            {
                wifi_event_sta_disconnected_t callbackData = *(wifi_event_sta_disconnected_t *) event_data;
                wifi_mgmt_event_disconnected_data_t disconnectedData = {0};
                memcpy(disconnectedData.ssid, callbackData.ssid, sizeof(callbackData.ssid));
                disconnectedData.ssid_len = callbackData.ssid_len;
                memcpy(disconnectedData.bssid, callbackData.bssid, sizeof(callbackData.bssid));
                disconnectedData.rssi = callbackData.rssi;
                disconnectedData.reason = callbackData.reason;
                wifiCallbackMgmt[WIFI_EVENT_STATUS_DISCONNECTED].cb(
                    NULL, WIFI_EVENT_STATUS_DISCONNECTED, &disconnectedData);
            }
            if (wifiAutoReconnect)
            {
                IOT_LOGI("iot_wifi", "Auto-reconnecting to WiFi...");
                esp_wifi_connect();
            }
            break;
        }
        case WIFI_EVENT_STA_START:
        {
            break;
        }
        case WIFI_EVENT_STA_CONNECTED:
        {
            currentWifiStatus = WIFI_EVENT_STATUS_CONNECTED_NO_IP;
            wifi_event_sta_connected_t *connectedData = (wifi_event_sta_connected_t *) event_data;
            IOT_LOGI("iot_wifi", "Connected to SSID: %.*s", connectedData->ssid_len, connectedData->ssid);
            wifi_mgmt_event_connected_data_t mgmtConnectedData = {0};
            memcpy(mgmtConnectedData.ssid, connectedData->ssid, sizeof(connectedData->ssid));
            mgmtConnectedData.ssid_len = connectedData->ssid_len;
            memcpy(mgmtConnectedData.bssid, connectedData->bssid, sizeof(connectedData->bssid));
            mgmtConnectedData.channel = connectedData->channel;
            mgmtConnectedData.authmode = (iot_wifi_auth_mode_t) connectedData->authmode;
            if (wifiCallbackMgmt[WIFI_EVENT_STATUS_CONNECTED].isRegistered &&
                wifiCallbackMgmt[WIFI_EVENT_STATUS_CONNECTED].cb != NULL)
            {
                wifiCallbackMgmt[WIFI_EVENT_STATUS_CONNECTED].cb(arg, WIFI_EVENT_STATUS_CONNECTED, &mgmtConnectedData);
            }
            break;
        }
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            currentWifiStatus = WIFI_EVENT_STATUS_GOT_IP;
            if (wifiCallbackMgmt[WIFI_EVENT_STATUS_GOT_IP].isRegistered &&
                wifiCallbackMgmt[WIFI_EVENT_STATUS_GOT_IP].cb != NULL)
            {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
                iot_wifi_ip_event_got_ip_data_t gotIpData = {0};
                memcpy(&gotIpData, &event->ip_info, sizeof(esp_netif_ip_info_t));
                wifiCallbackMgmt[WIFI_EVENT_STATUS_GOT_IP].cb(arg, WIFI_EVENT_STATUS_GOT_IP, &gotIpData);
            }
            break;
        }
        default:
            break;
        }
    }
}

iot_err_t IOT_WifiRegisterEventCb(uint8_t iface_num, wifi_event_status_t eventType, IOT_WifiEventCb callback, void *arg)
{
    if (eventType > WIFI_EVENT_STATUS_ALL)
    {
        IOT_LOGE("iot_wifi", "Invalid event type: %d", eventType);
        return IOT_ERR_INVALID_ARG;
    }
    if (iface_num != 0)
    {
        IOT_LOGE("iot_wifi", "Invalid interface number: %d", iface_num);
        return IOT_ERR_INVALID_ARG;
    }
    switch (eventType)
    {
    case WIFI_EVENT_STATUS_DISCONNECTED:
    case WIFI_EVENT_STATUS_CONNECTING:
    case WIFI_EVENT_STATUS_CONNECTED_NO_IP:
    case WIFI_EVENT_STATUS_GOT_IP:
    case WIFI_EVENT_STATUS_CONNECTED:
    case WIFI_EVENT_STATUS_WRONG_PASSWORD:
    case WIFI_EVENT_STATUS_NO_AP_FOUND:
    case WIFI_EVENT_STATUS_AUTH_FAIL:
    case WIFI_EVENT_STATUS_UNKNOWN_ERROR:
    {
        wifiCallbackMgmt[eventType].cb = callback;
        wifiCallbackMgmt[eventType].isRegistered = true;
        break;
    }
    case WIFI_EVENT_STATUS_ALL:
    {
        for (int i = 0; i < WIFI_EVENT_STATUS_MAX; i++)
        {
            wifiCallbackMgmt[i].cb = callback;
            wifiCallbackMgmt[i].isRegistered = true;
        }
        break;
    }
    default:
        IOT_LOGE("iot_wifi", "Unsupported event type: %d", eventType);
        return IOT_ERR_INVALID_ARG;
    }
    return IOT_OK;
}

iot_err_t IOT_WifiUnregisterEventCb(uint8_t iface_num, wifi_event_status_t eventType)
{
    if (eventType >= WIFI_EVENT_STATUS_MAX)
    {
        IOT_LOGE("iot_wifi", "Invalid event type: %d", eventType);
        return IOT_ERR_INVALID_ARG;
    }
    if (iface_num != 0)
    {
        IOT_LOGE("iot_wifi", "Invalid interface number: %d", iface_num);
        return IOT_ERR_INVALID_ARG;
    }
    switch (eventType)
    {
    case WIFI_EVENT_STATUS_DISCONNECTED:
    case WIFI_EVENT_STATUS_CONNECTING:
    case WIFI_EVENT_STATUS_CONNECTED_NO_IP:
    case WIFI_EVENT_STATUS_GOT_IP:
    case WIFI_EVENT_STATUS_CONNECTED:
    case WIFI_EVENT_STATUS_WRONG_PASSWORD:
    case WIFI_EVENT_STATUS_NO_AP_FOUND:
    case WIFI_EVENT_STATUS_AUTH_FAIL:
    case WIFI_EVENT_STATUS_UNKNOWN_ERROR:
    {
        wifiCallbackMgmt[eventType].cb = NULL;
        wifiCallbackMgmt[eventType].isRegistered = false;
        break;
    }
    case WIFI_EVENT_STATUS_ALL:
    {
        for (int i = 0; i < WIFI_EVENT_STATUS_MAX; i++)
        {
            wifiCallbackMgmt[i].cb = NULL;
            wifiCallbackMgmt[i].isRegistered = false;
        }
        break;
    }
    default:
        IOT_LOGE("iot_wifi", "Unsupported event type: %d", eventType);
        return IOT_ERR_INVALID_ARG;
    }
    return IOT_OK;
}

iot_err_t IOT_WifiInit(uint8_t iface_num)
{
ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t wifi_config = {
        .sta =
            {
                .threshold.authmode = default_auth_mode,
                .sae_pwe_h2e = default_sae_pwe_method,
                .sae_h2e_identifier = "",
            },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Allocate callback management BEFORE registering event handlers to avoid race condition
    wifiCallbackMgmt =
        (wifi_cb_mgmt_t *) Mem_SafeCalloc(WIFI_EVENT_STATUS_MAX, sizeof(wifi_cb_mgmt_t), "iot_wifi", "wifi callbacks");
    if (wifiCallbackMgmt == NULL)
    {
        IOT_LOGE("iot_wifi", "Failed to allocate memory for wifi callbacks");
        return IOT_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &iotInternalEventCallHandler, NULL, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &iotInternalEventCallHandler, NULL, NULL));

    return IOT_OK;
}
iot_err_t IOT_WifiDeinit(uint8_t iface_num)
{
    if (iface_num != 0)
    {
        IOT_LOGE("iot_wifi", "Invalid interface number: %d", iface_num);
        return IOT_ERR_INVALID_ARG;
    }

    // Unregister event handlers first
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, NULL);
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, NULL);

    // Stop and deinit wifi
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    // Free callback management last
    SAFE_FREE(wifiCallbackMgmt);

    return IOT_OK;
}

static iot_err_t convertAuthMode(iot_wifi_auth_mode_t *iot_auth_mode, wifi_auth_mode_t *esp_auth_mode, bool reverse)
{
    if (reverse)
    {
        switch (*esp_auth_mode)
        {
        case WIFI_AUTH_OPEN:
            *iot_auth_mode = IOT_WIFI_AUTH_OPEN;
            break;
        case WIFI_AUTH_WEP:
            *iot_auth_mode = IOT_WIFI_AUTH_WEP;
            break;
        case WIFI_AUTH_WPA_PSK:
            *iot_auth_mode = IOT_WIFI_AUTH_WPA_PSK;
            break;
        case WIFI_AUTH_WPA2_PSK:
            *iot_auth_mode = IOT_WIFI_AUTH_WPA2_PSK;
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            *iot_auth_mode = IOT_WIFI_AUTH_WPA_WPA2_PSK;
            break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            *iot_auth_mode = IOT_WIFI_AUTH_WPA2_ENTERPRISE;
            break;
        case WIFI_AUTH_WPA3_PSK:
            *iot_auth_mode = IOT_WIFI_AUTH_WPA3_PSK;
            break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            *iot_auth_mode = IOT_WIFI_AUTH_WPA2_WPA3_PSK;
            break;
        case WIFI_AUTH_WAPI_PSK:
            *iot_auth_mode = IOT_WIFI_AUTH_WAPI_PSK;
            break;
        default:
            return IOT_ERR_INVALID_ARG;
        }
    }
    else
    {
        switch (*iot_auth_mode)
        {
        case IOT_WIFI_AUTH_OPEN:
            *esp_auth_mode = WIFI_AUTH_OPEN;
            break;
        case IOT_WIFI_AUTH_WEP:
            *esp_auth_mode = WIFI_AUTH_WEP;
            break;
        case IOT_WIFI_AUTH_WPA_PSK:
            *esp_auth_mode = WIFI_AUTH_WPA_PSK;
            break;
        case IOT_WIFI_AUTH_WPA2_PSK:
            *esp_auth_mode = WIFI_AUTH_WPA2_PSK;
            break;
        case IOT_WIFI_AUTH_WPA_WPA2_PSK:
            *esp_auth_mode = WIFI_AUTH_WPA_WPA2_PSK;
            break;
        case IOT_WIFI_AUTH_WPA2_ENTERPRISE:
            *esp_auth_mode = WIFI_AUTH_WPA2_ENTERPRISE;
            break;
        case IOT_WIFI_AUTH_WPA3_PSK:
            *esp_auth_mode = WIFI_AUTH_WPA3_PSK;
            break;
        case IOT_WIFI_AUTH_WPA2_WPA3_PSK:
            *esp_auth_mode = WIFI_AUTH_WPA2_WPA3_PSK;
            break;
        case IOT_WIFI_AUTH_WAPI_PSK:
            *esp_auth_mode = WIFI_AUTH_WAPI_PSK;
            break;
        default:
            return IOT_ERR_INVALID_ARG;
        }
    }
    return IOT_OK;
}

iot_err_t IOT_WifiGetMac(uint8_t *mac)
{
    return esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK ? IOT_OK : IOT_ERR_FAIL;
}

iot_err_t IOT_WifiConnect(uint8_t iface_num, const char *target_ssid, const char *target_password, uint8_t retryTimes)
{
    iot_wifi_auth_mode_t target_auth_mode = IOT_WIFI_AUTH_WPA_WPA2_PSK;

    if (target_ssid == NULL || target_password == NULL)
    {
        IOT_LOGE("iot_wifi", "SSID or password is NULL");
        return IOT_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    wifiAutoReconnect = false; /* Suppress auto-reconnect during WiFi switch */
    ret = esp_wifi_disconnect();
    if (ret != ESP_OK)
    {
        IOT_LOGE("iot_wifi", "Failed to disconnect from WiFi before connecting: %s", esp_err_to_name(ret));
        return IOT_ERR_FAIL;
    }
    ret = esp_wifi_stop();
    if (ret != ESP_OK)
    {
        IOT_LOGE("iot_wifi", "Failed to stop WiFi: %s", esp_err_to_name(ret));
        return IOT_ERR_FAIL;
    }

    wifi_config_t wifi_config = {0};
    Str_SafeCopy((char *) wifi_config.sta.ssid, target_ssid, sizeof(wifi_config.sta.ssid));
    Str_SafeCopy((char *) wifi_config.sta.password, target_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.failure_retry_cnt = retryTimes;
    convertAuthMode(&target_auth_mode, &wifi_config.sta.threshold.authmode, false);

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK)
    {
        IOT_LOGE("iot_wifi", "Failed to set WiFi config to connect: %s", esp_err_to_name(ret));
        return IOT_ERR_FAIL;
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        IOT_LOGE("iot_wifi", "Failed to start WiFi to connect: %s", esp_err_to_name(ret));
        return IOT_ERR_FAIL;
    }

    wifiAutoReconnect = true;
    ret = esp_wifi_connect();
    if (ret != ESP_OK)
    {
        IOT_LOGE("iot_wifi", "Failed to connect to WiFi: %s", esp_err_to_name(ret));
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_WifiDisconnect(uint8_t iface_num)
{
    wifiAutoReconnect = false;
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK)
    {
        IOT_LOGE("iot_wifi", "Failed to disconnect from WiFi: %s", esp_err_to_name(ret));
        return IOT_ERR_FAIL;
    }
    IOT_LOGW("iot_wifi", "Disconnected from WiFi");
    return IOT_OK;
}

iot_ap_info_t *IOT_WifiScan(uint8_t iface_num, uint8_t *maxDevice, uint8_t timeout_sec)
{
    if (*maxDevice == 0 || timeout_sec == 0)
    {
        IOT_LOGE("iot_wifi", "maxDevice or timeout is invalid");
        return NULL;
    }

    uint16_t ap_count = *maxDevice;

    wifi_ap_record_t *ap_records =
        (wifi_ap_record_t *) Mem_SafeCalloc(ap_count, sizeof(wifi_ap_record_t), "iot_wifi", "ap records");
    if (!ap_records)
    {
        IOT_LOGE("iot_wifi", "Failed to allocate memory for AP records");
        return NULL;
    }
    TickType_t scanStart = xTaskGetTickCount();
    ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true)); // Blocking scan

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));
    IOT_LOGI("iot_wifi", "Found %d APs", ap_count);

    /* Pad so total time reaches 2/3 of timeout_sec (leave 1/3 headroom for caller) */
    TickType_t elapsed = xTaskGetTickCount() - scanStart;
    TickType_t target = pdMS_TO_TICKS((timeout_sec * 1000 * 2) / 3);
    if (elapsed < target)
    {
        vTaskDelay(target - elapsed);
    }

    iot_ap_info_t *scanned_aps =
        (iot_ap_info_t *) Mem_SafeCalloc(ap_count, sizeof(iot_ap_info_t), "iot_wifi", "scan results");
    if (!scanned_aps)
    {
        IOT_LOGE("iot_wifi", "Failed to allocate memory for result");
        SAFE_FREE(ap_records);
        return NULL;
    }

    for (int i = 0; i < ap_count; i++)
    {
        Str_SafeCopy(scanned_aps[i].ssid, (const char *) ap_records[i].ssid, sizeof(scanned_aps[i].ssid));
        memcpy(scanned_aps[i].bssid, ap_records[i].bssid, sizeof(scanned_aps[i].bssid));
        scanned_aps[i].rssi = ap_records[i].rssi;
        scanned_aps[i].primary = ap_records[i].primary;
        scanned_aps[i].authmode = ap_records[i].authmode;
    }

    SAFE_FREE(ap_records);
    *maxDevice = ap_count; // Update maxDevice with the actual number of scanned APs
    return scanned_aps;
}

wifi_event_status_t IOT_WifiGetConnectionStatus(uint8_t iface_num)
{
    return currentWifiStatus;
}

void IOT_WifiSetAutoReconnect(bool enable)
{
    wifiAutoReconnect = enable;
}

bool IOT_WifiGetAutoReconnect(void)
{
    return wifiAutoReconnect;
}

iot_wifi_auth_mode_t IOT_WifiGetAuth(void)
{
    wifi_ap_record_t apInfo;
    if (esp_wifi_sta_get_ap_info(&apInfo) != ESP_OK)
        return IOT_WIFI_AUTH_OPEN;
    return (iot_wifi_auth_mode_t) apInfo.authmode;
}

int8_t IOT_WifiGetRssi(void)
{
    wifi_ap_record_t apInfo;
    if (esp_wifi_sta_get_ap_info(&apInfo) != ESP_OK)
        return 0;
    return apInfo.rssi;
}

iot_err_t IOT_WifiGetIpInfo(uint8_t iface_num, IOT_WifiIpInfo_t *ipInfo)
{
    if (ipInfo == NULL)
        return IOT_ERR_INVALID_ARG;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL)
        return IOT_ERR_FAIL;

    esp_netif_ip_info_t espIp = {0};
    if (esp_netif_get_ip_info(netif, &espIp) != ESP_OK)
        return IOT_ERR_FAIL;

    ipInfo->ip = espIp.ip.addr;
    ipInfo->netmask = espIp.netmask.addr;
    ipInfo->gateway = espIp.gw.addr;
    return IOT_OK;
}
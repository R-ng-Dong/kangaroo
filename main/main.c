#include <stdbool.h>
#include <stdio.h>
#include "esp_mac.h"
#include "IOT_ErrorManager.h"
#include "IOT_Log.h"
#include "IOT_Platform.h"
#include "IOT_Core.h"
#include "ble/IOT_BleMgmt.h"

#include "cloud_event_handler.h"
#include "app.h"

#include "freertos/FreeRTOS.h"
#include "factory_test_ble.h"
#include "factory_test_udp.h"

#define STAGING_ENVIRONMENT 0
#define PRODUCT_ENVIRONMENT !STAGING_ENVIRONMENT

static const char *TAG = "MAIN";

void app_main(void)
{
#if 1
    iot_err_t err = IOT_OK;

    // Step 1: Initialize platform (NVS, WiFi, BLE, MQTT, etc.)
    err = IOT_InitPlatform();
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "IOT_InitPlatform failed: %s", iot_err_to_name(err));
        return;
    }

    // Step 3: Register event callbacks
    err = DeviceRegisterEvent();
    if (err != IOT_OK)
    {
        IOT_LOGE("MAIN", "Failed to register event callback");
    }

#endif

    esp_err_t esp_err = AppCommonInit();
    if (esp_err != ESP_OK)
    {
        IOT_LOGE("MAIN", "Failed to init AppCommon");
    }

    uint8_t wifi_mac[6];
    if (esp_read_mac(wifi_mac, ESP_MAC_WIFI_STA) == ESP_OK)
    {
        char ble_name[sizeof("KGTEST_") + 12U];
        snprintf(ble_name, sizeof(ble_name), "KGTEST_%02x%02x%02x%02x%02x%02x",
                 wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4], wifi_mac[5]);
        err = IOT_BleSetAdvertisingNameOverride(ble_name);
        if (err == IOT_OK)
            IOT_LOGI(TAG, "BLE scan-response name: %s", ble_name);
        else
            IOT_LOGE(TAG, "Failed to set BLE scan-response name: %s", iot_err_to_name(err));
    }
    else
    {
        IOT_LOGE(TAG, "Failed to read Wi-Fi MAC for BLE name");
    }

    WaterPurifierType model = AppCheckModel();
    if (WATER_PURIFIER_TYPE_STANDARD == model)
    {
        static const char *MODEL_ID = APP_PRODUCT_ID_STANDARD;
        err = IOT_CoreInit(MODEL_ID);
    }
    else if (WATER_PURIFIER_TYPE_HOT_COLD == model)
    {
        static const char *MODEL_ID = APP_PRODUCT_ID_HOT_COLD;
        err = IOT_CoreInit(MODEL_ID);
    }
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "IOT_CoreInit failed: %s", iot_err_to_name(err));
        return;
    }

    esp_err = AppInit(model);
    if (esp_err != ESP_OK)
    {
        IOT_LOGE("MAIN", "Failed to init AppInit");
    }

    //
    if (!IOT_CoreIsProvisioned())
    {
        // RPA init
        esp_err = FactoryTestUdpStart();
        if (esp_err != ESP_OK)
        {
            IOT_LOGE(TAG, "Factory UDP test failed to start: %s", esp_err_to_name(esp_err));
        }
    }
    else
    {
        IOT_LOGI(TAG, "Factory test skipped: device is provisioned");
    }

    IOT_LOGI(TAG, "============= Version %s =============\n", CONFIG_APP_PROJECT_VER);
    return;
}

#include <stdbool.h>
#include <stdio.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "IOT_ErrorManager.h"
#include "IOT_Log.h"
#include "IOT_Platform.h"
#include "IOT_Core.h"
#include "ble/IOT_BleMgmt.h"

#include "cloud_event_handler.h"
#include "app.h"
#include "record.h"

#include "factory_test_udp.h"
#include "define.h"

#define STAGING_ENVIRONMENT 0
#define PRODUCT_ENVIRONMENT !STAGING_ENVIRONMENT

static const char *TAG = "MAIN";

#define FACTORY_TEST 1

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

    _bool ret = D_TRUE;
    WaterPurifierType model = AppCheckModel();
    if (WATER_PURIFIER_TYPE_STANDARD == model)
    {
        static const char *MODEL_ID = "00000101C0040004";
        ret = AppRecordInit(MODEL_ID);
    }
    else if (WATER_PURIFIER_TYPE_HOT_COLD == model)
    {
        static const char *MODEL_ID = "00000101C0040003";
        ret = AppRecordInit(MODEL_ID);
    }
    if (ret != D_TRUE)
    {
        IOT_LOGE(TAG, "IOT_CoreInit failed: %s", iot_err_to_name(err));
        return;
    }

    esp_err = AppInit(model);
    if (esp_err != ESP_OK)
    {
        IOT_LOGE("MAIN", "Failed to init AppInit");
    }

#if FACTORY_TEST
    if (!IOT_CoreIsProvisioned())
    {
        // UI8 wifi_mac[6];
        // if (esp_read_mac(wifi_mac, ESP_MAC_WIFI_STA) == ESP_OK)
        // {
        //     char ble_name[sizeof("KANGAROO_") + 12U];
        //     snprintf(ble_name, sizeof(ble_name), "KANGAROO_%02x%02x%02x%02x%02x%02x",
        //              wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4], wifi_mac[5]);
        //     err = IOT_BleSetAdvertisingNameOverride(ble_name);
        //     if (err == IOT_OK)
        //         IOT_LOGI(TAG, "BLE scan-response name: %s", ble_name);
        //     else
        //         IOT_LOGE(TAG, "Failed to set BLE scan-response name: %s", iot_err_to_name(err));
        // }
        // else
        // {
        //     IOT_LOGE(TAG, "Failed to read Wi-Fi MAC for BLE name");
        // }
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
#endif

    IOT_LOGI(TAG, "============= Version %s =============\n", CONFIG_APP_PROJECT_VER);
    return;
}

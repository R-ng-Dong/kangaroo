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

#include "IOT_CoreBleAdv.h"

#define STAGING_ENVIRONMENT 0
#define PRODUCT_ENVIRONMENT !STAGING_ENVIRONMENT

static const char *TAG = "MAIN";

#define FACTORY_TEST 1

static volatile uint8_t s_batteryPercent = 100;

static void onBleAdv(const IOT_CoreBleAdvContext_t *ctx,
                     IOT_CoreBleAdvFields_t *fields,
                     void *arg)
{
    (void)arg;

    if (ctx->purpose == IOT_CORE_BLE_ADV_PROVISION && ctx->advFree >= 5)
    {
        uint8_t battery[3] = {0x0F, 0x18, s_batteryPercent};

        IOT_CoreBleAdvAddField(fields,
                               IOT_CORE_BLE_ADV_PACKET,
                               0x16,
                               battery,
                               sizeof(battery));
    }
}

// test
void onBatteryChanged(uint8_t percent)
{
    s_batteryPercent = percent;
    IOT_CoreBleAdvRefresh();
}

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

#if FACTORY_TEST
    /* Stop the factory UDP listener as soon as provisioning begins. Register
     * before event callbacks so an early provisioning event is not missed. */
    CloudRegisterProvisionStartedCallback(FactoryTestUdpStop);
#endif

    // Step 3: Register event callbacks
    err = DeviceRegisterEvent();
    if (err != IOT_OK)
    {
        IOT_LOGE("MAIN", "Failed to register event callback");
    }

#endif
    IOT_CoreBleAdvSetCustomizer(onBleAdv, NULL); 

    esp_err_t esp_err = AppCommonInit();
    if (esp_err != ESP_OK)
    {
        IOT_LOGE("MAIN", "Failed to init AppCommon");
    }

    const char *MODEL_ID = (char *)AppCheckModel();
    IOT_LOGW("MAIN", "modelID: %s", MODEL_ID);
    if (AppRecordInit(MODEL_ID) != D_TRUE)
    {
        IOT_LOGE(TAG, "IOT_CoreInit failed: %s", iot_err_to_name(err));
        return;
    }
    WaterPurifierType model = AppGetModel();
    esp_err = AppInit(model);
    if (esp_err != ESP_OK)
    {
        IOT_LOGE("MAIN", "Failed to init AppInit");
    }

#if FACTORY_TEST
    if (!IOT_CoreIsProvisioned())
    {
        // RPA init
        esp_err = FactoryTestUdpStart();
        if (esp_err != ESP_OK)
        {
            IOT_LOGE(TAG, "Factory UDP test failed to start: %s", esp_err_to_name(esp_err));
        }
        else{
            onBatteryChanged(10);
        }
    }
    else
    {
        IOT_LOGI(TAG, "Factory test skipped: device is provisioned");
    }
#endif

    IOT_LOGI(TAG, "=============[TEST-PRODUCT] Version %s(4) =============\n", CONFIG_APP_PROJECT_VER);
    return;
}

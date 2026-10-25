#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_Log.h"
#include "IOT_Platform.h"
#include "IOT_Core.h"

#include "cloud_event_handler.h"
#include "app.h"

#include "freertos/FreeRTOS.h"

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

    WaterPurifierType model = AppCheckModel();
    if (WATER_PURIFIER_TYPE_STANDARD == model)
    {
        static const char *MODEL_ID = "00000101C0040003";
        err = IOT_CoreInit(MODEL_ID);
    }
    else if (WATER_PURIFIER_TYPE_HOT_COLD == model)
    {
        static const char *MODEL_ID = "00000101C0040003";
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
    }

    IOT_LOGI(TAG, "============= Version %s =============\n", CONFIG_APP_PROJECT_VER);
    return;
}

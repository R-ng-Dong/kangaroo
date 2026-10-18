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

    // Step 2: Initialize core with your model ID
#if STAGING_ENVIRONMENT
    static const char *MODEL_ID = "00000101C0040001";
#endif

#if PRODUCT_ENVIRONMENT
    static const char *MODEL_ID = "00000101C0040003";
#endif
    err = IOT_CoreInit(MODEL_ID);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "IOT_CoreInit failed: %s", iot_err_to_name(err));
        return;
    }
    // Step 3: Register event callbacks
    err = DeviceRegisterEvent();
    if (err != IOT_OK)
    {
        IOT_LOGE("MAIN", "Failed to register event callback");
    }

    IOT_LOGI(TAG, "============= Version %s =============\n", CONFIG_APP_PROJECT_VER);

#endif

    AppInit();
    return;
}

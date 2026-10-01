#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_Log.h"
#include "IOT_Platform.h"
#include "IOT_Core.h"

#include "cloud_event_handler.h"
#include "water_profile.h"
#include "rpa.h"
#include "led_mgmt.h"

#define STAGING_ENVIRONMENT 1
#define PRODUCT_ENVIRONMENT !STAGING_ENVIRONMENT

static const char *TAG = "MAIN";



extern "C" void app_main(void)
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
    static const char *MODEL_ID = "000001000C040001";
#endif
    err = IOT_CoreInit(MODEL_ID);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "IOT_CoreInit failed: %s", iot_err_to_name(err));
        return;
    }
    // Step 3: Register event callbacks
    err = devRegisterEvent();
    if (err != IOT_OK)
    {
        IOT_LOGE("MAIN", "Failed to register event callback");
    }

    err = IOT_CoreStopProvision();
    if (err != IOT_OK) {
        IOT_LOGE("MAIN", "Failed to stop provision");
    }

    IOT_LOGI(TAG, "============= Version %s =============\n", CONFIG_APP_PROJECT_VER);

#endif
    LedScreen::getInstance();
    rpa_init_wifi_default();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    WaterProfileInit();
    
    // vTaskDelay(5000 / portTICK_PERIOD_MS);
    // WaterProfileEnableCommandTest(1);

    // vTaskDelay(10000 / portTICK_PERIOD_MS);
    // WaterProfileResetSystem();

    // SendSystemResetCommand();
    // SetFeatureEnable(HOT_FEATURE,0 ,120); vTaskDelay(200 / portTICK_PERIOD_MS);
    // SetFeatureEnable(HOT_FEATURE,0 ,80);
    // SetFeatureEnable(ENERGY_SAVING_FEATURE,0 ,0);

    // vTaskDelay(10000 / portTICK_PERIOD_MS);
    // FCT_EnableCommand(1);
    // vTaskDelay(5000 / portTICK_PERIOD_MS);
    // FCT_EnableCommand(0);
    // FCT_EnableCommand(0);



    // TDS_test();

    return;
}

void rpa_test_led_rgb(uint8_t red, uint8_t green, uint8_t blue){
    // printf("RGB: %d-%d-%d \n", red, green, blue);
    LedScreen::getInstance()->ledScreenSetColor(red, green, blue);
    // lc8823_load_value(&led_obj, ON_STATE, 50, red, green, blue);
    // lc8823_set_state(&led_obj, ON_STATE);    
    // lc8823_send_frame_buffer();
}
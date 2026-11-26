#include <stdbool.h>
#include "esp_mac.h"
#include "IOT_ErrorManager.h"
#include "IOT_Log.h"
#include "IOT_Platform.h"
#include "IOT_Core.h"

#include "cloud_event_handler.h"
#include "app.h"

#include "freertos/FreeRTOS.h"
#include "record.h"

#include "IOT_CoreBleAdv.h"


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
    if(esp_err != ESP_OK)
    {
        IOT_LOGE("MAIN", "Failed to init AppCommon");
    }

    AppCheckModel();

    UI8 MODEL_ID[PID_MAX_LENGTH];
    AppGetModelId(MODEL_ID);
    IOT_LOGW("MAIN", "modelID: %.*s", PID_MAX_LENGTH, (char *)MODEL_ID);

    UI8 vendor[SERIAL_MAX_LENGTH];
    AppGetSerial(vendor);
    IOT_LOGW("MAIN", "Serial: %.*s", SERIAL_MAX_LENGTH, (char *)vendor);
    IOT_CoreBleAdvSetVendorData(vendor, sizeof(vendor));

    if (AppRecordInit((char *)MODEL_ID) != D_TRUE)
    {
        IOT_LOGE(TAG, "IOT_CoreInit failed: %s", iot_err_to_name(err));
        return;
    }



    WaterPurifierType model = AppGetModel();
    esp_err = AppInit(model);
    if(esp_err != ESP_OK)
    {
        IOT_LOGE("MAIN", "Failed to init AppInit");
    }

    IOT_LOGI(TAG, "============= Version %s (5) =============\n", CONFIG_APP_PROJECT_VER);
    return;
}

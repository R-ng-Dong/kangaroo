#pragma once

// Only include sdkconfig.h when building under ESP-IDF
#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#endif

// ---- ESP-IDF chip detection ----

#if defined(CONFIG_IDF_TARGET_ESP32)
    #define CONFIG_DEVICE_SUPPORT_BLE
    #define CONFIG_DEVICE_SUPPORT_INTERNAL_EMAC_ETHERNET   // ESP32 classic only

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    #define CONFIG_DEVICE_SUPPORT_BLE

#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    #define CONFIG_DEVICE_SUPPORT_BLE

#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    #define CONFIG_DEVICE_SUPPORT_BLE
    // NO internal EMAC on ESP32-C6
#endif



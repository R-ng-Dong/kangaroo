#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h"

/*
 * SETTING events: WiFi changed.
 *
 * Split out of IOT_CoreEventHandler.h on 2026-10-14. That header remains the umbrella
 * (category enum, IOT_CoreEvent_t, callback registration) and includes this file, so
 * existing includes keep working. Prefer this header in new code that only needs it.
 */

/* ============================
 *  SETTING EVENTS
 * ============================ */

#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    SETTING_EVENT_WIFI_CHANGED,
} IOT_EventTypeSetting_t;

/** Longest SSID the event carries, in bytes (IEEE 802.11 caps an SSID at 32 octets). */
#define IOT_CORE_WIFI_SSID_MAX_LEN 32

/**
 * SETTING_EVENT_WIFI_CHANGED: the device stored new WiFi credentials and joined that network.
 *
 * Delivered on core's task, after the change. The SSID is a copy inside the event, so it is
 * valid for the whole handler call; copy it to keep it longer. The password is never carried.
 */
typedef struct
{
    char ssid[IOT_CORE_WIFI_SSID_MAX_LEN + 1]; /**< NUL-terminated SSID of the network now in use */
} IOT_CoreSettingWifiChangedData_t;

typedef struct
{
    IOT_EventTypeSetting_t type;
    union
    {
        IOT_CoreSettingWifiChangedData_t wifiChanged;
    } data;
} IOT_CoreSettingEvent_t;
#ifdef __cplusplus
}
#endif

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

typedef struct
{
    const char *ssid;
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

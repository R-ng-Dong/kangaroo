#include "warning_manager.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static UI32 s_warning_mask = 0U;
static portMUX_TYPE s_warning_lock = portMUX_INITIALIZER_UNLOCKED;

__attribute__((weak)) void WarningManagerOnChanged(UI32 active_mask, UI32 changed_mask)
{
    for (UI32 bit = 1U; bit != 0U; bit <<= 1U)
    {
        if ((changed_mask & bit) != 0U)
        {
            WarningId id = (WarningId)bit;
            ESP_LOGI("WAR", "Warning %s: %s (active mask=0x%08lx)",
                   (active_mask & bit) ? "set" : "cleared",
                   WarningManagerGetName(id),
                   (unsigned long)active_mask);
        }
    }
}

void WarningManagerInit(void)
{
    taskENTER_CRITICAL(&s_warning_lock);
    s_warning_mask = 0U;
    taskEXIT_CRITICAL(&s_warning_lock);
}

void WarningManagerClearAll(void)
{
    taskENTER_CRITICAL(&s_warning_lock);
    UI32 old_mask = s_warning_mask;
    s_warning_mask = 0U;
    taskEXIT_CRITICAL(&s_warning_lock);
    if (old_mask != 0U)
    {
        WarningManagerOnChanged(0U, old_mask);
    }
}

void WarningManagerSet(WarningId warning_id)
{
    if (warning_id == WARNING_ID_NONE)
    {
        return;
    }

    taskENTER_CRITICAL(&s_warning_lock);
    UI32 old_mask = s_warning_mask;
    s_warning_mask |= (UI32)warning_id;
    UI32 changed_mask = s_warning_mask ^ old_mask;
    UI32 active_mask = s_warning_mask;
    taskEXIT_CRITICAL(&s_warning_lock);
    if (changed_mask != 0U)
    {
        WarningManagerOnChanged(active_mask, changed_mask);
    }
}

void WarningManagerClear(WarningId warning_id)
{
    if (warning_id == WARNING_ID_NONE)
    {
        return;
    }

    taskENTER_CRITICAL(&s_warning_lock);
    UI32 old_mask = s_warning_mask;
    s_warning_mask &= ~((UI32)warning_id);
    UI32 changed_mask = s_warning_mask ^ old_mask;
    UI32 active_mask = s_warning_mask;
    taskEXIT_CRITICAL(&s_warning_lock);
    if (changed_mask != 0U)
    {
        WarningManagerOnChanged(active_mask, changed_mask);
    }
}

bool WarningManagerIsActive(WarningId warning_id)
{
    if (warning_id == WARNING_ID_NONE)
    {
        return false;
    }

    taskENTER_CRITICAL(&s_warning_lock);
    bool active = ((s_warning_mask & (UI32)warning_id) != 0U);
    taskEXIT_CRITICAL(&s_warning_lock);
    return active;
}

bool WarningManagerHasAnyWarning(void)
{
    taskENTER_CRITICAL(&s_warning_lock);
    bool has_warning = s_warning_mask != 0U;
    taskEXIT_CRITICAL(&s_warning_lock);
    return has_warning;
}

UI32 WarningManagerGetMask(void)
{
    taskENTER_CRITICAL(&s_warning_lock);
    UI32 mask = s_warning_mask;
    taskEXIT_CRITICAL(&s_warning_lock);
    return mask;
}

UI32 WarningManagerGetWarningCount(void)
{
    UI32 count = 0U;
    UI32 mask = WarningManagerGetMask();

    while (mask != 0U)
    {
        mask &= (mask - 1U);
        count++;
    }

    return count;
}

void WarningManagerUpdate(WarningId warning_id, bool should_be_active)
{
    if (WarningManagerIsActive(warning_id) == should_be_active)
    {
        return;
    }
    EventID event_id = WarningToEvent(warning_id);
    UI8 is_error = 0;
    if (should_be_active)
    {
        WarningManagerSet(warning_id);
        is_error = 1;
    }
    else
    {
        WarningManagerClear(warning_id);
        is_error = 0;
    }
    EventManagerPost(event_id, &is_error, sizeof(is_error));
}

const char *WarningManagerGetName(WarningId warning_id)
{
    switch (warning_id)
    {
        case WARNING_ID_ER00051_INLET_WATER_QUALITY_WARNING:
            return "ER00051_INLET_WATER_QUALITY_WARNING";
        case WARNING_ID_ER00052_OUTLET_WATER_QUALITY_FAULT:
            return "ER00052_OUTLET_WATER_QUALITY_FAULT";
        case WARNING_ID_ER00053_WATER_LEAK:
            return "ER00053_WATER_LEAK";
        case WARNING_ID_ER00054_TANK_LOW:
            return "ER00054_TANK_LOW";
        case WARNING_ID_ER00055_RO_PUMP_RUNTIME_OVER_60_MIN:
            return "ER00055_RO_PUMP_RUNTIME_OVER_60_MIN";
        case WARNING_ID_ER00056_FILTER1_LIFE_70:
            return "ER00056_FILTER1_LIFE_70";
        case WARNING_ID_ER00057_FILTER2_LIFE_70:
            return "ER00057_FILTER2_LIFE_70";
        case WARNING_ID_ER00058_FILTER3_LIFE_70:
            return "ER00058_FILTER3_LIFE_70";
        case WARNING_ID_ER00059_FILTER4_LIFE_70:
            return "ER00059_FILTER4_LIFE_70";
        case WARNING_ID_ER00060_FILTER5_LIFE_70:
            return "ER00060_FILTER5_LIFE_70";
        case WARNING_ID_ER00061_FILTER1_LIFE_90:
            return "ER00061_FILTER1_LIFE_90";
        case WARNING_ID_ER00062_FILTER2_LIFE_90:
            return "ER00062_FILTER2_LIFE_90";
        case WARNING_ID_ER00063_FILTER3_LIFE_90:
            return "ER00063_FILTER3_LIFE_90";
        case WARNING_ID_ER00064_FILTER4_LIFE_90:
            return "ER00064_FILTER4_LIFE_90";
        case WARNING_ID_ER00065_FILTER5_LIFE_90:
            return "ER00065_FILTER5_LIFE_90";
        case WARNING_ID_ER00066_OUTPUT_WATER_QUALITY_WARNING:
            return "ER00066_OUTPUT_WATER_QUALITY_WARNING";
        case WARNING_ID_ER00067_HEATER_SENSOR_FAULT:
            return "ER00067_HEATER_SENSOR_FAULT";
        case WARNING_ID_ER00068_HEATER_RUNTIME_OVER_1H:
            return "ER00068_HEATER_RUNTIME_OVER_1H";
        case WARNING_ID_ER00069_COOLER_SENSOR_FAULT:
            return "ER00069_COOLER_SENSOR_FAULT";
        case WARNING_ID_ER00070_COOLER_RUNTIME_OVER_2H:
            return "ER00070_COOLER_RUNTIME_OVER_2H";
        case WARNING_ID_ER00071:
            return "ER00071";
        case WARNING_ID_ER00072:
            return "ER00072";
        case WARNING_ID_ER00073:
            return "ER00073";
        case WARNING_ID_ER00074:
            return "ER00074";
        case WARNING_ID_NONE:
        default:
            return "NONE";
    }
}

EventID WarningToEvent(WarningId Warning_id)
{
    switch (Warning_id)
    {
        case WARNING_ID_ER00051_INLET_WATER_QUALITY_WARNING:
            return EVENT_INLET_WATER_QUALITY_WARNING;
        case WARNING_ID_ER00052_OUTLET_WATER_QUALITY_FAULT:
            return EVENT_OUTLET_WATER_QUALITY_FAULT;
        case WARNING_ID_ER00053_WATER_LEAK:
            return EVENT_WATER_LEAK;
        case WARNING_ID_ER00054_TANK_LOW:
            return EVENT_TANK_LOW;
        case WARNING_ID_ER00055_RO_PUMP_RUNTIME_OVER_60_MIN:
            return EVENT_RO_PUMP_RUNTIME_OVER_60_MIN;
        case WARNING_ID_ER00056_FILTER1_LIFE_70:
            return EVENT_FILTER1_LIFE_70;
        case WARNING_ID_ER00057_FILTER2_LIFE_70:
            return EVENT_FILTER2_LIFE_70;
        case WARNING_ID_ER00058_FILTER3_LIFE_70:
            return EVENT_FILTER3_LIFE_70;
        case WARNING_ID_ER00059_FILTER4_LIFE_70:
            return EVENT_FILTER4_LIFE_70;
        case WARNING_ID_ER00060_FILTER5_LIFE_70:
            return EVENT_FILTER5_LIFE_70;
        case WARNING_ID_ER00061_FILTER1_LIFE_90:
            return EVENT_FILTER1_LIFE_90;
        case WARNING_ID_ER00062_FILTER2_LIFE_90:
            return EVENT_FILTER2_LIFE_90;
        case WARNING_ID_ER00063_FILTER3_LIFE_90:
            return EVENT_FILTER3_LIFE_90;
        case WARNING_ID_ER00064_FILTER4_LIFE_90:
            return EVENT_FILTER4_LIFE_90;
        case WARNING_ID_ER00065_FILTER5_LIFE_90:
            return EVENT_FILTER5_LIFE_90;
        case WARNING_ID_ER00066_OUTPUT_WATER_QUALITY_WARNING:
            return EVENT_OUTPUT_WATER_QUALITY_WARNING;
        case WARNING_ID_ER00067_HEATER_SENSOR_FAULT:
            return EVENT_HEATER_SENSOR_FAULT;
        case WARNING_ID_ER00068_HEATER_RUNTIME_OVER_1H:
            return EVENT_HEATER_RUNTIME_OVER_1H;
        case WARNING_ID_ER00069_COOLER_SENSOR_FAULT:
            return EVENT_COOLER_SENSOR_FAULT;
        case WARNING_ID_ER00070_COOLER_RUNTIME_OVER_2H:
            return EVENT_COOLER_RUNTIME_OVER_2H;
        default:
            return EVENT_MAX_NUM;
    }
}

#include "water_controller.h"
#include "esp_err.h"
#include "uart_protocol.h"

static _bool WaterControllerResult(esp_err_t result)
{
    return (result == ESP_OK) ? D_TRUE : D_FALSE;
}

_bool WaterControllerResetSystem(void)
{
    return WaterControllerResult(SendSystemResetCommand());
}

_bool WaterControllerSetFilterRuntimeMinutes(FilterTimeIndex filter_index, UI16 time_m)
{
    WaterPurifyFilter filter;

    switch (filter_index)
    {
    case FILTER_TIME_INDEX_1ST:
        filter = FIRST_FILTER;
        break;
    case FILTER_TIME_INDEX_2ND:
        filter = SECOND_FILTER;
        break;
    case FILTER_TIME_INDEX_3RD:
        filter = THIRD_FILTER;
        break;
    case FILTER_TIME_INDEX_RO:
        filter = RO_FILTER;
        break;
    case FILTER_TIME_INDEX_POST:
        filter = POST_FILTER;
        break;
    default:
        return D_FALSE;
    }

    return WaterControllerResult(SetTimeFilter(filter, time_m));
}

_bool WaterControllerSetFeatureHot(UI8 enabled, UI8 temperature)
{
    if(MIN_TEMPERATURE_HOT_TANK > temperature) 
        temperature = MIN_TEMPERATURE_HOT_TANK;
    else if(MAX_TEMPERATURE_HOT_TANK < temperature) 
        temperature = MAX_TEMPERATURE_HOT_TANK;
    UI8 value = temperature*2;
    return WaterControllerResult(SetFeatureEnable(HOT_FEATURE, enabled, value));
}

_bool WaterControllerSetFeatureCold(UI8 enabled, UI8 temperature)
{
    if(MIN_TEMPERATURE_COLD_TANK > temperature) 
        temperature = MIN_TEMPERATURE_COLD_TANK;
    else if(MAX_TEMPERATURE_COLD_TANK < temperature) 
        temperature = MAX_TEMPERATURE_COLD_TANK;
    UI8 value = temperature*2;
    return WaterControllerResult(SetFeatureEnable(COLD_FEATURE, enabled, value));
}

_bool WaterControllerSetFeatureEnergySaving(UI8 enabled)
{
    return WaterControllerResult(SetFeatureEnable(ENERGY_SAVING_FEATURE, enabled, enabled));
}

_bool WaterControllerSetTDSValue(UI16 tds_inlet, UI16 tds_ro)
{
    return WaterControllerResult(SetTDSValue(tds_inlet, tds_ro));
}

_bool WaterControllerEnableCommandTest(UI8 enabled)
{
    return WaterControllerResult(EnableCommand(enabled));
}

_bool WaterControllerSetOutputControl(UI8 output_idx, UI8 control_value)
{
    return WaterControllerResult(SetOutputControl(output_idx, control_value));
}

_bool WaterControllerSetLedControl(UI8 led_idx, UI8 control_value)
{
    return WaterControllerResult(SetLedControl(led_idx, control_value));
}

_bool WaterControllerSetDeviceId(UI32 dev_id)
{
    return WaterControllerResult(SetDeviceId(dev_id));
}

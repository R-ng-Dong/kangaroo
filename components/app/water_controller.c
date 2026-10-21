#include "water_controller.h"

#include "esp_err.h"
#include "water_controller_task.h"
#include "uart_protocol.h"

static _bool WaterControllerResult(esp_err_t result)
{
    return (result == ESP_OK) ? D_TRUE : D_FALSE;
}


_bool WaterControllerInit(void)
{
    return WaterControllerTaskInit();
}

_bool WaterControllerTest(void)
{
    return WaterControllerResult(EnableCommand(1)); // auto 1
}

_bool WaterControllerResetSystem(void)
{
    return WaterControllerResult(SendSystemResetCommand());
}

_bool WaterControllerSetFilterRuntimeMinutes(FilterTimeIndex filter_index, UI16 time_m)
{
    // static const WaterPurifyFilter filters[] = {FIRST_FILTER, SECOND_FILTER, THIRD_FILTER, RO_FILTER, POST_FILTER};
    if (filter_index > FILTER_TIME_INDEX_POST)
    {
        return D_FALSE;
    }
    printf("WaterControllerSetFilterRuntimeMinutes: %d\n", time_m);

    WaterControlRequest_t request = {0};
    request.command = WATER_CONTROL_SEND_FILTER;

    request.value1 = (UI32)filter_index;
    request.verify1 = WATER_CONTROL_VERIFY_NONE;

    request.value2 = time_m;
    request.expected2 = (UI32)time_m * 60;
    request.verify2 = WATER_CONTROL_VERIFY_FILTER_TIME;
    return WaterControllerTaskEnqueue(&request);
}

_bool WaterControllerSetFeatureHot(UI8 enabled, UI8 temperature)
{
    if (enabled > 1U) return D_FALSE;
    if (temperature < MIN_TEMPERATURE_HOT_TANK) temperature = MIN_TEMPERATURE_HOT_TANK;
    if (temperature > MAX_TEMPERATURE_HOT_TANK) temperature = MAX_TEMPERATURE_HOT_TANK;
    printf("set feature hot: %d, temp: %d\n", enabled, temperature);
    WaterControlRequest_t request = {0};
    request.command = WATER_CONTROL_SEND_FEATURE_HOT;
    
    request.value1 = enabled ? 1 : 0;
    request.expected1 = enabled ? HOT_SWITCH_BIT : 0;
    request.verify1 = WATER_CONTROL_VERIFY_SETTING_BIT;
    if (enabled)
    {
        request.value2 = temperature * 2;
        request.expected2 = (UI32)(temperature * 2); // target temperature
        request.verify2 = WATER_CONTROL_VERIFY_HOT_TEMPERATURE;
    }
    return WaterControllerTaskEnqueue(&request);
}

_bool WaterControllerSetFeatureCold(UI8 enabled, UI8 temperature)
{
    if (enabled > 1U) return D_FALSE;
    if (temperature < MIN_TEMPERATURE_COLD_TANK) temperature = MIN_TEMPERATURE_COLD_TANK;
    if (temperature > MAX_TEMPERATURE_COLD_TANK) temperature = MAX_TEMPERATURE_COLD_TANK;

    printf("set feature cold: %d, temp: %d\n", enabled, temperature);

    WaterControlRequest_t request = {0};
    request.command = WATER_CONTROL_SEND_FEATURE_COLD;
    
    request.value1 = (UI32)enabled ? 1 : 0;
    request.expected1 = enabled ? COLD_SWITCH_BIT : 0;
    request.verify1 = WATER_CONTROL_VERIFY_SETTING_BIT;
    if (enabled)
    {
        request.value2 = temperature * 2;
        request.expected2 = (UI32)(temperature * 2); // target temperature
        request.verify2 = WATER_CONTROL_VERIFY_COLD_TEMPERATURE;
    }
    return WaterControllerTaskEnqueue(&request);
}

_bool WaterControllerSetFeatureEnergySaving(UI8 enabled)
{
    return WaterControllerResult(SetFeatureEnable(ENERGY_SAVING_FEATURE, enabled, enabled));
}

_bool WaterControllerStartROFlushing(UI16 time_s)
{
    printf("RO flushing: %u seconds\n", time_s);

    WaterControlRequest_t request = {0};
    request.command = WATER_CONTROL_SEND_RO_FLUSHING;
    
    request.value1 = (UI32)time_s;
    request.expected1 = RO_STATUS_FLUSHING;
    request.verify1 = WATER_CONTROL_VERIFY_RO_STATUS;

    return WaterControllerTaskEnqueue(&request);
    // return WaterControllerResult(SetTimeFilter(RO_FLUSHING_START, 0U));
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


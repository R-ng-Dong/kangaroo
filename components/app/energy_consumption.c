#include "energy_consumption.h"

#include "system_runtime.h"
#include "water_runtime.h"

#include "esp_timer.h"
#include <stddef.h>


_bool EnergyConsumptionGet(EnergyConsumption_t *out_consumption)
{
    if (out_consumption == NULL)
    {
        return D_FALSE;
    }

    *out_consumption = (EnergyConsumption_t){0};

    WaterRuntimeRefresh();
    if (!WaterRuntimeHasValue())
    {
        return D_TRUE;
    }

    const DeviceRuntimeSnapshot_t runtime = WaterRuntimeGetSnapshot();
    const UI32 now_ms = (UI32)(esp_timer_get_time() / 1000ULL);
    const UI32 system_runtime_seconds = SystemRuntimeGetSeconds(now_ms);

    out_consumption->hot_tank_wh = ((UI64)runtime.total_hot_tank_runtime * HOT_TANK_POWER_W) / SECONDS_PER_HOUR;
    out_consumption->cold_tank_wh = ((UI64)runtime.total_cold_tank_runtime * COLD_TANK_POWER_W) / SECONDS_PER_HOUR;
    out_consumption->mcu_iot_wh = ((UI64)system_runtime_seconds * MCU_IOT_POWER_W) / SECONDS_PER_HOUR;
    out_consumption->pump_valve_wh = ((UI64)runtime.total_pump_runtime * PUMP_VALVE_POWER_W) / SECONDS_PER_HOUR;
    out_consumption->electrolysis_wh = ((UI64)runtime.total_electrolysis_runtime * ELECTROLYSIS_POWER_W) / SECONDS_PER_HOUR;
    out_consumption->total_wh = out_consumption->hot_tank_wh +
                                out_consumption->cold_tank_wh +
                                out_consumption->mcu_iot_wh +
                                out_consumption->pump_valve_wh +
                                out_consumption->electrolysis_wh;

    return D_TRUE;
}

UI64 EnergyConsumptionGetTotal(void)
{
    EnergyConsumption_t out = {0};
    if(!EnergyConsumptionGet(&out))
        return 0;
    return out.total_wh;
}

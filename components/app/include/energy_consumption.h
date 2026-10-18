#ifndef ENERGY_CONSUMPTION_H
#define ENERGY_CONSUMPTION_H

#include "common.h"

typedef struct
{
    UI64 hot_tank_wh;
    UI64 cold_tank_wh;
    UI64 mcu_iot_wh;
    UI64 pump_valve_wh;
    UI64 electrolysis_wh;
    UI64 total_wh;
} EnergyConsumption_t;

_bool EnergyConsumptionGet(EnergyConsumption_t *out_consumption);
UI64 EnergyConsumptionGetTotal(void);

#endif

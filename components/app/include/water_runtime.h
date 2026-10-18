#ifndef WATER_RUNTIME_H
#define WATER_RUNTIME_H

#include <stdbool.h>
#include "common.h"

typedef struct {
    bool valid;
    UI32 total_pump_runtime;
    UI32 total_hot_tank_runtime;
    UI32 total_cold_tank_runtime;
    UI32 total_electrolysis_runtime;
} DeviceRuntimeSnapshot_t;

void WaterRuntimeInit(void);
void WaterRuntimeRefresh(void);
bool WaterRuntimeHasValue(void);
DeviceRuntimeSnapshot_t WaterRuntimeGetSnapshot(void);

#endif

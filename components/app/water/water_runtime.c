#include "water_runtime.h"

#include "app_context.h"
#include "water_profile.h"

#include <string.h>

static DeviceRuntimeSnapshot_t s_snapshot;

void WaterRuntimeInit(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.valid = false;
}

void WaterRuntimeRefresh(void)
{
    WaterPurifierRuntime_t state;
    AppContextGetRuntime(&state);

    if (!AppContextHasRuntime())
    {
        s_snapshot.valid = false;
        return;
    }

    s_snapshot.valid = true;
    s_snapshot.total_pump_runtime = state.total_pump_runtime;
    s_snapshot.total_hot_tank_runtime = state.total_hot_tank_runtime;
    s_snapshot.total_cold_tank_runtime = state.total_cold_tank_runtime;
    s_snapshot.total_electrolysis_runtime = state.total_electrolysis_runtime;
}

bool WaterRuntimeHasValue(void)
{
    return s_snapshot.valid;
}

DeviceRuntimeSnapshot_t WaterRuntimeGetSnapshot(void)
{
    return s_snapshot;
}

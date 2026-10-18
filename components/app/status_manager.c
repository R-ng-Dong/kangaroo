#include "status_manager.h"

#include "app_context.h"
#include "water_profile.h"
#include "warning_manager.h"

#include <string.h>

static DeviceStatusSnapshot_t s_snapshot;

static void StatusManagerUpdateWarning(WarningId warning_id, bool should_be_active)
{
    WarningManagerUpdate(warning_id, should_be_active);
    // if (WarningManagerIsActive(warning_id) == should_be_active)
    // {
    //     return;
    // }

    // if (should_be_active)
    // {
    //     WarningManagerSet(warning_id);
    // }
    // else
    // {
    //     WarningManagerClear(warning_id);
    // }
}

void StatusManagerInit(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.valid = false;
}

void StatusManagerRefresh(void)
{
    WaterProfileState_t state;
    AppContextGetWaterState(&state);

    if (!AppContextHasWaterState())
    {
        s_snapshot.valid = false;
        return;
    }

    s_snapshot.valid = true;
    s_snapshot.ro_status = state.ro_status;
    s_snapshot.ply_status = state.ply_status;
    s_snapshot.hot_tank_status = state.hot_tank_status;
    s_snapshot.cold_tank_status = state.cold_tank_status;
    s_snapshot.setting_val = state.setting_val;
    s_snapshot.input_status = state.input_status;
    s_snapshot.output_status = state.output_status;
    s_snapshot.switch_status = state.switch_status;
    s_snapshot.led_status = state.led_status;
}

bool StatusManagerHasWaterStatus(void)
{
    return s_snapshot.valid;
}

DeviceStatusSnapshot_t StatusManagerGetSnapshot(void)
{
    return s_snapshot;
}

void StatusManagerCheckWarnings(void)
{
    if (!s_snapshot.valid)
    {
        return;
    }

    StatusManagerUpdateWarning(WARNING_ID_ER00055_RO_PUMP_RUNTIME_OVER_60_MIN,
                               s_snapshot.ro_status == RO_STATUS_PUMP_RUNNING_TOO_LONG);
    StatusManagerUpdateWarning(WARNING_ID_ER00067_HEATER_SENSOR_FAULT,
                               s_snapshot.hot_tank_status == HOT_TANK_STATUS_TEMPERATURE_SENSOR_ERROR);
    StatusManagerUpdateWarning(WARNING_ID_ER00068_HEATER_RUNTIME_OVER_1H,
                               s_snapshot.hot_tank_status == HOT_TANK_STATUS_HEATER_RUNNING_TOO_LONG);
    StatusManagerUpdateWarning(WARNING_ID_ER00069_COOLER_SENSOR_FAULT,
                               s_snapshot.cold_tank_status == COLD_TANK_STATUS_TEMPERATURE_SENSOR_ERROR);
    StatusManagerUpdateWarning(WARNING_ID_ER00070_COOLER_RUNTIME_OVER_2H,
                               s_snapshot.cold_tank_status == COLD_TANK_STATUS_COMPRESSOR_RUNNING_TOO_LONG);
}

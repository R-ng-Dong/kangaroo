#ifndef WATER_CONTROLLER_H
#define WATER_CONTROLLER_H

#include "common.h"
#include "water_profile.h"

#define MIN_TEMPERATURE_HOT_TANK 40
#define MAX_TEMPERATURE_HOT_TANK 100 

#define MIN_TEMPERATURE_COLD_TANK 0
#define MAX_TEMPERATURE_COLD_TANK 20 

_bool WaterControllerResetSystem(void);
_bool WaterControllerSetFilterRuntimeMinutes(FilterTimeIndex filter_index, UI16 time_m);
_bool WaterControllerSetFeatureHot(UI8 enabled, UI8 temperature);
_bool WaterControllerSetFeatureCold(UI8 enabled, UI8 temperature);
_bool WaterControllerSetFeatureEnergySaving(UI8 enabled);
_bool WaterControllerSetTDSValue(UI16 tds_inlet, UI16 tds_ro);
_bool WaterControllerEnableCommandTest(UI8 enabled);
_bool WaterControllerSetOutputControl(UI8 output_idx, UI8 control_value);
_bool WaterControllerSetLedControl(UI8 led_idx, UI8 control_value);
_bool WaterControllerSetDeviceId(UI32 dev_id);

#endif

#ifndef RECORD_H_
#define RECORD_H_

#include "IOT_ErrorManager.h"
#include "common.h"

/* Register app history rules before IOT_CoreInit() starts delivering state changes. */
_bool AppRecordInit(const char *modelId);
_bool app_update_water_consumption(uint32_t water_consumption);
_bool app_update_energy_consumption(uint32_t energy_consumption);
_bool app_update_tds_in(uint16_t inlet_ppm);
_bool app_update_tds_out(uint16_t outlet_ppm);

#endif  // RECORD_H_

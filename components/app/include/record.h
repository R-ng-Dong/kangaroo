#ifndef RECORD_H_
#define RECORD_H_

#include "IOT_ErrorManager.h"
#include "common.h"

/* Register app history rules before IOT_CoreInit() starts delivering state changes. */
_bool AppRecordInit(void);
_bool app_update_water_consumption(uint32_t value);
_bool app_update_energy_consumption(uint32_t value);
_bool app_update_energy_saving(uint32_t value);

#endif  // RECORD_H_

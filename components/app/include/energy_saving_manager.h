#ifndef ENERGY_SAVING_MANAGER_H
#define ENERGY_SAVING_MANAGER_H

#include "common.h"

#define ENERGY_SAVING_MANUAL_OVERRIDE_SECONDS (30U * 60U)

typedef enum
{
    ENERGY_SAVING_TANK_HOT = 0,
    ENERGY_SAVING_TANK_COLD = 1
} EnergySavingTank_t;

void EnergySavingManagerInit(void);
_bool EnergySavingManagerSetSchedule(_bool enabled, UI16 start_minute, UI16 end_minute);
void EnergySavingManagerUpdate(UI16 minute_of_day, UI64 now_seconds);
_bool EnergySavingManagerSetTank(EnergySavingTank_t tank, UI8 enabled, UI8 temperature,
                                 UI64 now_seconds);
void EnergySavingManagerGetSavedEnergy(UI64 now_seconds, UI64 *hot_tank_wh, UI64 *cold_tank_wh);

#endif

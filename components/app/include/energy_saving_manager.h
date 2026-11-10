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
_bool EnergySavingManagerClearConfig(void);
_bool EnergySavingManagerSetSchedule(_bool enabled, UI16 start_minute, UI16 end_minute);
void EnergySavingManagerSyncFromMainboard(void);
void EnergySavingManagerUpdate(UI16 minute_of_day, UI64 now_seconds);
_bool EnergySavingManagerSetTank(EnergySavingTank_t tank, UI8 enabled, UI8 temperature,
                                 UI64 now_seconds);
/* Update the target only; the switch state is left untouched. Used by the
 * cloud ATTR_TEMP_SET path (attr 20 is the half-degree wire value, C x 2):
 * the reference never turns a tank on because its setpoint changed. */
_bool EnergySavingManagerSetTankTemperature(EnergySavingTank_t tank, UI8 temperature,
                                            UI64 now_seconds);
/* Current policy switch state (what was last applied to the mainboard). */
_bool EnergySavingManagerGetTankEnabled(EnergySavingTank_t tank, UI8 *enabled);
void EnergySavingManagerGetSavedEnergy(UI64 now_seconds, UI64 *hot_tank_wh, UI64 *cold_tank_wh);
void EnergySavingManagerSyncBaseEnable(EnergySavingTank_t tank);
void EnergySavingGetConfig(UI16 *enable, UI16 *start_minute, UI16 *end_minute);

#endif

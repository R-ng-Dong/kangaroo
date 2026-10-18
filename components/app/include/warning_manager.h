#ifndef WARNING_MANAGER_H
#define WARNING_MANAGER_H

#include <stdbool.h>
#include "common.h"
#include "event_manager.h"

#define RO_PUMP_OVERRUN_TIMEOUT_SECONDS 60*60 

typedef enum {
    WARNING_ID_NONE = 0x00U,

    WARNING_ID_ER00051_INLET_WATER_QUALITY_WARNING = (1U << 0),
    WARNING_ID_ER00052_OUTLET_WATER_QUALITY_FAULT = (1U << 1),
    WARNING_ID_ER00053_WATER_LEAK = (1U << 2), // unuse
    WARNING_ID_ER00054_TANK_LOW = (1U << 3), // unuse
    WARNING_ID_ER00055_RO_PUMP_RUNTIME_OVER_60_MIN = (1U << 4),
    WARNING_ID_ER00056_FILTER1_LIFE_70 = (1U << 5),
    WARNING_ID_ER00057_FILTER2_LIFE_70 = (1U << 6),
    WARNING_ID_ER00058_FILTER3_LIFE_70 = (1U << 7),
    WARNING_ID_ER00059_FILTER4_LIFE_70 = (1U << 8),
    WARNING_ID_ER00060_FILTER5_LIFE_70 = (1U << 9),
    WARNING_ID_ER00061_FILTER1_LIFE_90 = (1U << 10),
    WARNING_ID_ER00062_FILTER2_LIFE_90 = (1U << 11),
    WARNING_ID_ER00063_FILTER3_LIFE_90 = (1U << 12),
    WARNING_ID_ER00064_FILTER4_LIFE_90 = (1U << 13),
    WARNING_ID_ER00065_FILTER5_LIFE_90 = (1U << 14),
    WARNING_ID_ER00066_OUTPUT_WATER_QUALITY_WARNING = (1U << 15),
    WARNING_ID_ER00067_HEATER_SENSOR_FAULT = (1U << 16),
    WARNING_ID_ER00068_HEATER_RUNTIME_OVER_1H = (1U << 17),
    WARNING_ID_ER00069_COOLER_SENSOR_FAULT = (1U << 18),
    WARNING_ID_ER00070_COOLER_RUNTIME_OVER_2H = (1U << 19),
    WARNING_ID_ER00071 = (1U << 20),
    WARNING_ID_ER00072 = (1U << 21),
    WARNING_ID_ER00073 = (1U << 22),
    WARNING_ID_ER00074 = (1U << 23)
} WarningId;

void WarningManagerInit(void);
void WarningManagerClearAll(void);
void WarningManagerSet(WarningId warning_id);
void WarningManagerClear(WarningId warning_id);
bool WarningManagerIsActive(WarningId warning_id);
bool WarningManagerHasAnyWarning(void);
UI32 WarningManagerGetMask(void);
UI32 WarningManagerGetWarningCount(void);
void WarningManagerUpdate(WarningId warning_id, bool should_be_active);
const char *WarningManagerGetName(WarningId warning_id);

/* Override this weak hook to forward warning transitions to cloud/UI. */
void WarningManagerOnChanged(UI32 active_mask, UI32 changed_mask);
EventID WarningToEvent(WarningId Warning_id);

#endif

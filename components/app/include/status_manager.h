#ifndef STATUS_MANAGER_H
#define STATUS_MANAGER_H

#include <stdbool.h>
#include "common.h"

typedef struct {
    bool valid;
    UI8 ro_status;
    UI8 ply_status;
    UI8 hot_tank_status;
    UI8 cold_tank_status;
    UI8 setting_val;
    UI8 input_status;
    UI8 output_status;
    UI8 switch_status;
    UI8 led_status;
} DeviceStatusSnapshot_t;

void StatusManagerInit(void);
void StatusManagerRefresh(void);
bool StatusManagerHasWaterStatus(void);
DeviceStatusSnapshot_t StatusManagerGetSnapshot(void);
void StatusManagerCheckWarnings(void);

#endif

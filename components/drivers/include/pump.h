#ifndef PUMP_DRIVER_H
#define PUMP_DRIVER_H

#include "driver/gpio.h"
#include "common.h"

typedef enum{
    PUMP_ACTIVE_LOW = 0,
    PUMP_ACTIVE_HIGH = 1
}PumpActiveLevel;

void PumpInit(gpio_num_t pin, PumpActiveLevel level);
bool PumpIsActive(void);
UI32 PumpGetActiveTotalSeconds(void);
void PumpUpdateRuntime(UI32 now_ms);
void PumpStartRuntime(UI32 now_ms);
UI32 PumpGetElapsedTimeSeconds(UI32 now_ms);

#endif
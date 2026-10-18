#include "pump.h"

static gpio_num_t pump_pin = GPIO_NUM_NC;
static PumpActiveLevel pump_active_level = PUMP_ACTIVE_LOW;

static UI32 s_pump_active_start_ms = 0U;
static UI32 s_pump_active_total_s = 0U;

static UI8 s_pump_active = 0;

void PumpInit(gpio_num_t pin, PumpActiveLevel level)
{
    pump_pin = pin;
    pump_active_level = level;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

bool PumpIsActive(void)
{
    if(PUMP_ACTIVE_HIGH == pump_active_level)
    {
        return gpio_get_level(pump_pin) ? true : false; // VCC => active
    }else{
        return gpio_get_level(pump_pin) ? false : true; // GND => active
    }
}

UI32 PumpGetActiveTotalSeconds(void)
{
    return s_pump_active_total_s;
}

void PumpUpdateRuntime(UI32 now_ms)
{
    if (!s_pump_active)
    {
        return;
    }
    s_pump_active_total_s += (now_ms - s_pump_active_start_ms) / 1000U;
    s_pump_active_start_ms = now_ms;
    s_pump_active = 0;
}

void PumpStartRuntime(UI32 now_ms)
{
    if(s_pump_active)
    {
        return;
    }
    s_pump_active_start_ms = now_ms;
    s_pump_active = 1;
}

UI32 PumpGetElapsedTimeSeconds(UI32 now_ms)
{
    if(s_pump_active)
    {
        return (now_ms - s_pump_active_start_ms) / 1000U;
    }
    return 0;
}
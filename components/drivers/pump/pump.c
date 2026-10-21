#include "pump.h"

#include "esp_err.h"
#include "nvs.h"

#define PUMP_RUNTIME_NVS_NAMESPACE "pump_runtime"
#define PUMP_RUNTIME_NVS_KEY "active_total_s"

static gpio_num_t pump_pin = GPIO_NUM_NC;
static PumpActiveLevel pump_active_level = PUMP_ACTIVE_LOW;

static UI32 s_pump_active_start_ms = 0U;
static UI32 s_pump_active_total_s = 0U;

static UI8 s_pump_active = 0;

_bool PumpLoadRuntimeFromFlash(void);

void PumpInit(gpio_num_t pin, PumpActiveLevel level)
{
    PumpLoadRuntimeFromFlash();
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

_bool PumpIsActive(void)
{
    if(PUMP_ACTIVE_HIGH == pump_active_level)
    {
        return gpio_get_level(pump_pin) ? D_TRUE : D_FALSE; // VCC => active
    }else{
        return gpio_get_level(pump_pin) ? D_FALSE : D_TRUE; // GND => active
    }
}

UI32 PumpGetActiveTotalSeconds(void)
{
    return s_pump_active_total_s;
}

_bool PumpSaveRuntimeToFlash(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PUMP_RUNTIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return D_FALSE;
    }

    err = nvs_set_u32(handle, PUMP_RUNTIME_NVS_KEY, s_pump_active_total_s);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

_bool PumpLoadRuntimeFromFlash(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PUMP_RUNTIME_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            s_pump_active_total_s = 0U;
            return D_TRUE;
        }
        return D_FALSE;
    }

    uint32_t total_seconds = 0U;
    err = nvs_get_u32(handle, PUMP_RUNTIME_NVS_KEY, &total_seconds);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        s_pump_active_total_s = total_seconds;
        return D_TRUE;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        s_pump_active_total_s = 0U;
        return D_TRUE;
    }
    return D_FALSE;
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
    PumpSaveRuntimeToFlash();
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
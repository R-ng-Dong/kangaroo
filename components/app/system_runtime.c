#include "system_runtime.h"

#include <string.h>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SYSTEM_RUNTIME_NVS_NAMESPACE "system_runtime"
#define SYSTEM_RUNTIME_DEVICE_TOTAL_KEY "device_total"

static UI32 s_device_runtime_total_s = 0U;
static UI32 s_device_active_start_ms = 0U;

static UI32 SystemRuntimeCurrentSeconds(UI32 now_ms)
{
    return s_device_runtime_total_s + ((now_ms - s_device_active_start_ms) / 1000U);
}

bool SystemRuntimeSaveToFlash(void)
{
    // return true;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SYSTEM_RUNTIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return false;
    }

    err = nvs_set_u32(handle, SYSTEM_RUNTIME_DEVICE_TOTAL_KEY, s_device_runtime_total_s);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool SystemRuntimeLoadFromFlash(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SYSTEM_RUNTIME_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return false;
    }

    uint32_t value = 0U;
    err = nvs_get_u32(handle, SYSTEM_RUNTIME_DEVICE_TOTAL_KEY, &value);
    if (err == ESP_OK)
    {
        s_device_runtime_total_s = value;
    }
    else
    {
        s_device_runtime_total_s = 0U;
    }

    nvs_close(handle);
    return true;
}

void SystemRuntimeInit(void)
{

    s_device_runtime_total_s = 0U;
    s_device_active_start_ms = 0U;

    SystemRuntimeLoadFromFlash();
}

bool SystemRuntimeReconfig(UI32 time_s, UI32 now_ms)
{

    s_device_runtime_total_s = time_s;
    s_device_active_start_ms = now_ms;
    return SystemRuntimeSaveToFlash();
}

bool SystemRuntimeUpdate(UI32 now_ms)
{

    s_device_runtime_total_s += (now_ms - s_device_active_start_ms) / 1000U;
    s_device_active_start_ms = now_ms;
    SystemRuntimeSaveToFlash();
    return true;
}

UI32 SystemRuntimeGetSeconds(UI32 now_ms)
{
    return SystemRuntimeCurrentSeconds(now_ms); //s_device_runtime_total_s
}

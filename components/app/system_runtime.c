#include "system_runtime.h"
#include <string.h>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

#define MAX_NUM_FILTER 5

#define SYSTEM_RUNTIME_NVS_NAMESPACE "system_runtime"
#define SYSTEM_RUNTIME_DEVICE_TOTAL_KEY "device_total"

static UI32 s_device_runtime_total_s[MAX_NUM_FILTER + 1] = {0U};
static UI32 s_device_active_start_ms[MAX_NUM_FILTER + 1] = {0U};

static UI32 SystemRuntimeCurrentSeconds(UI8 filter_idx, UI32 now_ms)
{
    if(filter_idx > MAX_NUM_FILTER) return 0;
    return s_device_runtime_total_s[filter_idx] + ((now_ms - s_device_active_start_ms[filter_idx]) / 1000U);
}

_bool SystemRuntimeSaveToFlash(void)
{
    // return D_TRUE;
    printf("SystemRuntimeSaveToFlash === \n");
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SYSTEM_RUNTIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return D_FALSE;
    }

    err = nvs_set_blob(handle, SYSTEM_RUNTIME_DEVICE_TOTAL_KEY, s_device_runtime_total_s, sizeof(s_device_runtime_total_s));
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return D_FALSE;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

_bool SystemRuntimeLoadFromFlash(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SYSTEM_RUNTIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return D_FALSE;
    }

    uint32_t value[MAX_NUM_FILTER + 1];
    size_t value_size = sizeof(value);
    err = nvs_get_blob(handle, SYSTEM_RUNTIME_DEVICE_TOTAL_KEY, &value, &value_size);
    if (err == ESP_OK && value_size == sizeof(value))
    {
        printf("device_runtime_total_s: ");
        for (UI8 i = 0; i <= MAX_NUM_FILTER; i++)
        {
            s_device_runtime_total_s[i] = value[i];
            printf("%lu ", s_device_runtime_total_s[i]);
        }
        printf("\n");
    }
    else
    {
        for (UI8 i = 0; i <= MAX_NUM_FILTER; i++)
        {
            s_device_runtime_total_s[i] = 0;
        }
        err = nvs_set_blob(handle, SYSTEM_RUNTIME_DEVICE_TOTAL_KEY, s_device_runtime_total_s, sizeof(s_device_runtime_total_s));
        if (err == ESP_OK)
        {
            err = nvs_commit(handle);
        }
    }

    nvs_close(handle);
    return D_TRUE;
}

_bool SystemRuntimeInit(void)
{

    // s_device_runtime_total_s = 0U;
    // s_device_active_start_ms = 0U;
    printf("SystemRuntimeInit === \n");
    return SystemRuntimeLoadFromFlash();
}

_bool SystemRuntimeReconfig(UI8 filter_idx, UI32 time_s, UI32 now_ms)
{
    if(filter_idx > MAX_NUM_FILTER) return D_FALSE;
    s_device_runtime_total_s[filter_idx] = time_s;
    s_device_active_start_ms[filter_idx] = now_ms;
    return SystemRuntimeSaveToFlash();
}

_bool SystemRuntimeUpdate(UI8 filter_idx, UI32 now_ms)
{
    if(filter_idx > MAX_NUM_FILTER && filter_idx != SYS_UPDATE_ALL) return D_FALSE;
    if(filter_idx == SYS_UPDATE_ALL)
    {
        for (UI8 i = 0; i <= MAX_NUM_FILTER; i++)
        {
            s_device_runtime_total_s[i] += (now_ms - s_device_active_start_ms[i]) / 1000U;
            s_device_active_start_ms[i] = now_ms;
        }
    }
    else{
        s_device_runtime_total_s[filter_idx] += (now_ms - s_device_active_start_ms[filter_idx]) / 1000U;
        s_device_active_start_ms[filter_idx] = now_ms;
    }
    return SystemRuntimeSaveToFlash();
}

UI32 SystemRuntimeGetSeconds(UI8 filter_idx, UI32 now_ms)
{
    return SystemRuntimeCurrentSeconds(filter_idx, now_ms); //s_device_runtime_total_s
}

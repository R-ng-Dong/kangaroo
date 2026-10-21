#include "filter_runtime.h"

#include <string.h>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "water_controller.h"

#define FILTER_RUNTIME_NVS_NAMESPACE "filter_runtime"

static FilterRuntimeModel_t s_runtime_model = FILTER_RUNTIME_MODEL_UART;
static FilterRuntimeSnapshot_t s_snapshot = {0};

static UI32 s_local_runtime_total_s[FILTER_RUNTIME_FILTER_COUNT];
static UI32 s_local_runtime_start_ms[FILTER_RUNTIME_FILTER_COUNT];
static bool s_local_runtime_running[FILTER_RUNTIME_FILTER_COUNT];
static bool s_last_pump_active = false;

static const char *s_nvs_keys[FILTER_RUNTIME_FILTER_COUNT] = {
    "f1_total",
    "f2_total",
    "f3_total",
    "ro_total",
    "post_total"
};

static bool FilterRuntimePersistSnapshot(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_RUNTIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return false;
    }

    for (UI8 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
    {
        err = nvs_set_u32(handle, s_nvs_keys[i], s_local_runtime_total_s[i]);
        if (err != ESP_OK)
        {
            nvs_close(handle);
            return false;
        }
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool FilterRuntimeSaveToFlash(void)
{
    if (s_runtime_model != FILTER_RUNTIME_MODEL_LOCAL)
    {
        return false;
    }

    return FilterRuntimePersistSnapshot();
}

bool FilterRuntimeLoadFromFlash(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_RUNTIME_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        for (UI8 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
        {
            s_snapshot.valid[i] = true;
        }
        return false;
    }

    for (UI8 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
    {
        uint32_t value = 0;
        err = nvs_get_u32(handle, s_nvs_keys[i], &value);
        if (err == ESP_OK)
        {
            s_local_runtime_total_s[i] = value;
            s_snapshot.seconds[i] = value;
            s_snapshot.valid[i] = true;
        }
        else
        {
            s_local_runtime_total_s[i] = 0U;
            s_snapshot.seconds[i] = 0U;
            s_snapshot.valid[i] = true;
        }
    }

    nvs_close(handle);
    return true;
}

void FilterRuntimeInit(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(s_local_runtime_total_s, 0, sizeof(s_local_runtime_total_s));
    memset(s_local_runtime_start_ms, 0, sizeof(s_local_runtime_start_ms));
    memset(s_local_runtime_running, 0, sizeof(s_local_runtime_running));
    s_last_pump_active = false;

    if (s_runtime_model == FILTER_RUNTIME_MODEL_LOCAL)
    {
        FilterRuntimeLoadFromFlash();
    }
}

void FilterRuntimeSetModel(FilterRuntimeModel_t model)
{
    if ((model != FILTER_RUNTIME_MODEL_UART) && (model != FILTER_RUNTIME_MODEL_LOCAL))
    {
        model = FILTER_RUNTIME_MODEL_UART;
    }

    s_runtime_model = model;

    if (s_runtime_model == FILTER_RUNTIME_MODEL_LOCAL)
    {
        FilterRuntimeLoadFromFlash();
    }
}

FilterRuntimeModel_t FilterRuntimeGetModel(void)
{
    return s_runtime_model;
}

void FilterRuntimeReset(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(s_local_runtime_total_s, 0, sizeof(s_local_runtime_total_s));
    memset(s_local_runtime_start_ms, 0, sizeof(s_local_runtime_start_ms));
    memset(s_local_runtime_running, 0, sizeof(s_local_runtime_running));
    s_last_pump_active = false;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_RUNTIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        for (UI32 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
        {
            nvs_erase_key(handle, s_nvs_keys[i]);
        }
        nvs_commit(handle);
        nvs_close(handle);
    }
}

bool FilterRuntimeRefreshFromUart(void)
{
    for (UI32 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
    {
        s_snapshot.seconds[i] = WaterProfileGetFilterRuntimeSeconds((FilterTimeIndex)i);
        s_snapshot.valid[i] = true;
    }

    return true;
}

bool FilterRuntimeUpdateLocal(bool pump_active, UI32 now_ms)
{
    if (s_runtime_model != FILTER_RUNTIME_MODEL_LOCAL)
    {
        return false;
    }

    if (pump_active && !s_last_pump_active)
    {
        for (UI8 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
        {
            if (!s_local_runtime_running[i])
            {
                s_local_runtime_running[i] = true;
                s_local_runtime_start_ms[i] = now_ms;
                // s_local_runtime_total_s[i] += (now_ms - s_local_runtime_start_ms[i]) / 1000U;
                s_snapshot.valid[i] = true;
                break;
            }
        }
    }

    if (!pump_active && s_last_pump_active)
    {
        for (UI8 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
        {
            if (s_local_runtime_running[i])
            {
                s_local_runtime_total_s[i] += (now_ms - s_local_runtime_start_ms[i]) / 1000U;
                s_local_runtime_running[i] = false;
                s_snapshot.seconds[i] = s_local_runtime_total_s[i];
                s_snapshot.valid[i] = true;
            }
        }
        
        FilterRuntimeSaveToFlash();
    }

    s_last_pump_active = pump_active;
    return true;
}

bool FilterRuntimeGet(FilterTimeIndex filter_index, UI32 *seconds_out)
{
    if (seconds_out == NULL)
    {
        return false;
    }

    if (filter_index >= FILTER_RUNTIME_FILTER_COUNT)
    {
        return false;
    }

    if (s_runtime_model == FILTER_RUNTIME_MODEL_UART)
    {
        *seconds_out = s_snapshot.seconds[filter_index];
        return s_snapshot.valid[filter_index];
    }

    *seconds_out = s_local_runtime_total_s[filter_index];
    return s_snapshot.valid[filter_index];
}

void FilterRuntimeSet(FilterTimeIndex filter_index, UI32 seconds)
{
    if (filter_index >= FILTER_RUNTIME_FILTER_COUNT)
    {
        return;
    }

    s_snapshot.seconds[filter_index] = seconds;
    s_snapshot.valid[filter_index] = true;

    if (s_runtime_model == FILTER_RUNTIME_MODEL_LOCAL)
    {
        s_local_runtime_total_s[filter_index] = seconds;
        FilterRuntimeSaveToFlash();
    }else if (s_runtime_model == FILTER_RUNTIME_MODEL_UART)
    {
        WaterControllerSetFilterRuntimeMinutes(filter_index, seconds/60U);
    }
}

// get all filter time
bool FilterRuntimeGetSnapshot(UI32 now_ms, FilterRuntimeSnapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return false;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));

    if (s_runtime_model == FILTER_RUNTIME_MODEL_UART)
    {
        *out_snapshot = s_snapshot;
        return true;
    }

    for (UI32 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
    {
        UI32 total = s_local_runtime_total_s[i];
        if (s_local_runtime_running[i])
        {
            total += (now_ms - s_local_runtime_start_ms[i]) / 1000U;
        }

        out_snapshot->seconds[i] = total;
        out_snapshot->valid[i] = s_snapshot.valid[i] || s_local_runtime_running[i] || (total > 0U);
    }

    return true;
}

bool FilterRuntimeHasValue(FilterTimeIndex filter_index)
{
    if (filter_index >= FILTER_RUNTIME_FILTER_COUNT)
    {
        return false;
    }

    return s_snapshot.valid[filter_index];
}

bool FilterRuntimeHasAnyValue(void)
{
    for (UI32 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
    {
        if (s_snapshot.valid[i])
        {
            return true;
        }
    }

    return false;
}

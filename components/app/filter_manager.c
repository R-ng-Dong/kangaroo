#include "filter_manager.h"
#include <stdio.h>
#include <string.h>
#include "system_runtime.h"
#include "filter_runtime.h"
#include "water_runtime.h"
#include "filter_runtime.h"
#include "pump.h"
#include "warning_manager.h"
#include "nvs.h"
#include "esp_err.h"
#include "pin_config.h"

#define FILTER_LIFETIME_NVS_NAMESPACE "filter_config"

static const UI32 s_default_filter_lifetime_seconds[FILTER_RUNTIME_FILTER_COUNT] = {
    FIRST_FILTER_LIFETIME_HOURS * 3600U,
    SECOND_FILTER_LIFETIME_HOURS * 3600U,
    THIRD_FILTER_LIFETIME_HOURS * 3600U,
    RO_FILTER_LIFETIME_HOURS * 3600U,
    POST_FILTER_LIFETIME_HOURS * 3600U
};

static const UI64 s_filter_replacement_seconds[FILTER_RUNTIME_FILTER_COUNT] = {
    FIRST_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U,
    SECOND_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U,
    THIRD_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U,
    RO_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U,
    POST_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U
};

static UI32 s_last_report_ms = 0U;
static UI8 s_last_reported_percent[FILTER_RUNTIME_FILTER_COUNT] = {0U};

static const char *s_filter_lifetime_nvs_keys[FILTER_RUNTIME_FILTER_COUNT] = {
    "f1_life_s", "f2_life_s", "f3_life_s", "ro_life_s", "post_life_s"
};

static const char *s_filter_pid_nvs_keys[FILTER_RUNTIME_FILTER_COUNT] = {
    "f1_pid", "f2_pid", "f3_pid", "ro_pid", "post_pid"
};

static const UI8 s_default_filter_pids[FILTER_RUNTIME_FILTER_COUNT][FILTER_PID_SIZE] = {
    {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
     0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU},

    {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
     0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU},

    {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
     0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU},

    {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
     0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU},
     
    {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
     0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU}
};

static UI32 s_filter_lifetime_seconds[FILTER_RUNTIME_FILTER_COUNT];

static void FilterManagerLoadLifetimes(void)
{
    printf("Load life time\n");
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_LIFETIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        memcpy(s_filter_lifetime_seconds, s_default_filter_lifetime_seconds,
               sizeof(s_filter_lifetime_seconds));
        return;
    }

    _bool defaults_added = false;
    for (UI8 i = 0; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
    {
        UI32 seconds = 0U;
        err = nvs_get_u32(handle, s_filter_lifetime_nvs_keys[i], &seconds);
        if ((err == ESP_OK) && (seconds > 0U))
        {
            s_filter_lifetime_seconds[i] = seconds;
            printf("[Filter idx %d]: %lu\n", i, s_filter_lifetime_seconds[i]);
        }
        else
        {
            s_filter_lifetime_seconds[i] = s_default_filter_lifetime_seconds[i];
            if (nvs_set_u32(handle, s_filter_lifetime_nvs_keys[i], s_filter_lifetime_seconds[i]) == ESP_OK)
            {
                defaults_added = true;
            }
        }
    }
 
    if (defaults_added)
    {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

static void FilterManagerLoadDefaultPids(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_LIFETIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    _bool defaults_added = false;
    for (UI8 i = 0U; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
    {
        size_t pid_size = 0U;
        err = nvs_get_blob(handle, s_filter_pid_nvs_keys[i], NULL, &pid_size);
        if ((err == ESP_OK) && (pid_size == FILTER_PID_SIZE))
        {
            continue;
        }

        err = nvs_set_blob(handle, s_filter_pid_nvs_keys[i],
                           s_default_filter_pids[i], FILTER_PID_SIZE);
        if (err == ESP_OK)
        {
            defaults_added = true;
        }
    }

    if (defaults_added)
    {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

_bool FilterManagerSetLifetimeSeconds(FilterTimeIndex filter_index, UI32 seconds)
{
    if ((filter_index < FILTER_TIME_INDEX_1ST) ||
        (filter_index >= FILTER_RUNTIME_FILTER_COUNT) || (seconds == 0U))
    {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_LIFETIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return false;
    }

    err = nvs_set_u32(handle, s_filter_lifetime_nvs_keys[filter_index], seconds);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK)
    {
        return false;
    }

    s_filter_lifetime_seconds[filter_index] = seconds;
    printf("[filter_index : %d]: life time seconds: %lu\n", (UI8)filter_index, seconds);
    FilterManagerSetRuntime(filter_index, 0); //reset
    printf("config filter idx %d done\n", (UI8)filter_index);
    return true;
}

_bool FilterManagerGetLifetimeSeconds(FilterTimeIndex filter_index, UI32 *seconds_out)
{
    if ((filter_index < FILTER_TIME_INDEX_1ST) ||
        (filter_index >= FILTER_RUNTIME_FILTER_COUNT) || (seconds_out == NULL))
    {
        return false;
    }
    *seconds_out = s_filter_lifetime_seconds[filter_index];
    return true;
}

_bool FilterManagerSetPid(FilterTimeIndex filter_index, const UI8 pid[FILTER_PID_SIZE])
{
    if ((filter_index < FILTER_TIME_INDEX_1ST) ||
        (filter_index >= FILTER_RUNTIME_FILTER_COUNT) || (pid == NULL))
    {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_LIFETIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return false;
    }

    err = nvs_set_blob(handle, s_filter_pid_nvs_keys[filter_index], pid, FILTER_PID_SIZE);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

_bool FilterManagerGetPid(FilterTimeIndex filter_index, UI8 pid_out[FILTER_PID_SIZE])
{
    if ((filter_index < FILTER_TIME_INDEX_1ST) ||
        (filter_index >= FILTER_RUNTIME_FILTER_COUNT) || (pid_out == NULL))
    {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_LIFETIME_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return false;
    }

    size_t pid_size = FILTER_PID_SIZE;
    err = nvs_get_blob(handle, s_filter_pid_nvs_keys[filter_index], pid_out, &pid_size);
    nvs_close(handle);
    return (err == ESP_OK) && (pid_size == FILTER_PID_SIZE);
}

static void FilterManagerUpdateWarning(WarningId warning_id, _bool should_be_active)
{
    WarningManagerUpdate(warning_id, should_be_active);
}

static UI32 GetMaxSecondsByPump(FilterTimeIndex filter_index)
{
    if (FILTER_RUNTIME_FILTER_COUNT <= filter_index)
    {
        return 0U;
    }

    return s_filter_lifetime_seconds[filter_index];
}

static UI64 GetMaxSecondsBySystem(FilterTimeIndex filter_index)
{
    if (FILTER_RUNTIME_FILTER_COUNT <= filter_index)
    {
        return 0U;
    }

    return s_filter_replacement_seconds[filter_index];
}

static UI64 GetRemainingSeconds(UI32 used_seconds, UI64 max_seconds)
{
    if (used_seconds >= max_seconds)
    {
        return 0U;
    }

    return max_seconds - used_seconds;
}

static UI32 GetRemainingPercent(FilterTimeIndex filter_index, UI32 used_seconds, UI64 max_seconds)
{
    UI64 remaining_seconds = GetRemainingSeconds(used_seconds, max_seconds);
    if ((max_seconds == 0U) || (remaining_seconds == 0U))
    {
        return 0U;
    }

    return (remaining_seconds * 100U) / max_seconds;
}

static void FilterManagerGetLifetimePercent(UI32 now_ms, UI8 *percent_values){
    FilterRuntimeSnapshot_t snapshot = {0};

    if (!FilterRuntimeGetSnapshot(now_ms, &snapshot) || !percent_values)
    {
        return ;
    }
    for (UI8 idx = 0U; idx < FILTER_RUNTIME_FILTER_COUNT; ++idx)
    {
        if (!snapshot.valid[idx])
        {
            continue;
        }
        UI32 pump_runtime_seconds = snapshot.seconds[idx];
        UI32 max_pump_runtime_seconds = GetMaxSecondsByPump((FilterTimeIndex)idx);
        UI8 filter_lifetime_by_pump = GetRemainingPercent((FilterTimeIndex)idx, pump_runtime_seconds, max_pump_runtime_seconds);

        UI32 system_runtime_seconds = SystemRuntimeGetSeconds(now_ms);
        UI64 max_systemruntime_seconds = GetMaxSecondsBySystem((FilterTimeIndex)idx);
        UI8 filter_lifetime_by_system = GetRemainingPercent((FilterTimeIndex)idx, system_runtime_seconds, max_systemruntime_seconds);

        percent_values[idx] = (filter_lifetime_by_pump <= filter_lifetime_by_system) ? filter_lifetime_by_pump : filter_lifetime_by_system;
        if(percent_values[idx] >= 99) percent_values[idx] = 100;
        // printf("percent[%d]: %d-%d \n", idx, filter_lifetime_by_pump, filter_lifetime_by_system);
    }
}

// for test
__attribute__((weak)) void FilterManagerReportToCloud(const UI8 *percent_values, FilterManagerReportMode mode, FilterTimeIndex filter_index)
{
    (void)percent_values;
    (void)mode;
    (void)filter_index;
    printf("[Dummy] FilterManagerReportToCloud\n");
}

void FilterManagerInit(void)
{
    FilterManagerLoadLifetimes();
    FilterManagerLoadDefaultPids();

#if DEVICE_TYPE == WATER_PURIFIER_TYPE_STANDARD
    FilterRuntimeSetModel(FILTER_RUNTIME_MODEL_LOCAL);
    FilterRuntimeInit();
    PumpInit(PUMP_PIN, PUMP_ACTIVE_LOW);

#elif DEVICE_TYPE == WATER_PURIFIER_TYPE_HOT_COLD
    FilterRuntimeSetModel(FILTER_RUNTIME_MODEL_UART);
#endif

    s_last_report_ms = 0U;
    for (UI8 idx = 0U; idx < FILTER_RUNTIME_FILTER_COUNT; ++idx)
    {
        s_last_reported_percent[idx] = 0U;
    }
}

static _bool FilterManagerHasSignificantDecrease(const UI8 *percent_values)
{
    if (NULL == percent_values)
    {
        return false;
    }

    for (UI8 idx = 0U; idx < FILTER_RUNTIME_FILTER_COUNT; ++idx)
    {
        if ((s_last_reported_percent[idx] >= percent_values[idx]) &&
            ((s_last_reported_percent[idx] - percent_values[idx]) >= FILTER_MANAGER_REPORT_DECREASE_PERCENT))
        {
            return true;
        }
    }

    return false;
}

static void FilterManagerReportCurrentPercent(const UI8 *percent_values,
                                              FilterManagerReportMode mode, FilterTimeIndex filter_index)
{
    FilterManagerReportToCloud(percent_values, mode, filter_index);

    // Update s_last_reported_percent 
    if(mode == FILTER_MANAGER_REPORT_ONE_FILTER)
    {
        s_last_reported_percent[(UI8)filter_index] = percent_values[(UI8)filter_index];
    }
    else{
        for (UI8 idx = 0; idx < FILTER_RUNTIME_FILTER_COUNT; ++idx)
        {
            s_last_reported_percent[idx] = percent_values[idx];
        }
    }
}

_bool FilterManagerUpdateFromPump(UI32 now_ms)
{
    _bool pump_active = PumpIsActive();
    if (pump_active)
    {
        PumpStartRuntime(now_ms);
    }else
    {
        PumpUpdateRuntime(now_ms);
    }
    return FilterRuntimeUpdateLocal(pump_active, now_ms);
}

_bool FilterManagerShouldReport(UI32 now_ms)
{
    if (s_last_report_ms == 0U)
    {
        return true;
    }

    return (now_ms - s_last_report_ms) >= FILTER_MANAGER_REPORT_INTERVAL_MS;
}

void FilterManagerCheckWarning(UI32 now_ms, UI8 *remaining_percent)
{
    if(!remaining_percent) return;

    FilterRuntimeSnapshot_t snapshot = {0};
    static const WarningId warning_70[FILTER_RUNTIME_FILTER_COUNT] = {
        WARNING_ID_ER00056_FILTER1_LIFE_70, WARNING_ID_ER00057_FILTER2_LIFE_70,
        WARNING_ID_ER00058_FILTER3_LIFE_70, WARNING_ID_ER00059_FILTER4_LIFE_70,
        WARNING_ID_ER00060_FILTER5_LIFE_70
    };
    static const WarningId warning_90[FILTER_RUNTIME_FILTER_COUNT] = {
        WARNING_ID_ER00061_FILTER1_LIFE_90, WARNING_ID_ER00062_FILTER2_LIFE_90,
        WARNING_ID_ER00063_FILTER3_LIFE_90, WARNING_ID_ER00064_FILTER4_LIFE_90,
        WARNING_ID_ER00065_FILTER5_LIFE_90
    };

    if (!FilterRuntimeGetSnapshot(now_ms, &snapshot))
    {
        return;
    }

    /* LIFE_70/90 mean 70%/90% of the estimated lifetime has been used. */
    for (UI8 idx = 0U; idx < FILTER_RUNTIME_FILTER_COUNT; ++idx)
    {
        if (!snapshot.valid[idx])
        {
            continue;
        }

        FilterManagerUpdateWarning(warning_70[idx], remaining_percent[idx] <= 30U);
        FilterManagerUpdateWarning(warning_90[idx], remaining_percent[idx] <= 10U);
    }
}

void FilterManagerTick(UI32 now_ms)
{
#if DEVICE_TYPE == WATER_PURIFIER_TYPE_STANDARD
    _bool updated = FilterManagerUpdateFromPump(now_ms);
    if (!updated)
    {
        return ;
    }
#elif DEVICE_TYPE == WATER_PURIFIER_TYPE_HOT_COLD
    FilterRuntimeRefreshFromUart();
#endif

    UI8 remaining_percent[FILTER_RUNTIME_FILTER_COUNT] = {0U};
    FilterManagerGetLifetimePercent(now_ms, remaining_percent);

    FilterManagerCheckWarning(now_ms, remaining_percent);


    // Report immediately when any filter's remaining life drops by at least 5 points.
    _bool has_significant_decrease = FilterManagerHasSignificantDecrease(remaining_percent);
    if (has_significant_decrease)
    {
        for (UI8 idx = 0U; idx < FILTER_RUNTIME_FILTER_COUNT; ++idx)
        {
            if ((s_last_reported_percent[idx] >= remaining_percent[idx]) &&
                ((s_last_reported_percent[idx] - remaining_percent[idx]) >= FILTER_MANAGER_REPORT_DECREASE_PERCENT))
            {
                printf("filter %d report by trigger\n", idx);
                FilterManagerReportCurrentPercent(remaining_percent,
                                                 FILTER_MANAGER_REPORT_ONE_FILTER, (FilterTimeIndex)idx);
            }
        }
    }
    // Keep periodic feedback as a fallback.
    else if (FilterManagerShouldReport(now_ms))
    {
        printf("filter report by timeout\n");
        FilterManagerReportCurrentPercent(remaining_percent, FILTER_MANAGER_REPORT_ALL_FILTERS, (FilterTimeIndex)0);
        s_last_report_ms = now_ms;
    }
    return ;
}

UI32 FilterManagerGetWaterConsumption(void)
{
    UI32 pump_active_total_seconds = 0;
#if DEVICE_TYPE == WATER_PURIFIER_TYPE_STANDARD
    pump_active_total_seconds = PumpGetActiveTotalSeconds();
#elif DEVICE_TYPE == WATER_PURIFIER_TYPE_HOT_COLD
    if(WaterRuntimeHasValue())
    {
        DeviceRuntimeSnapshot_t snapshot = WaterRuntimeGetSnapshot();
        pump_active_total_seconds = snapshot.total_pump_runtime;
    }
#endif
    // UI32 pump_active_total_hours = pump_active_total_seconds/3600;

    return (pump_active_total_seconds * PUMP_FLOW_RATE_LPH * FILTER_EFFICIENCY) / (3600 * 100);
}

void FilterManagerSetRuntime(FilterTimeIndex filter_index, UI32 seconds)
{
    FilterRuntimeSet(filter_index, seconds);
}

#include "filter_manager.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "system_runtime.h"
#include "filter_runtime.h"
#include "water_runtime.h"
#include "pump.h"
#include "warning_manager.h"
#include "nvs.h"
#include "esp_err.h"
#include "pin_config.h"
#include "system_config.h"
#include "flush_cycle_state.h"

#include "system_config.h"

#define FILTER_LIFETIME_NVS_NAMESPACE "filter_config"
#define FILTER_CONFIG_NVS_KEY "filters_v1"
#define FILTER_CONFIG_HEADER 0x55aa55aa

typedef struct __attribute__((packed))
{
    UI8 pid[FILTER_PID_SIZE];
    UI32 lifetime_seconds;
    UI64 replacement_seconds;
} FilterConfig_t;

typedef struct __attribute__((packed))
{
    UI32 header;
    FilterConfig_t filters[FILTER_RUNTIME_FILTER_COUNT];
} FilterConfigStore_t;

static _bool is_filter_update_pending = D_FALSE;

static const UI32 s_default_filter_lifetime_seconds[FILTER_RUNTIME_FILTER_COUNT] = {
    FIRST_FILTER_LIFETIME_HOURS * 3600U,
    SECOND_FILTER_LIFETIME_HOURS * 3600U,
    THIRD_FILTER_LIFETIME_HOURS * 3600U,
    RO_FILTER_LIFETIME_HOURS * 3600U,
    POST_FILTER_LIFETIME_HOURS * 3600U
};

static const UI64 s_default_filter_replacement_seconds[FILTER_RUNTIME_FILTER_COUNT] = {
    FIRST_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U,
    SECOND_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U,
    THIRD_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U,
    RO_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U,
    POST_FILTER_REPLACEMENT_MONTH_THRESHOLD * 30U * 24U * 3600U
};

static UI32 s_last_report_ms = 0U;
static UI8 s_last_reported_percent[FILTER_RUNTIME_FILTER_COUNT] = {0U};

static const UI8 s_default_filter_pids[FILTER_RUNTIME_FILTER_COUNT][FILTER_PID_SIZE] = {
    {0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x30, 0x31, 0x43, 0x34, 0x30, 0x34, 0x30, 0x30, 0x30, 0x31},
    {0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x30, 0x31, 0x43, 0x34, 0x30, 0x34, 0x30, 0x30, 0x30, 0x33},
    {0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x30, 0x31, 0x43, 0x34, 0x30, 0x34, 0x30, 0x30, 0x30, 0x34},
    {0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x30, 0x31, 0x43, 0x34, 0x30, 0x34, 0x30, 0x30, 0x30, 0x35},
    {0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x30, 0x31, 0x43, 0x34, 0x30, 0x34, 0x30, 0x30, 0x30, 0x36}
};

static FilterConfigStore_t s_filter_config;

static void FilterManagerSetDefaultConfig(void)
{
    s_filter_config.header = FILTER_CONFIG_HEADER;
    for (UI8 i = 0U; i < FILTER_RUNTIME_FILTER_COUNT; ++i)
    {
        memcpy(s_filter_config.filters[i].pid, s_default_filter_pids[i], FILTER_PID_SIZE);
        s_filter_config.filters[i].lifetime_seconds = s_default_filter_lifetime_seconds[i];
        s_filter_config.filters[i].replacement_seconds = s_default_filter_replacement_seconds[i];
    }
}

static _bool FilterManagerSaveConfig(nvs_handle_t handle)
{
    esp_err_t err = nvs_set_blob(handle, FILTER_CONFIG_NVS_KEY, &s_filter_config, sizeof(s_filter_config));
    if (err == ESP_OK) err = nvs_commit(handle);
    return (err == ESP_OK) ? D_TRUE : D_FALSE;
}

void FilterUpdateSetPending(_bool pending)
{
    is_filter_update_pending = pending;
}

static void FilterManagerLoadLifetimes(void)
{
    FilterManagerSetDefaultConfig();
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_LIFETIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    size_t stored_size = sizeof(s_filter_config);
    err = nvs_get_blob(handle, FILTER_CONFIG_NVS_KEY, &s_filter_config, &stored_size);
    if ((err == ESP_OK) && (stored_size == sizeof(s_filter_config)) &&
        (s_filter_config.header == FILTER_CONFIG_HEADER))
    {
        nvs_close(handle);
        return;
    }

    /* Missing or invalid blob: persist all five filters' defaults under the same key. */
    (void)FilterManagerSaveConfig(handle);
    nvs_close(handle);
}

_bool FilterManagerSetLifetimeSeconds(FilterTimeIndex filter_index, UI32 expireTime)
{
    if ((filter_index < FILTER_TIME_INDEX_1ST) ||
        (filter_index >= FILTER_RUNTIME_FILTER_COUNT) || (expireTime == 0U))
    {
        return D_FALSE;
    }

    UI16 lifeTimeHour = (UI16)(expireTime >> 16);
    UI16 replacementTimeMonth = (UI16)expireTime; 

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_LIFETIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return D_FALSE;
    }

    UI32 previousLifetime = s_filter_config.filters[filter_index].lifetime_seconds;
    UI64 previousReplacementTime = s_filter_config.filters[filter_index].replacement_seconds;

    s_filter_config.filters[filter_index].lifetime_seconds = lifeTimeHour * 3600U;
    s_filter_config.filters[filter_index].replacement_seconds = replacementTimeMonth * 30U * 24U * 3600U;

    const _bool saved = FilterManagerSaveConfig(handle);
    if (!saved)
    {
        s_filter_config.filters[filter_index].replacement_seconds = previousReplacementTime;
        s_filter_config.filters[filter_index].lifetime_seconds = previousLifetime;
    } 

    nvs_close(handle);
    if (!saved)
    {
        return D_FALSE;
    }

    printf("[filter_index : %d]: lifeTimeHour: %u (h), replacementTimeMonth: %u (m)\n", (UI8)filter_index, lifeTimeHour, replacementTimeMonth);
    FilterManagerSetRuntime(filter_index, 0); //reset
    return D_TRUE;
}

_bool FilterManagerGetLifetimeSeconds(FilterTimeIndex filter_index, UI32 *seconds_out)
{
    if ((filter_index < FILTER_TIME_INDEX_1ST) ||
        (filter_index >= FILTER_RUNTIME_FILTER_COUNT) || (seconds_out == NULL))
    {
        return D_FALSE;
    }
    *seconds_out = s_filter_config.filters[filter_index].lifetime_seconds;
    return D_TRUE;
}

_bool FilterManagerGetReplacementSeconds(FilterTimeIndex filter_index, UI64 *seconds_out)
{
    if ((filter_index < FILTER_TIME_INDEX_1ST) ||
        (filter_index >= FILTER_RUNTIME_FILTER_COUNT) || (seconds_out == NULL))
    {
        return D_FALSE;
    }
    *seconds_out = s_filter_config.filters[filter_index].replacement_seconds;
    return D_TRUE;
}

_bool FilterManagerSetPid(FilterTimeIndex filter_index, const UI8 pid[FILTER_PID_SIZE])
{
    if ((filter_index < FILTER_TIME_INDEX_1ST) ||
        (filter_index >= FILTER_RUNTIME_FILTER_COUNT) || (pid == NULL))
    {
        return D_FALSE;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(FILTER_LIFETIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return D_FALSE;
    }

    UI8 previous[FILTER_PID_SIZE];
    memcpy(previous, s_filter_config.filters[filter_index].pid, FILTER_PID_SIZE);
    memcpy(s_filter_config.filters[filter_index].pid, pid, FILTER_PID_SIZE);
    const _bool saved = FilterManagerSaveConfig(handle);
    if (!saved) memcpy(s_filter_config.filters[filter_index].pid, previous, FILTER_PID_SIZE);
    nvs_close(handle);
    return saved;
}

_bool FilterManagerGetPid(FilterTimeIndex filter_index, UI8 pid_out[FILTER_PID_SIZE])
{
    if ((filter_index < FILTER_TIME_INDEX_1ST) ||
        (filter_index >= FILTER_RUNTIME_FILTER_COUNT) || (pid_out == NULL))
    {
        return D_FALSE;
    }

    memcpy(pid_out, s_filter_config.filters[filter_index].pid, FILTER_PID_SIZE);
    return D_TRUE;
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

    return s_filter_config.filters[filter_index].lifetime_seconds;
}

static UI64 GetMaxSecondsBySystem(FilterTimeIndex filter_index)
{
    if (FILTER_RUNTIME_FILTER_COUNT <= filter_index)
    {
        return 0U;
    }

    return s_filter_config.filters[filter_index].replacement_seconds;
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
    float percent = ((float)remaining_seconds / (float)max_seconds) * 100.0f;

    return (UI32)(percent + 0.5f);
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

        UI32 system_runtime_seconds = SystemRuntimeGetSeconds(idx, now_ms);
        UI64 max_systemruntime_seconds = GetMaxSecondsBySystem((FilterTimeIndex)idx);
        UI8 filter_lifetime_by_system = GetRemainingPercent((FilterTimeIndex)idx, system_runtime_seconds, max_systemruntime_seconds);


        percent_values[idx] = (filter_lifetime_by_pump <= filter_lifetime_by_system) ? filter_lifetime_by_pump : filter_lifetime_by_system;
  
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
    s_last_report_ms = 0U;
    ESP_LOGW("FILTER", "================== FILTER MANAGER ==================\n");
    for (UI8 idx = 0U; idx < FILTER_RUNTIME_FILTER_COUNT; ++idx)
    {
        s_last_reported_percent[idx] = 0U;
        ESP_LOGI("FILTER", "[Filter idx %d] | life time: %lu (s), replacement time: %llu (s)", idx, s_filter_config.filters[idx].lifetime_seconds, s_filter_config.filters[idx].replacement_seconds);
        ESP_LOG_BUFFER_HEX("FILTER", s_filter_config.filters[idx].pid, FILTER_PID_SIZE);
    }
}

void FilterManagerSetModel(WaterPurifierType model)
{
    if(WATER_PURIFIER_TYPE_STANDARD == model)
    {
        FilterRuntimeSetModel(FILTER_RUNTIME_MODEL_LOCAL);
        PumpInit(PUMP_PIN, PUMP_ACTIVE_LOW);
    }
    else if(WATER_PURIFIER_TYPE_HOT_COLD == model)
    {
        FilterRuntimeSetModel(FILTER_RUNTIME_MODEL_UART);
    }
}


static _bool FilterManagerHasSignificantDecrease(const UI8 *percent_values)
{
    if (NULL == percent_values)
    {
        return D_FALSE;
    }
    const UI32 FILTER_MANAGER_REPORT_DECREASE_PERCENT = SystemConfigGetStateFilterStepPct();
    for (UI8 idx = 0U; idx < FILTER_RUNTIME_FILTER_COUNT; ++idx)
    {
        if ((s_last_reported_percent[idx] >= percent_values[idx]) &&
            ((s_last_reported_percent[idx] - percent_values[idx]) >= FILTER_MANAGER_REPORT_DECREASE_PERCENT))
        {
            return D_TRUE;
        }

        if ((s_last_reported_percent[idx] <= percent_values[idx]) &&
            ((percent_values[idx] - s_last_reported_percent[idx]) >= FILTER_MANAGER_REPORT_DECREASE_PERCENT))
        {
            return D_TRUE;
        }
    }

    return D_FALSE;
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
    static UI8 scan = 0;
    static _bool is_active = D_FALSE;
    _bool pump_active = PumpIsActive();

    if (pump_active)
    {
        scan++;
        if(scan == 5)
        {
            if(is_active) 
                return D_TRUE;
            
            is_active = D_TRUE;
            PumpStartRuntime(now_ms);
            return FilterRuntimeUpdateLocal(D_TRUE, now_ms);
        }
    }else
    {
        scan = 0;
        if(is_active == D_TRUE)
        {
            PumpUpdateRuntime(now_ms);
            is_active = D_FALSE;
            return FilterRuntimeUpdateLocal(D_FALSE, now_ms);
        }
        is_active = D_FALSE;
    }
    return D_TRUE;
}

_bool FilterManagerShouldReport(UI32 now_ms)
{
    if (s_last_report_ms == 0U)
    {
        return D_TRUE;
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

void FilterManagerTick(UI32 now_ms, WaterPurifierType model)
{
    static _bool first_time = D_TRUE;
    if(is_filter_update_pending)
    {
        return;
    }

    if(WATER_PURIFIER_TYPE_STANDARD == model)
    {
        _bool updated = FilterManagerUpdateFromPump(now_ms);
        if (!updated)
        {
            return ;
        }
    }
    else if(WATER_PURIFIER_TYPE_HOT_COLD == model)
    {
        FilterRuntimeRefreshFromUart();
    }

    static UI8 remaining_percent[FILTER_RUNTIME_FILTER_COUNT] = {0U};
    FilterManagerGetLifetimePercent(now_ms, remaining_percent);

    FilterManagerCheckWarning(now_ms, remaining_percent);
    
    if(first_time)
    {
        FilterManagerReportCurrentPercent(remaining_percent, FILTER_MANAGER_REPORT_ALL_FILTERS, (FilterTimeIndex)0);
        first_time = D_FALSE;
        return;
    }
    // printf("remaining: %d %d %d %d %d\n", remaining_percent[0], remaining_percent[1], remaining_percent[2], remaining_percent[3], remaining_percent[4]);
    // Report immediately when any filter's remaining life drops by at least 5 points.
    _bool has_significant_decrease = FilterManagerHasSignificantDecrease(remaining_percent);
    if (has_significant_decrease)
    {
        FilterManagerReportCurrentPercent(remaining_percent, FILTER_MANAGER_REPORT_ALL_FILTERS, (FilterTimeIndex)0);
#if 0
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
#endif

    }

#if 0
    // Keep periodic feedback as a fallback.
    else if (FilterManagerShouldReport(now_ms))
    {
        printf("filter report by timeout\n");
        FilterManagerReportCurrentPercent(remaining_percent, FILTER_MANAGER_REPORT_ALL_FILTERS, (FilterTimeIndex)0);
        s_last_report_ms = now_ms;
    }
#endif

    return ;
}

UI32 FilterManagerGetWaterConsumption(WaterPurifierType model)
{
    UI32 pump_active_total_seconds = 0;
    if(WATER_PURIFIER_TYPE_STANDARD == model)
    {
        pump_active_total_seconds = PumpGetActiveTotalSeconds(esp_timer_get_time() / 1000U);
    }
    else if(WATER_PURIFIER_TYPE_HOT_COLD == model)
    {
        WaterRuntimeRefresh();
        if(WaterRuntimeHasValue())
        {
            DeviceRuntimeSnapshot_t snapshot = WaterRuntimeGetSnapshot();
            pump_active_total_seconds = snapshot.total_pump_runtime;
        }
    }

    UI64 water_consumption_ws = (UI64)pump_active_total_seconds * PUMP_FLOW_RATE_LPH * FILTER_EFFICIENCY;
    UI64 water_consumption = water_consumption_ws / (3600 * 100);


    FlushCycleState_t state_out;
    if(!FlushCycleStateGet(&state_out))
    {
        return 0;
    }

    UI32 total_flush_water = state_out.total_drain_water_filter1 + state_out.total_drain_water_filter2\
                            + state_out.total_drain_water_filter3 + state_out.total_drain_water_ro\
                            + state_out.total_drain_water_post;
    UI32 total_removed_water = WATER_TANK_CAPACITY_L + INITIAL_WASTE_WATER_VOLUME_L + total_flush_water;
    printf("pump_active_total_seconds: %lu, total_removed_water: %lu, water_consumption: %llu\n", pump_active_total_seconds, total_removed_water, water_consumption);
    UI32 calculated_consumption = ((UI32)water_consumption <= total_removed_water)
                                      ? 0U
                                      : (UI32)water_consumption - total_removed_water;
    return (calculated_consumption < state_out.water_consumption_floor_l)
               ? state_out.water_consumption_floor_l
               : calculated_consumption;
}

void FilterManagerSetRuntime(FilterTimeIndex filter_index, UI32 seconds)
{
    FilterRuntimeSet(filter_index, seconds);
}

_bool FilterManagerCheckFirstChanged(FilterTimeIndex filter_index, UI8 pid[FILTER_PID_SIZE])
{
    if(filter_index > FILTER_TIME_INDEX_POST) return D_FALSE;
    _bool is_default = (memcmp(s_filter_config.filters[filter_index].pid, pid, FILTER_PID_SIZE) == 0) ? D_TRUE : D_FALSE;
    return is_default ? D_FALSE : D_TRUE;
}

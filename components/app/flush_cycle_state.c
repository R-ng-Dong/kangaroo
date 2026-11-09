#include "flush_cycle_state.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#define FLUSH_CYCLE_NVS_NAMESPACE "flush_cycle"
#define FLUSH_CYCLE_NVS_KEY "state"
#define FLUSH_CYCLE_STATE_VERSION 1U

typedef struct
{
    UI8 version;
    FlushCycleState_t state;
} FlushCycleStoredState_t;

static const char *TAG = "FLUSH_CYCLE";
static FlushCycleState_t s_state;
static bool s_initialized;

static _bool FlushCycleStateSave(void)
{
    FlushCycleStoredState_t stored = {
        .version = FLUSH_CYCLE_STATE_VERSION,
        .state = s_state,
    };
    nvs_handle_t handle;
    esp_err_t err = nvs_open(FLUSH_CYCLE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return D_FALSE;
    }

    err = nvs_set_blob(handle, FLUSH_CYCLE_NVS_KEY, &stored, sizeof(stored));
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save state: %s", esp_err_to_name(err));
        return D_FALSE;
    }
    return D_TRUE;
}

void FlushCycleStateInit(void)
{
    if (s_initialized)
    {
        return;
    }

    FlushCycleStoredState_t stored = {0};
    size_t stored_size = sizeof(stored);
    nvs_handle_t handle;

    s_initialized = true;
    esp_err_t err = nvs_open(FLUSH_CYCLE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK)
    {
        err = nvs_get_blob(handle, FLUSH_CYCLE_NVS_KEY, &stored, &stored_size);
        nvs_close(handle);
    }

    if (err == ESP_OK && stored_size == sizeof(stored) &&
        stored.version == FLUSH_CYCLE_STATE_VERSION )
    {
        s_state = stored.state;
        return;
    }

    s_state = (FlushCycleState_t){
        .drain_time_s = 30U * 60U,
        .post_filter_flush_time_s = 0,
        .total_drain_water_filter1 = 0,
        .total_drain_water_filter2 = 0,
        .total_drain_water_filter3 = 0,
        .total_drain_water_ro = 0,
        .total_drain_water_post = 0,
        .drain_done = 0,
        .post_filter_flush_done = 1,
    };

    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK)
    {
        ESP_LOGW(TAG, "No valid saved state, using defaults: %s", esp_err_to_name(err));
    }
    (void)FlushCycleStateSave();
}

static void FlushCycleStateEnsureInitialized(void)
{
    if (!s_initialized)
    {
        FlushCycleStateInit();
    }
}

_bool FlushCycleStateGet(FlushCycleState_t *state_out)
{
    if (state_out == NULL)
    {
        return D_FALSE;
    }
    FlushCycleStateEnsureInitialized();
    *state_out = s_state;
    return D_TRUE;
}

static _bool FlushCycleStateUpdate(const FlushCycleState_t *new_state)
{
    FlushCycleStateEnsureInitialized();
    FlushCycleState_t old_state = s_state;
    s_state = *new_state;
    if (FlushCycleStateSave() != D_TRUE)
    {
        s_state = old_state;
        return D_FALSE;
    }
    return D_TRUE;
}

_bool FlushCycleStateSetDrainTime(UI32 time_s)
{
    FlushCycleStateEnsureInitialized();
    FlushCycleState_t updated = s_state;
    updated.drain_time_s = time_s;
    updated.drain_done = (time_s > 0) ? 0 : 1;
    return FlushCycleStateUpdate(&updated);
}

_bool FlushCycleStateSetPostFilterFlushTime(UI32 time_s)
{
    FlushCycleStateEnsureInitialized();
    FlushCycleState_t updated = s_state;
    updated.post_filter_flush_time_s = time_s;
    updated.post_filter_flush_done = (time_s > 0) ? 0 : 1;
    return FlushCycleStateUpdate(&updated);
}

_bool FlushCycleStateSetTotalDrainWater(UI8 filter_idx)
{
    if(filter_idx >= 5) return D_FALSE;

    FlushCycleStateEnsureInitialized();
    FlushCycleState_t updated = s_state;
    switch (filter_idx)
    {
    case 0:
        updated.total_drain_water_filter1 += 15;
        break;
    case 1:
        updated.total_drain_water_filter2 += 15;
        break;
    case 2:
        updated.total_drain_water_filter3 += 15;
        break;
    case 3:
        updated.total_drain_water_ro += 20;
        break;
    case 4:
        updated.total_drain_water_post += 25;
        break;
    
    default:
        break;
    }
    return FlushCycleStateUpdate(&updated);
}


_bool FlushCycleStateSetDrainDone(_bool done)
{
    FlushCycleStateEnsureInitialized();
    FlushCycleState_t updated = s_state;
    updated.drain_done = done ? 1U : 0U;
    return FlushCycleStateUpdate(&updated);
}

_bool FlushCycleStateSetPostFilterFlushDone(_bool done)
{
    FlushCycleStateEnsureInitialized();
    FlushCycleState_t updated = s_state;
    updated.post_filter_flush_done = done ? 1U : 0U;
    return FlushCycleStateUpdate(&updated);
}

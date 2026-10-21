#include "energy_saving_manager.h"

#include "water_controller.h"
#include "esp_err.h"
#include "nvs.h"

#include <string.h>

#define ENERGY_SAVING_NVS_NAMESPACE "energy_saving"
#define NVS_KEY_CONFIG "config"


#define ENERGY_SAVING_CONFIG_HEADER 0x55AA55AA

typedef struct __attribute__((packed))
{
    UI32 header;
    UI8 schedule_enabled;
    UI16 start_minute;
    UI16 end_minute;
    UI8 hot_temperature;
    UI8 cold_temperature;
} EnergySavingConfig;

typedef struct
{
    UI8 enabled;
    UI8 temperature;
    UI8 base_enabled;
    UI8 override_active;
    UI64 override_until;
    UI8 counting_saved_time;
    UI64 saved_seconds;
} TankPolicy;

static struct
{
    UI8 schedule_enabled;
    UI16 start_minute;
    UI16 end_minute;
    UI16 last_minute;
    _bool has_last_minute;
    _bool has_last_update;
    UI64 last_update_seconds;
    TankPolicy tanks[2];
} s_policy;

// for test
__attribute__((weak)) void AppResponseTankStateToCloud(EnergySavingTank_t tank, UI8 enable)
{
    (void)tank;
    (void)enable;
    printf("[Dummy] AppResponseTankStateToCloud\n");
}

static EnergySavingConfig GetConfig(void)
{
    EnergySavingConfig config = {
        .header = ENERGY_SAVING_CONFIG_HEADER,
        .schedule_enabled = s_policy.schedule_enabled,
        .start_minute = s_policy.start_minute,
        .end_minute = s_policy.end_minute,
        .hot_temperature = s_policy.tanks[ENERGY_SAVING_TANK_HOT].temperature,
        .cold_temperature = s_policy.tanks[ENERGY_SAVING_TANK_COLD].temperature,
    };
    return config;
}

static _bool IsConfigValid(const EnergySavingConfig *config)
{
    return (config->header == ENERGY_SAVING_CONFIG_HEADER &&
            config->schedule_enabled <= 1U &&
            config->start_minute < MINUTES_PER_DAY &&
            config->end_minute < MINUTES_PER_DAY &&
            config->hot_temperature >= MIN_TEMPERATURE_HOT_TANK &&
            config->hot_temperature <= MAX_TEMPERATURE_HOT_TANK &&
            config->cold_temperature >= MIN_TEMPERATURE_COLD_TANK &&
            config->cold_temperature <= MAX_TEMPERATURE_COLD_TANK)
               ? D_TRUE : D_FALSE;
}

static void ApplyConfig(const EnergySavingConfig *config)
{
    s_policy.schedule_enabled = config->schedule_enabled ? D_TRUE : D_FALSE;
    s_policy.start_minute = config->start_minute;
    s_policy.end_minute = config->end_minute;
    s_policy.tanks[ENERGY_SAVING_TANK_HOT].temperature = config->hot_temperature;
    s_policy.tanks[ENERGY_SAVING_TANK_COLD].temperature = config->cold_temperature;
}

static _bool SaveConfig(const EnergySavingConfig *config)
{
    nvs_handle_t handle;
    if (nvs_open(ENERGY_SAVING_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return D_FALSE;

    esp_err_t err = nvs_set_blob(handle, NVS_KEY_CONFIG, config, sizeof(*config));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return (err == ESP_OK) ? D_TRUE : D_FALSE;
}

static void LoadPersistentSettings(void)
{
    nvs_handle_t handle;
    if (nvs_open(ENERGY_SAVING_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return;

    EnergySavingConfig config = GetConfig();
    size_t config_size = sizeof(config);
    esp_err_t err = nvs_get_blob(handle, NVS_KEY_CONFIG, &config, &config_size);
    if (err == ESP_OK && config_size == sizeof(config) && IsConfigValid(&config))
    {
        ApplyConfig(&config);
    }
    nvs_close(handle);
}

static _bool IsScheduleActive(UI16 minute_of_day)
{
    if (!s_policy.schedule_enabled || s_policy.start_minute == s_policy.end_minute)
    {
        return D_FALSE;
    }

    if (s_policy.start_minute < s_policy.end_minute)
    {
        return (minute_of_day >= s_policy.start_minute && minute_of_day < s_policy.end_minute)
                   ? D_TRUE : D_FALSE;
    }

    return (minute_of_day >= s_policy.start_minute || minute_of_day < s_policy.end_minute)
               ? D_TRUE : D_FALSE;
}

static _bool ApplyTank(EnergySavingTank_t tank, UI8 enabled, UI8 temperature)
{
    _bool ret = D_FALSE;
    if (tank == ENERGY_SAVING_TANK_HOT)
    {
        ret = WaterControllerSetFeatureHot(enabled, temperature);
    }
    else
    {
        ret = WaterControllerSetFeatureCold(enabled, temperature);
    }
    if(ret != D_TRUE) 
        return D_FALSE;
    AppResponseTankStateToCloud(tank, enabled);
    return D_TRUE;
}

static void AccountSavedTimeUntil(UI64 now_seconds)
{
    if (!s_policy.has_last_update)
    {
        s_policy.last_update_seconds = now_seconds;
        s_policy.has_last_update = D_TRUE;
        return;
    }

    if (now_seconds >= s_policy.last_update_seconds)
    {
        const UI64 elapsed = now_seconds - s_policy.last_update_seconds;
        for (UI8 i = 0U; i < 2U; ++i)
        {
            if (s_policy.tanks[i].counting_saved_time)
            {
                s_policy.tanks[i].saved_seconds += elapsed;
            }
        }
    }
    s_policy.last_update_seconds = now_seconds;
}

void EnergySavingManagerInit(void)
{
    memset(&s_policy, 0, sizeof(s_policy));
    s_policy.tanks[ENERGY_SAVING_TANK_HOT].temperature = MAX_TEMPERATURE_HOT_TANK; // default
    s_policy.tanks[ENERGY_SAVING_TANK_COLD].temperature = MIN_TEMPERATURE_COLD_TANK; // default
    s_policy.tanks[ENERGY_SAVING_TANK_HOT].enabled = D_TRUE;
    s_policy.tanks[ENERGY_SAVING_TANK_COLD].enabled = D_TRUE;
    s_policy.tanks[ENERGY_SAVING_TANK_HOT].base_enabled = D_TRUE;
    s_policy.tanks[ENERGY_SAVING_TANK_COLD].base_enabled = D_TRUE;
    LoadPersistentSettings();
}

_bool EnergySavingManagerSetSchedule(_bool enabled, UI16 start_minute, UI16 end_minute)
{
    if (start_minute > 1440 || end_minute > 1440)
    {
        return D_FALSE;
    }

    EnergySavingConfig config = GetConfig();
    config.schedule_enabled = enabled ? 1U : 0U;
    config.start_minute = start_minute;
    config.end_minute = end_minute;
    if (!SaveConfig(&config))
    {
        return D_FALSE;
    }
    printf("enable: %d, start_minute: %u s, end_minute: %u s\n", enabled, start_minute, end_minute);
    ApplyConfig(&config);
    s_policy.has_last_minute = D_FALSE;
    s_policy.has_last_update = D_FALSE;
    return D_TRUE;
}

_bool EnergySavingManagerSetTank(EnergySavingTank_t tank, UI8 enabled, UI8 temperature,
                                 UI64 now_seconds)
{
    if (tank > ENERGY_SAVING_TANK_COLD)
    {
        return D_FALSE;
    }

    AccountSavedTimeUntil(now_seconds);
    TankPolicy *policy = &s_policy.tanks[tank];
    if (temperature == 0xFFU)
    {
        temperature = policy->temperature;
    }
    else
    {
        if (tank == ENERGY_SAVING_TANK_HOT)
        {
            if (temperature < MIN_TEMPERATURE_HOT_TANK) temperature = MIN_TEMPERATURE_HOT_TANK;
            if (temperature > MAX_TEMPERATURE_HOT_TANK) temperature = MAX_TEMPERATURE_HOT_TANK;
        }
        else
        {
            if (temperature < MIN_TEMPERATURE_COLD_TANK) temperature = MIN_TEMPERATURE_COLD_TANK;
            if (temperature > MAX_TEMPERATURE_COLD_TANK) temperature = MAX_TEMPERATURE_COLD_TANK;
        }
        EnergySavingConfig config = GetConfig();
        if (tank == ENERGY_SAVING_TANK_HOT)
            config.hot_temperature = temperature;
        else
            config.cold_temperature = temperature;
        if (!SaveConfig(&config))
        {
            return D_FALSE;
        }
    }
    policy->temperature = temperature;
    policy->base_enabled = enabled ? D_TRUE : D_FALSE;

    if (enabled && IsScheduleActive(s_policy.has_last_minute ? s_policy.last_minute : 0U))
    {
        policy->override_active = D_TRUE;
        policy->override_until = now_seconds + ENERGY_SAVING_MANUAL_OVERRIDE_SECONDS;
        if (ApplyTank(tank, D_TRUE, policy->temperature))
        {
            policy->enabled = D_TRUE;
            return D_TRUE;
        }
        return D_FALSE;
    }
    else
    {
        policy->override_active = D_FALSE;
        if (ApplyTank(tank, enabled ? D_TRUE : D_FALSE, policy->temperature))
        {
            policy->enabled = enabled ? D_TRUE : D_FALSE;
            return D_TRUE;
        }
        return D_FALSE;
    }
}

void EnergySavingManagerUpdate(UI16 minute_of_day, UI64 now_seconds)
{
    if (minute_of_day >= MINUTES_PER_DAY)
    {
        return;
    }

    const _bool schedule_active = IsScheduleActive(minute_of_day);
    AccountSavedTimeUntil(now_seconds);

    s_policy.last_minute = minute_of_day;
    s_policy.has_last_minute = D_TRUE;

    for (UI8 i = 0U; i < 2U; ++i)
    {
        TankPolicy *tank = &s_policy.tanks[i];
        UI8 desired_enabled;

        if (tank->override_active)
        {
            if (!schedule_active || now_seconds >= tank->override_until)
            {
                tank->override_active = D_FALSE;
            }
        }

        if (tank->override_active)
        {
            desired_enabled = D_TRUE;
        }
        else if (schedule_active)
        {
            desired_enabled = D_FALSE;
        }
        else
        {
            desired_enabled = tank->base_enabled; // On
        }

        if (desired_enabled != tank->enabled)
        {
            if (ApplyTank((EnergySavingTank_t)i, desired_enabled, tank->temperature))
            {
                tank->enabled = desired_enabled;
            }
        }

        tank->counting_saved_time = (schedule_active && !tank->enabled) ? D_TRUE : D_FALSE;
    }
}

void EnergySavingManagerGetSavedEnergy(UI64 now_seconds, UI64 *hot_tank_wh, UI64 *cold_tank_wh)
{
    AccountSavedTimeUntil(now_seconds);
    if (hot_tank_wh != NULL)
    {
        *hot_tank_wh = (s_policy.tanks[ENERGY_SAVING_TANK_HOT].saved_seconds * HOT_TANK_POWER_W) /
                       SECONDS_PER_HOUR;
    }
    if (cold_tank_wh != NULL)
    {
        *cold_tank_wh = (s_policy.tanks[ENERGY_SAVING_TANK_COLD].saved_seconds * COLD_TANK_POWER_W) /
                        SECONDS_PER_HOUR;
    }
}

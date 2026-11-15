#include "energy_saving_manager.h"
#include "water_profile.h"
#include "water_controller.h"
#include "esp_err.h"
#include "nvs.h"
#include "rtc_time.h"
#include "esp_timer.h"

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
    UI8 fault_logged;
    UI64 saved_seconds;
} TankPolicy;

static struct
{
    UI8 schedule_enabled;
    UI16 start_minute;
    UI16 end_minute;
    UI16 last_minute;
    _bool has_last_minute;
    _bool handled_first_schedule_deactivation;
    _bool has_last_update;
    UI64 last_update_seconds;
    _bool synced_from_mainboard;
    UI8 user_cmds;
    TankPolicy tanks[2];
} s_policy;

// for test
// __attribute__((weak)) void AppResponseTankStateToCloud(EnergySavingTank_t tank, UI8 enable)
// {
//     (void)tank;
//     (void)enable;
//     printf("[Dummy] AppResponseTankStateToCloud\n");
// }

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

_bool EnergySavingManagerClearConfig(void)
{
    nvs_handle_t handle;
    if (nvs_open(ENERGY_SAVING_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return D_FALSE;

    esp_err_t err = nvs_erase_key(handle, NVS_KEY_CONFIG);
    if (err == ESP_OK)
        err = nvs_commit(handle);
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

/* Attr 20 carries the mainboard's half-degree wire unit (C x 2), so the
 * boxed ranges are 80..200 hot and 0..40 cold. */
static UI8 ClampTemperature(EnergySavingTank_t tank, UI8 temperature)
{
    if (ENERGY_SAVING_TANK_HOT == tank)
    {
        if (temperature < MIN_TEMPERATURE_HOT_TANK) temperature = MIN_TEMPERATURE_HOT_TANK;
        if (temperature > MAX_TEMPERATURE_HOT_TANK) temperature = MAX_TEMPERATURE_HOT_TANK;
    }
    else
    {
        if (temperature < MIN_TEMPERATURE_COLD_TANK) temperature = MIN_TEMPERATURE_COLD_TANK;
        if (temperature > MAX_TEMPERATURE_COLD_TANK) temperature = MAX_TEMPERATURE_COLD_TANK;
    }
    return temperature;
}

/* Mainboard 4.6/4.9: 0x02..0x04 are the numbered faults (running too long,
 * temperature sensor, load broken). Turning the tank ON while one is active
 * is refused, exactly like the reference's Purifier_TankFaultBlocksOn.
 * Before the first STATUS frame the state is unknown, so ON is refused too;
 * OFF is always allowed (it is the protective direction). */
static _bool TankFaulted(EnergySavingTank_t tank)
{
    if (D_FALSE == HasMessageType1())
    {
        return D_TRUE;
    }

    const UI8 status = (ENERGY_SAVING_TANK_HOT == tank)
                           ? WaterProfileGetStatus(HOT_TANK_STATUS)
                           : WaterProfileGetStatus(COLD_TANK_STATUS);

    if (ENERGY_SAVING_TANK_HOT == tank)
    {
        return ((HOT_TANK_STATUS_HEATER_RUNNING_TOO_LONG == status) ||
                (HOT_TANK_STATUS_TEMPERATURE_SENSOR_ERROR == status) ||
                (HOT_TANK_STATUS_LOAD_BROKEN == status)) ? D_TRUE : D_FALSE;
    }

    return ((COLD_TANK_STATUS_COMPRESSOR_RUNNING_TOO_LONG == status) ||
            (COLD_TANK_STATUS_TEMPERATURE_SENSOR_ERROR == status) ||
            (COLD_TANK_STATUS_LOAD_BROKEN == status)) ? D_TRUE : D_FALSE;
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
    // AppResponseTankStateToCloud(tank, enabled);
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
    UI8 setting = WaterProfileGetSetting();
    s_policy.tanks[ENERGY_SAVING_TANK_HOT].temperature = WaterProfileGetHotTargetTemperature(); // default
    s_policy.tanks[ENERGY_SAVING_TANK_COLD].temperature = WaterProfileGetColdTargetTemperature(); // default
    s_policy.tanks[ENERGY_SAVING_TANK_HOT].enabled = setting & HOT_SWITCH_BIT ? D_TRUE : D_FALSE;
    s_policy.tanks[ENERGY_SAVING_TANK_COLD].enabled = setting & COLD_SWITCH_BIT ? D_TRUE : D_FALSE;
    s_policy.tanks[ENERGY_SAVING_TANK_HOT].base_enabled = setting & HOT_SWITCH_BIT ? D_TRUE : D_FALSE;
    s_policy.tanks[ENERGY_SAVING_TANK_COLD].base_enabled = setting & COLD_SWITCH_BIT ? D_TRUE : D_FALSE;;
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

    INT hour = 0; INT minute = 0; INT year = 0;
    GetNowTime(&year, &hour, &minute, NULL);
    if (year >= 125)
    {
        UI16 minute_of_day = hour * 60 + minute;

        if(!IsScheduleActive(minute_of_day))
        {
            TankPolicy *hot = &s_policy.tanks[ENERGY_SAVING_TANK_HOT];
            TankPolicy *cold = &s_policy.tanks[ENERGY_SAVING_TANK_COLD];
            hot->base_enabled = D_TRUE;
            cold->base_enabled = D_TRUE;
        }
        EnergySavingManagerUpdate(minute_of_day, (UI64)(esp_timer_get_time() / 1000000ULL));

    }    
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
    s_policy.user_cmds++;
    TankPolicy *policy = &s_policy.tanks[tank];
    if (temperature == 0xFFU)
    {
        temperature = policy->temperature;
    }
    else
    {
        temperature = ClampTemperature(tank, temperature);
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

    /* ON with an active tank fault is refused; the user's intent is kept in
     * base_enabled so the tank starts once the fault clears. OFF always goes
     * through. */
    if ((0U != enabled) && (D_TRUE == TankFaulted(tank)))
    {
        if (D_FALSE == policy->fault_logged)
        {
            policy->fault_logged = D_TRUE;
            printf("energy saving: tank %d ON refused, fault active\n", (int)tank);
        }
        return D_FALSE;
    }
    policy->fault_logged = D_FALSE;

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

/* Adopt the machine's own switch/setpoint state once the first STATUS frame
 * has arrived, unless the user already commanded something. At init the frame
 * has not been seen yet, so WaterProfileGetSetting() reads 0 and the policy
 * would otherwise force both tanks OFF on the first tick of every boot.
 * Called from the application loop, before the clock is needed. */
void EnergySavingManagerSyncFromMainboard(void)
{
    if ((D_TRUE == s_policy.synced_from_mainboard) || (D_FALSE == HasMessageType1()))
    {
        return;
    }

    s_policy.synced_from_mainboard = D_TRUE;
    if (0U != s_policy.user_cmds)
    {
        return;
    }

    const UI8 setting = WaterProfileGetSetting();
    TankPolicy *hot = &s_policy.tanks[ENERGY_SAVING_TANK_HOT];
    TankPolicy *cold = &s_policy.tanks[ENERGY_SAVING_TANK_COLD];
    const UI8 hot_target = WaterProfileGetHotTargetTemperature();
    const UI8 cold_target = WaterProfileGetColdTargetTemperature();

    hot->enabled = hot->base_enabled = (0U != (setting & HOT_SWITCH_BIT)) ? D_TRUE : D_FALSE;
    cold->enabled = cold->base_enabled = (0U != (setting & COLD_SWITCH_BIT)) ? D_TRUE : D_FALSE;
    if ((hot_target >= MIN_TEMPERATURE_HOT_TANK) && (hot_target <= MAX_TEMPERATURE_HOT_TANK))
    {
        hot->temperature = hot_target;
    }
    if ((cold_target >= MIN_TEMPERATURE_COLD_TANK) && (cold_target <= MAX_TEMPERATURE_COLD_TANK))
    {
        cold->temperature = cold_target;
    }
    printf("energy saving: synced from mainboard hot=%d/%d cold=%d/%d\n",
           hot->enabled, hot->temperature, cold->enabled, cold->temperature);
}

void EnergySavingManagerSyncBaseEnable(EnergySavingTank_t tank)
{
    if (D_FALSE == HasMessageType1())
    {
        return;
    }

    // const UI8 setting = WaterProfileGetSetting();
    // if(ENERGY_SAVING_TANK_HOT == tank)
    // {
    //     TankPolicy *hot = &s_policy.tanks[ENERGY_SAVING_TANK_HOT];
    //     hot->base_enabled = (0U != (setting & HOT_SWITCH_BIT)) ? D_TRUE : D_FALSE;
    //     printf("[hot] energy saving: synced base_enable %d\n", hot->base_enabled);
    // }else if(ENERGY_SAVING_TANK_COLD == tank)
    // {
    //     TankPolicy *cold = &s_policy.tanks[ENERGY_SAVING_TANK_COLD];
    //     cold->base_enabled = (0U != (setting & COLD_SWITCH_BIT)) ? D_TRUE : D_FALSE;
    //     printf("[cold] energy saving: synced base_enable %d\n", cold->base_enabled);
    // }
}

void EnergySavingManagerUpdate(UI16 minute_of_day, UI64 now_seconds)
{
    if (minute_of_day >= MINUTES_PER_DAY)
    {
        return;
    }
    if(!s_policy.schedule_enabled)
    {
        // printf("energy saving: schedule disabled, skipping update\n");
        return;
    }

    EnergySavingManagerSyncFromMainboard();

    const _bool was_schedule_active = s_policy.has_last_minute
                                         ? IsScheduleActive(s_policy.last_minute)
                                         : D_FALSE;
    const _bool schedule_active = IsScheduleActive(minute_of_day);
    const _bool schedule_just_deactivated =
        (was_schedule_active && !schedule_active &&
         !s_policy.handled_first_schedule_deactivation) ? D_TRUE : D_FALSE;
    if (schedule_just_deactivated)
    {
        s_policy.handled_first_schedule_deactivation = D_TRUE;
    }
    if(!was_schedule_active && schedule_active)
    {
        s_policy.handled_first_schedule_deactivation = D_FALSE;
    }
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
            //s_policy.handled_first_schedule_deactivation = D_FALSE;
        }
        else
        {
            /* Schedule end forces the tank back ON and updates the base
             * state too, so the next update does not immediately turn it OFF. */
            if (schedule_just_deactivated)
            {
                tank->base_enabled = D_TRUE;
            }
            desired_enabled = tank->base_enabled;
        }

        if ((D_TRUE == desired_enabled) && (D_TRUE == TankFaulted((EnergySavingTank_t)i)))
        {
            if (D_FALSE == tank->fault_logged)
            {
                tank->fault_logged = D_TRUE;
                printf("energy saving: tank %u ON deferred, fault active\n", (unsigned)i);
            }
            continue;
        }
        tank->fault_logged = D_FALSE;

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

/* Temperature-only update: the switch state is deliberately not touched.
 * This is the ATTR_TEMP_SET path; the reference only stores the setpoint and
 * sends it when the tank is on. */
_bool EnergySavingManagerSetTankTemperature(EnergySavingTank_t tank, UI8 temperature,
                                            UI64 now_seconds)
{
    if (tank > ENERGY_SAVING_TANK_COLD)
    {
        return D_FALSE;
    }

    AccountSavedTimeUntil(now_seconds);
    s_policy.user_cmds++;
    TankPolicy *policy = &s_policy.tanks[tank];
    const UI8 clamped = ClampTemperature(tank, temperature);

    EnergySavingConfig config = GetConfig();
    if (ENERGY_SAVING_TANK_HOT == tank)
    {
        config.hot_temperature = clamped;
    }
    else
    {
        config.cold_temperature = clamped;
    }
    if (!SaveConfig(&config))
    {
        return D_FALSE;
    }

    policy->temperature = clamped;

    if (D_TRUE == policy->enabled)
    {
        if (D_TRUE == TankFaulted(tank))
        {
            return D_FALSE;
        }
        return ApplyTank(tank, D_TRUE, clamped);
    }

    /* Tank is off: the setpoint rides out on the next ON command. */
    return D_TRUE;
}

_bool EnergySavingManagerGetTankEnabled(EnergySavingTank_t tank, UI8 *enabled)
{
    if ((tank > ENERGY_SAVING_TANK_COLD) || (NULL == enabled))
    {
        return D_FALSE;
    }
    *enabled = (D_TRUE == s_policy.tanks[tank].enabled) ? 1U : 0U;
    return D_TRUE;
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

void EnergySavingGetConfig(UI16 *enable, UI16 *start_minute, UI16 *end_minute)
{
    if(enable == NULL || start_minute == NULL || end_minute == NULL)
    {
        return;
    }
    EnergySavingConfig config = GetConfig();
    *enable = config.schedule_enabled;
    *start_minute = config.start_minute;
    *end_minute = config.end_minute;

    printf("schedule: enable: %d, start: %d, stop: %d \n", *enable, *start_minute, *end_minute);
}
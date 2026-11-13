#include "system_config.h"
#include "nvs.h"
#include <string.h>

#define CONFIG_NAMESPACE "ral_config"
#define CONFIG_KEY "snapshot"

static ReportConfig s_config;
static _bool s_config_loaded = D_FALSE;

static ConfigKey s_config_keys[SYSTEM_CONFIG_KEY_COUNT] = {
    {"alert.tier1.intervalMs", SCOPE_CORE, 15000, 5000, INT32_MAX}, 
    {"alert.tier1.maxSends", SCOPE_CORE, 5, 1, INT32_MAX}, 
    {"alert.tier2.intervalMs", SCOPE_CORE, 60000, 5000, INT32_MAX}, 
    {"alert.tier2.maxSends", SCOPE_CORE, 5, 1, INT32_MAX}, 
    {"alert.tier3.intervalMs", SCOPE_CORE, 6000000, 5000, INT32_MAX}, 
    {"report.intervalMs", SCOPE_CORE, 900000, 60000, 604800000}, 
    {"state.tds.deltaPct", SCOPE_STATE, 5, 1, 50}, 
    {"state.tds.floorPpm", SCOPE_STATE, 2, 0, 50}, 
    {"state.tds.minGapSec", SCOPE_STATE, 60, 10, 3600}, 
    {"state.tds.runPeriodSec", SCOPE_STATE, 300, 60, 3600}, 
    {"state.tds.idlePeriodSec", SCOPE_STATE, 3600, 300, 86400}, 
    {"state.temp.hotBandC", SCOPE_STATE, 5, 1, 30}, 
    {"state.temp.coldBandC", SCOPE_STATE, 3, 1, 20}, 
    {"state.temp.periodSec", SCOPE_STATE, 3600, 300, 86400}, 
    {"state.filter.stepPct", SCOPE_STATE, 1, 1, 20}, 
    {"state.water.deltaMl", SCOPE_STATE, 1000, 100, 100000}, 
    {"state.water.minGapSec", SCOPE_STATE, 900, 60, 86400}, 
    {"state.energy.deltaWh", SCOPE_STATE, 200, 10, 10000}, 
    {"state.energy.minGapSec", SCOPE_STATE, 900, 60, 86400}, 
    {"state.live.intervalSec", SCOPE_STATE, 60, 10, 600}, 
    {"state.live.ttlSec", SCOPE_STATE, 120, 30, 1800}, 
    {"alert.tdsIn.maxPpm", SCOPE_ALERT, 0, 1, 2000}, 
    {"alert.tdsOut.maxPpm", SCOPE_ALERT, 0, 1, 2000}, 
    {"alert.tdsOut.warnPpm", SCOPE_ALERT, 0, 1, 2000}, 
    {"alert.quality.onSec", SCOPE_ALERT, 60, 1, 3600}, 
    {"alert.quality.offSec", SCOPE_ALERT, 300, 1, 3600}, 
    {"alert.leak.onSec", SCOPE_ALERT, 10, 1, 3600}, 
    {"alert.leak.offSec", SCOPE_ALERT, 60, 1, 3600}, 
    {"alert.sensor.onSec", SCOPE_ALERT, 30, 1, 3600}, 
    {"alert.sensor.offSec", SCOPE_ALERT, 60, 1, 3600}, 
    {"alert.pump.maxRunMin", SCOPE_ALERT, 60, 5, 600}, 
    {"alert.hot.maxRunMin", SCOPE_ALERT, 60, 5, 600}, 
    {"alert.cold.maxRunMin", SCOPE_ALERT, 120, 5, 600}, 
    {"alert.filter.warnPct", SCOPE_ALERT, 30, 1, 99}, 
    {"alert.filter.criticalPct", SCOPE_ALERT, 10, 1, 99}, 
    {"general.tzOffsetMin", SCOPE_GENERAL, 420, -720, 840},
    {"test.totalFlushWater", SCOPE_GENERAL, 0, 0, 1000}
};

void SystemConfigLoad(void)
{
    if (s_config_loaded)
        return;
    for (size_t i = 0; i < sizeof(s_config_keys) / sizeof(s_config_keys[0]); ++i)
        s_config.values[i] = s_config_keys[i].def;
    nvs_handle_t h;
    if (nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &h) == ESP_OK)
    {
        size_t size = sizeof(s_config);
        if (nvs_get_blob(h, CONFIG_KEY, &s_config, &size) != ESP_OK || size != sizeof(s_config))
        {
            memset(s_config.version, 0, sizeof(s_config.version));
            for (size_t i = 0; i < sizeof(s_config_keys) / sizeof(s_config_keys[0]); ++i)
                s_config.values[i] = s_config_keys[i].def;
        }
        nvs_close(h);
    }
    s_config_loaded = D_TRUE;
}

bool SystemConfigSave(void)
{
    SystemConfigLoad();

    nvs_handle_t h;
    if (nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return D_FALSE;
    esp_err_t err = nvs_set_blob(h, CONFIG_KEY, &s_config, sizeof(s_config));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}


_bool SystemConfigGet(ReportConfig *config)
{
    if (config == NULL)
        return D_FALSE;

    SystemConfigLoad();
    *config = s_config;
    return D_TRUE;
}

const ConfigKey *SystemConfigGetKeyInfo(SystemConfigKey key)
{
    if (key < 0 || key >= SYSTEM_CONFIG_KEY_COUNT)
        return NULL;
    return &s_config_keys[key];
}

UI32 SystemConfigGetVersion(UI8 scope)
{
    if(scope >= SCOPE_COUNT) 
        return 0;
    return s_config.version[scope];
}

_bool SystemConfigSet(ReportConfig config)
{
    SystemConfigLoad();
    ReportConfig previous = s_config;
    for (size_t i = 0; i < SYSTEM_CONFIG_KEY_COUNT; ++i)
    {
        if (s_config_keys[i].scope == SCOPE_ALERT && s_config_keys[i].def == 0 && config.values[i] == 0)
            continue;
        if (config.values[i] < s_config_keys[i].min)
            config.values[i] = s_config_keys[i].min;
        if (config.values[i] > s_config_keys[i].max)
            config.values[i] = s_config_keys[i].max;
    }
    s_config = config;
    if (!SystemConfigSave())
    {
        s_config = previous;
        return D_FALSE;
    }
    return D_TRUE;
}

// UI32 SystemConfigGetReportIntervalMs(void)
// {
//     SystemConfigLoad();
//     return (UI32)s_config.values[SYSTEM_CONFIG_REPORT_INTERVAL_MS];
// }

#define DEFINE_CONFIG_GETTER(name, key) \
    SI32 SystemConfigGet##name(void) \
    { \
        SystemConfigLoad(); \
        return s_config.values[key]; \
    }

DEFINE_CONFIG_GETTER(AlertTier1IntervalMs, SYSTEM_CONFIG_ALERT_TIER1_INTERVAL_MS)
DEFINE_CONFIG_GETTER(AlertTier1MaxSends, SYSTEM_CONFIG_ALERT_TIER1_MAX_SENDS)
DEFINE_CONFIG_GETTER(AlertTier2IntervalMs, SYSTEM_CONFIG_ALERT_TIER2_INTERVAL_MS)
DEFINE_CONFIG_GETTER(AlertTier2MaxSends, SYSTEM_CONFIG_ALERT_TIER2_MAX_SENDS)
DEFINE_CONFIG_GETTER(AlertTier3IntervalMs, SYSTEM_CONFIG_ALERT_TIER3_INTERVAL_MS)
DEFINE_CONFIG_GETTER(ReportIntervalMs, SYSTEM_CONFIG_REPORT_INTERVAL_MS)
DEFINE_CONFIG_GETTER(StateTdsDeltaPct, SYSTEM_CONFIG_STATE_TDS_DELTA_PCT)
DEFINE_CONFIG_GETTER(StateTdsFloorPpm, SYSTEM_CONFIG_STATE_TDS_FLOOR_PPM)
DEFINE_CONFIG_GETTER(StateTdsMinGapSec, SYSTEM_CONFIG_STATE_TDS_MIN_GAP_SEC)
DEFINE_CONFIG_GETTER(StateTdsRunPeriodSec, SYSTEM_CONFIG_STATE_TDS_RUN_PERIOD_SEC)
DEFINE_CONFIG_GETTER(StateTdsIdlePeriodSec, SYSTEM_CONFIG_STATE_TDS_IDLE_PERIOD_SEC)
DEFINE_CONFIG_GETTER(StateTempHotBandC, SYSTEM_CONFIG_STATE_TEMP_HOT_BAND_C)
DEFINE_CONFIG_GETTER(StateTempColdBandC, SYSTEM_CONFIG_STATE_TEMP_COLD_BAND_C)
DEFINE_CONFIG_GETTER(StateTempPeriodSec, SYSTEM_CONFIG_STATE_TEMP_PERIOD_SEC)
DEFINE_CONFIG_GETTER(StateFilterStepPct, SYSTEM_CONFIG_STATE_FILTER_STEP_PCT)
DEFINE_CONFIG_GETTER(StateWaterDeltaMl, SYSTEM_CONFIG_STATE_WATER_DELTA_ML)
DEFINE_CONFIG_GETTER(StateWaterMinGapSec, SYSTEM_CONFIG_STATE_WATER_MIN_GAP_SEC)
DEFINE_CONFIG_GETTER(StateEnergyDeltaWh, SYSTEM_CONFIG_STATE_ENERGY_DELTA_WH)
DEFINE_CONFIG_GETTER(StateEnergyMinGapSec, SYSTEM_CONFIG_STATE_ENERGY_MIN_GAP_SEC)
DEFINE_CONFIG_GETTER(StateLiveIntervalSec, SYSTEM_CONFIG_STATE_LIVE_INTERVAL_SEC)
DEFINE_CONFIG_GETTER(StateLiveTtlSec, SYSTEM_CONFIG_STATE_LIVE_TTL_SEC)
DEFINE_CONFIG_GETTER(AlertTdsInMaxPpm, SYSTEM_CONFIG_ALERT_TDS_IN_MAX_PPM)
DEFINE_CONFIG_GETTER(AlertTdsOutMaxPpm, SYSTEM_CONFIG_ALERT_TDS_OUT_MAX_PPM)
DEFINE_CONFIG_GETTER(AlertTdsOutWarnPpm, SYSTEM_CONFIG_ALERT_TDS_OUT_WARN_PPM)
DEFINE_CONFIG_GETTER(AlertQualityOnSec, SYSTEM_CONFIG_ALERT_QUALITY_ON_SEC)
DEFINE_CONFIG_GETTER(AlertQualityOffSec, SYSTEM_CONFIG_ALERT_QUALITY_OFF_SEC)
DEFINE_CONFIG_GETTER(AlertLeakOnSec, SYSTEM_CONFIG_ALERT_LEAK_ON_SEC)
DEFINE_CONFIG_GETTER(AlertLeakOffSec, SYSTEM_CONFIG_ALERT_LEAK_OFF_SEC)
DEFINE_CONFIG_GETTER(AlertSensorOnSec, SYSTEM_CONFIG_ALERT_SENSOR_ON_SEC)
DEFINE_CONFIG_GETTER(AlertSensorOffSec, SYSTEM_CONFIG_ALERT_SENSOR_OFF_SEC)
DEFINE_CONFIG_GETTER(AlertPumpMaxRunMin, SYSTEM_CONFIG_ALERT_PUMP_MAX_RUN_MIN)
DEFINE_CONFIG_GETTER(AlertHotMaxRunMin, SYSTEM_CONFIG_ALERT_HOT_MAX_RUN_MIN)
DEFINE_CONFIG_GETTER(AlertColdMaxRunMin, SYSTEM_CONFIG_ALERT_COLD_MAX_RUN_MIN)
DEFINE_CONFIG_GETTER(AlertFilterWarnPct, SYSTEM_CONFIG_ALERT_FILTER_WARN_PCT)
DEFINE_CONFIG_GETTER(AlertFilterCriticalPct, SYSTEM_CONFIG_ALERT_FILTER_CRITICAL_PCT)
DEFINE_CONFIG_GETTER(GeneralTzOffsetMin, SYSTEM_CONFIG_GENERAL_TZ_OFFSET_MIN)
DEFINE_CONFIG_GETTER(TotalFlushWater, SYSTEM_CONFIG_TEST_TOTAL_FLUSH_WATER_VOLUME_L)

#undef DEFINE_CONFIG_GETTER

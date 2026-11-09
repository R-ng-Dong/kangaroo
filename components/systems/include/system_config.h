#ifndef SYSTEM_CONFIG_H_
#define SYSTEM_CONFIG_H_

#include "common.h"

/* Keep these values in the same order as the entries in system_config.c.
 * The numeric value is the index used to access the corresponding config value.
 */
typedef enum
{
    SYSTEM_CONFIG_ALERT_TIER1_INTERVAL_MS = 0,
    SYSTEM_CONFIG_ALERT_TIER1_MAX_SENDS,
    SYSTEM_CONFIG_ALERT_TIER2_INTERVAL_MS,
    SYSTEM_CONFIG_ALERT_TIER2_MAX_SENDS,
    SYSTEM_CONFIG_ALERT_TIER3_INTERVAL_MS,
    SYSTEM_CONFIG_REPORT_INTERVAL_MS,
    SYSTEM_CONFIG_STATE_TDS_DELTA_PCT,
    SYSTEM_CONFIG_STATE_TDS_FLOOR_PPM,
    SYSTEM_CONFIG_STATE_TDS_MIN_GAP_SEC,
    SYSTEM_CONFIG_STATE_TDS_RUN_PERIOD_SEC,
    SYSTEM_CONFIG_STATE_TDS_IDLE_PERIOD_SEC,
    SYSTEM_CONFIG_STATE_TEMP_HOT_BAND_C,
    SYSTEM_CONFIG_STATE_TEMP_COLD_BAND_C,
    SYSTEM_CONFIG_STATE_TEMP_PERIOD_SEC,
    SYSTEM_CONFIG_STATE_FILTER_STEP_PCT,
    SYSTEM_CONFIG_STATE_WATER_DELTA_ML,
    SYSTEM_CONFIG_STATE_WATER_MIN_GAP_SEC,
    SYSTEM_CONFIG_STATE_ENERGY_DELTA_WH,
    SYSTEM_CONFIG_STATE_ENERGY_MIN_GAP_SEC,
    SYSTEM_CONFIG_STATE_LIVE_INTERVAL_SEC,
    SYSTEM_CONFIG_STATE_LIVE_TTL_SEC,
    SYSTEM_CONFIG_ALERT_TDS_IN_MAX_PPM,
    SYSTEM_CONFIG_ALERT_TDS_OUT_MAX_PPM,
    SYSTEM_CONFIG_ALERT_TDS_OUT_WARN_PPM,
    SYSTEM_CONFIG_ALERT_QUALITY_ON_SEC,
    SYSTEM_CONFIG_ALERT_QUALITY_OFF_SEC,
    SYSTEM_CONFIG_ALERT_LEAK_ON_SEC,
    SYSTEM_CONFIG_ALERT_LEAK_OFF_SEC,
    SYSTEM_CONFIG_ALERT_SENSOR_ON_SEC,
    SYSTEM_CONFIG_ALERT_SENSOR_OFF_SEC,
    SYSTEM_CONFIG_ALERT_PUMP_MAX_RUN_MIN,
    SYSTEM_CONFIG_ALERT_HOT_MAX_RUN_MIN,
    SYSTEM_CONFIG_ALERT_COLD_MAX_RUN_MIN,
    SYSTEM_CONFIG_ALERT_FILTER_WARN_PCT,
    SYSTEM_CONFIG_ALERT_FILTER_CRITICAL_PCT,
    SYSTEM_CONFIG_GENERAL_TZ_OFFSET_MIN,

    SYSTEM_CONFIG_TEST_TOTAL_FLUSH_WATER_VOLUME_L,
    SYSTEM_CONFIG_KEY_COUNT
} SystemConfigKey;


typedef struct
{
    UI32 version[4];
    SI32 values[SYSTEM_CONFIG_KEY_COUNT];
} ReportConfig;

typedef struct
{
    const char *key;
    UI8 scope;
    SI32 def, min, max;
} ConfigKey;

enum
{
    SCOPE_CORE = 0,
    SCOPE_STATE,
    SCOPE_ALERT,
    SCOPE_GENERAL,
    SCOPE_COUNT
};


void SystemConfigLoad(void);
_bool SystemConfigSet(ReportConfig config);
_bool SystemConfigGet(ReportConfig *config);
const ConfigKey *SystemConfigGetKeyInfo(SystemConfigKey key);

UI32 SystemConfigGetVersion(UI8 scope);

SI32 SystemConfigGetAlertTier1IntervalMs(void);
SI32 SystemConfigGetAlertTier1MaxSends(void);
SI32 SystemConfigGetAlertTier2IntervalMs(void);
SI32 SystemConfigGetAlertTier2MaxSends(void);
SI32 SystemConfigGetAlertTier3IntervalMs(void);
SI32 SystemConfigGetReportIntervalMs(void);
SI32 SystemConfigGetStateTdsDeltaPct(void);
SI32 SystemConfigGetStateTdsFloorPpm(void);
SI32 SystemConfigGetStateTdsMinGapSec(void);
SI32 SystemConfigGetStateTdsRunPeriodSec(void);
SI32 SystemConfigGetStateTdsIdlePeriodSec(void);
SI32 SystemConfigGetStateTempHotBandC(void);
SI32 SystemConfigGetStateTempColdBandC(void);
SI32 SystemConfigGetStateTempPeriodSec(void);
SI32 SystemConfigGetStateFilterStepPct(void);
SI32 SystemConfigGetStateWaterDeltaMl(void);
SI32 SystemConfigGetStateWaterMinGapSec(void);
SI32 SystemConfigGetStateEnergyDeltaWh(void);
SI32 SystemConfigGetStateEnergyMinGapSec(void);
SI32 SystemConfigGetStateLiveIntervalSec(void);
SI32 SystemConfigGetStateLiveTtlSec(void);
SI32 SystemConfigGetAlertTdsInMaxPpm(void);
SI32 SystemConfigGetAlertTdsOutMaxPpm(void);
SI32 SystemConfigGetAlertTdsOutWarnPpm(void);
SI32 SystemConfigGetAlertQualityOnSec(void);
SI32 SystemConfigGetAlertQualityOffSec(void);
SI32 SystemConfigGetAlertLeakOnSec(void);
SI32 SystemConfigGetAlertLeakOffSec(void);
SI32 SystemConfigGetAlertSensorOnSec(void);
SI32 SystemConfigGetAlertSensorOffSec(void);
SI32 SystemConfigGetAlertPumpMaxRunMin(void);
SI32 SystemConfigGetAlertHotMaxRunMin(void);
SI32 SystemConfigGetAlertColdMaxRunMin(void);
SI32 SystemConfigGetAlertFilterWarnPct(void);
SI32 SystemConfigGetAlertFilterCriticalPct(void);
SI32 SystemConfigGetGeneralTzOffsetMin(void);
SI32 SystemConfigGetTotalFlushWater(void);

#endif

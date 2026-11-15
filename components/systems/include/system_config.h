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
    SYSTEM_CONFIG_STATE_TDS_INLET_DELTA_PPM,
    SYSTEM_CONFIG_STATE_TDS_OUT_DELTA_PPM,
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
    SYSTEM_CONFIG_GENERAL_FLUSH_SECONDS,

    SYSTEM_CONFIG_KEY_COUNT
} SystemConfigKey;


typedef struct
{
    UI32 version[4];
    UI32 values[SYSTEM_CONFIG_KEY_COUNT];
} ReportConfig;

typedef struct
{
    const char *key;
    UI8 scope;
    UI32 def, min, max;
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

UI32 SystemConfigGetAlertTier1IntervalMs(void);
UI32 SystemConfigGetAlertTier1MaxSends(void);
UI32 SystemConfigGetAlertTier2IntervalMs(void);
UI32 SystemConfigGetAlertTier2MaxSends(void);
UI32 SystemConfigGetAlertTier3IntervalMs(void);
UI32 SystemConfigGetReportIntervalMs(void);
UI32 SystemConfigGetStateTdsInletDelta(void);
UI32 SystemConfigGetStateTdsOutDelta(void);
UI32 SystemConfigGetStateTdsMinGapSec(void);
UI32 SystemConfigGetStateTdsRunPeriodSec(void);
UI32 SystemConfigGetStateTdsIdlePeriodSec(void);
UI32 SystemConfigGetStateTempHotBandC(void);
UI32 SystemConfigGetStateTempColdBandC(void);
UI32 SystemConfigGetStateTempPeriodSec(void);
UI32 SystemConfigGetStateFilterStepPct(void);
UI32 SystemConfigGetStateWaterDeltaMl(void);
UI32 SystemConfigGetStateWaterMinGapSec(void);
UI32 SystemConfigGetStateEnergyDeltaWh(void);
UI32 SystemConfigGetStateEnergyMinGapSec(void);
UI32 SystemConfigGetStateLiveIntervalSec(void);
UI32 SystemConfigGetStateLiveTtlSec(void);
UI32 SystemConfigGetAlertTdsInMaxPpm(void);
UI32 SystemConfigGetAlertTdsOutMaxPpm(void);
UI32 SystemConfigGetAlertTdsOutWarnPpm(void);
UI32 SystemConfigGetAlertQualityOnSec(void);
UI32 SystemConfigGetAlertQualityOffSec(void);
UI32 SystemConfigGetAlertLeakOnSec(void);
UI32 SystemConfigGetAlertLeakOffSec(void);
UI32 SystemConfigGetAlertSensorOnSec(void);
UI32 SystemConfigGetAlertSensorOffSec(void);
UI32 SystemConfigGetAlertPumpMaxRunMin(void);
UI32 SystemConfigGetAlertHotMaxRunMin(void);
UI32 SystemConfigGetAlertColdMaxRunMin(void);
UI32 SystemConfigGetAlertFilterWarnPct(void);
UI32 SystemConfigGetAlertFilterCriticalPct(void);
UI32 SystemConfigGetGeneralTzOffsetMin(void);
UI32 SystemConfigGetFlushSecond(void);

#endif

#ifndef FILTER_MANAGER_H
#define FILTER_MANAGER_H

#include "common.h"
#include "water_profile.h" // FilterTimeIndex

#define FILTER_MANAGER_REPORT_INTERVAL_MS 1*60000U
#define FILTER_MANAGER_REPORT_DECREASE_PERCENT 5

#define FILTER_EFFICIENCY 48 //%
#define PUMP_FLOW_RATE_LPH 51 //L/h

#define FIRST_FILTER_LIFETIME_HOURS 250U
#define SECOND_FILTER_LIFETIME_HOURS 500U
#define THIRD_FILTER_LIFETIME_HOURS 750U
#define RO_FILTER_LIFETIME_HOURS 2700U
#define POST_FILTER_LIFETIME_HOURS 1000U
#define FILTER_PID_SIZE 16U

#define FIRST_FILTER_REPLACEMENT_MONTH_THRESHOLD 12U
#define SECOND_FILTER_REPLACEMENT_MONTH_THRESHOLD 12U
#define THIRD_FILTER_REPLACEMENT_MONTH_THRESHOLD 12U
#define RO_FILTER_REPLACEMENT_MONTH_THRESHOLD 36U
#define POST_FILTER_REPLACEMENT_MONTH_THRESHOLD 12U

typedef enum
{
    FILTER_MANAGER_REPORT_ALL_FILTERS = 0,
    FILTER_MANAGER_REPORT_ONE_FILTER
} FilterManagerReportMode;

void FilterManagerInit(void);
void FilterManagerTick(UI32 now_ms);
void FilterManagerCheckWarning(UI32 now_ms, UI8 *remaining_percent);
UI32 FilterManagerGetWaterConsumption(void);
void FilterManagerSetRuntime(FilterTimeIndex filter_index, UI32 seconds);
_bool FilterManagerSetLifetimeSeconds(FilterTimeIndex filter_index, UI32 seconds);
_bool FilterManagerGetLifetimeSeconds(FilterTimeIndex filter_index, UI32 *seconds_out);
_bool FilterManagerSetPid(FilterTimeIndex filter_index, const UI8 pid[FILTER_PID_SIZE]);
_bool FilterManagerGetPid(FilterTimeIndex filter_index, UI8 pid_out[FILTER_PID_SIZE]);

#endif

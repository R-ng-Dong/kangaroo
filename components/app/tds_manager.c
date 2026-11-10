#include "tds_manager.h"
#include <stdio.h>
#include <math.h>
#include "app_context.h"
#include "warning_manager.h"

static void TdsManagerUpdateWarning(WarningId warning_id, bool should_be_active)
{
    WarningManagerUpdate(warning_id, should_be_active);
}

__attribute__((weak)) void AppPushTdsReportToCloud(UI16 inlet_ppm, UI16 ro_ppm)
{
    (void)inlet_ppm;
    (void)ro_ppm;
    //TODO
    printf("[Dummy] AppPushTdsReportToCloud\n");
}

__attribute__((weak)) void TdsInputUpdateRecord(UI16 inlet_ppm)
{
    (void)inlet_ppm;
    //TODO
    printf("[Dummy] TdsInputUpdateRecord\n");
}

__attribute__((weak)) void TdsOutputUpdateRecord(UI16 outlet_ppm)
{
    (void)outlet_ppm;
    //TODO
    printf("[Dummy] TdsOutputUpdateRecord\n");
}

static UI16 TdsDeltaPpm(UI16 old_value, UI16 new_value)
{
    if (old_value == 0)
    {
        return (new_value == 0) ? 0 : TDS_REPORT_DELTA_PERCENT;
    }

    if(old_value >= new_value)
    {
        return old_value - new_value;
    }
    return new_value - old_value;
}

void TdsManagerInit(void)
{
    AppContextInit();
}

bool TdsManagerShouldReport(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms)
{
    TdsState_t previous = {0};
    bool has_previous = AppContextHasTdsState();

    if (has_previous)
    {
        AppContextGetTdsState(&previous);
    }

    if (!has_previous)
    {
        return true;
    }
    // periodic feedback (60s)
    if ((now_ms - previous.last_sent_ms) >= TDS_REPORT_INTERVAL_MS)
    {
        // printf("TDS report interval reached: %lu ms\n", (unsigned long)(now_ms - previous.last_sent_ms));
        // AppPushTdsReportToCloud(inlet_ppm, ro_ppm);
        UI16 inlet_delta = TdsDeltaPpm(previous.last_reported_inlet_ppm, inlet_ppm);
        UI16 ro_delta = TdsDeltaPpm(previous.last_reported_ro_ppm, ro_ppm);
        if((inlet_delta >= TDS_REPORT_DELTA_PERCENT) || ro_delta >= TDS_REPORT_DELTA_PERCENT)
        {
            if ((inlet_delta >= TDS_REPORT_DELTA_PERCENT))
            {
                printf("TDS input report triggered by delta: inlet=%u ppm\n", inlet_delta);
                // TdsInputUpdateRecord(inlet_ppm);
                AppPushTdsReportToCloud(inlet_ppm, 0xFFFF); // post inlet only
            }
            if (ro_delta >= TDS_REPORT_DELTA_PERCENT)
            {
                printf("TDS output report triggered by delta: ro=%u ppm\n", ro_delta);
                // TdsOutputUpdateRecord(ro_ppm);
                AppPushTdsReportToCloud(0xFFFF, ro_ppm); // post ro only
            } 
            return true;
        }
       
    }

    return false;
}

void TdsManagerForceReport(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms)
{
    AppPushTdsReportToCloud(inlet_ppm, ro_ppm);
    AppContextMarkTdsReported(inlet_ppm, ro_ppm, now_ms);
}

void TdsManagerHandleSample(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms)
{
    bool has_previous = AppContextHasTdsState();

    AppContextSetTdsState(inlet_ppm, ro_ppm, now_ms);

    if (!has_previous)
    {
        AppPushTdsReportToCloud(inlet_ppm, ro_ppm);
        // TdsInputUpdateRecord(inlet_ppm);
        // TdsOutputUpdateRecord(ro_ppm);
        AppContextMarkTdsReported(inlet_ppm, ro_ppm, now_ms);
        return;
    }

    if (TdsManagerShouldReport(inlet_ppm, ro_ppm, now_ms))
    {
        AppContextMarkTdsReported(inlet_ppm, ro_ppm, now_ms);
        return;
    }

}

UI8 TdsManagerGetEfficiency(UI16 inlet_ppm, UI16 ro_ppm)
{
    if(!inlet_ppm || 0 == inlet_ppm)
    {
        return 0;
    }
    return ((inlet_ppm - ro_ppm) * 100) / inlet_ppm;
}

void TdsManagerCheckWarnings(UI16 inlet_ppm, UI16 ro_ppm, const UI8 max_num_detect)
{
    static _bool is_inlet_water_quality_warning = D_FALSE;
    static _bool is_output_water_quality_warning = D_FALSE;
    static _bool is_output_water_quality_fault = D_FALSE;

    static UI8 count_check_inlet_water_quality_warning = 0;
    static UI8 count_check_water_quality_warning = 0;
    static UI8 count_check_water_quality_fault = 0;

    //WARNING_ID_ER00051_INLET_WATER_QUALITY_WARNING
    if (inlet_ppm >= TDS_INPUT_ERROR_THRESHOLD)
    {
        if (count_check_inlet_water_quality_warning < max_num_detect)
        {
            count_check_inlet_water_quality_warning++;
        }
    }
    else if (count_check_inlet_water_quality_warning > 0)
    {
        count_check_inlet_water_quality_warning--;
    }
    if (count_check_inlet_water_quality_warning >= max_num_detect)
    {
        is_inlet_water_quality_warning = D_TRUE;
    }
    else if (count_check_inlet_water_quality_warning == 0)
    {
        is_inlet_water_quality_warning = D_FALSE;
    }

    //WARNING_ID_ER00066_OUTPUT_WATER_QUALITY_WARNING
    bool output_warning_detected = ro_ppm >= TDS_OUTPUT_WARNING_THRESHOLD &&
                                   ro_ppm < TDS_OUTPUT_ERROR_THRESHOLD;
    if (output_warning_detected)
    {
        if (count_check_water_quality_warning < max_num_detect)
        {
            count_check_water_quality_warning++;
        }
    }
    else if (count_check_water_quality_warning > 0)
    {
        count_check_water_quality_warning--;
    }
    if (count_check_water_quality_warning >= max_num_detect)
    {
        is_output_water_quality_warning = D_TRUE;
        is_output_water_quality_fault = D_FALSE;
        count_check_water_quality_fault = 0;
    }
    else if (count_check_water_quality_warning == 0)
    {
        is_output_water_quality_warning = D_FALSE;
    }

    //WARNING_ID_ER00052_OUTLET_WATER_QUALITY_FAULT
    if (ro_ppm >= TDS_OUTPUT_ERROR_THRESHOLD)
    {
        if (count_check_water_quality_fault < max_num_detect)
        {
            count_check_water_quality_fault++;
        }
    }
    else if (count_check_water_quality_fault > 0)
    {
        count_check_water_quality_fault--;
    }
    if (count_check_water_quality_fault >= max_num_detect)
    {
        is_output_water_quality_fault = D_TRUE;
        is_output_water_quality_warning = D_FALSE;
        count_check_water_quality_warning = 0;
    }
    else if (count_check_water_quality_fault == 0)
    {
        is_output_water_quality_fault = D_FALSE;
    }

    #if 0
    printf("[INLET_WATER_QUALITY_WARNING]: %d/%d, active: %d\n",\
         count_check_inlet_water_quality_warning, max_num_detect, is_inlet_water_quality_warning);
    printf("[OUTPUT_WATER_QUALITY_WARNING]: %d/%d, active: %d\n",\
         count_check_water_quality_warning, max_num_detect, is_output_water_quality_warning);
    printf("[OUTLET_WATER_QUALITY_FAULT]: %d/%d, active: %d\n",\
         count_check_water_quality_fault, max_num_detect, is_output_water_quality_fault);    
    #endif

    TdsManagerUpdateWarning(WARNING_ID_ER00051_INLET_WATER_QUALITY_WARNING,
                            is_inlet_water_quality_warning);
    TdsManagerUpdateWarning(WARNING_ID_ER00066_OUTPUT_WATER_QUALITY_WARNING,
                            is_output_water_quality_warning);
    TdsManagerUpdateWarning(WARNING_ID_ER00052_OUTLET_WATER_QUALITY_FAULT,
                            is_output_water_quality_fault);
}

void TdsManagerClearAllWarnings(void)
{
    TdsManagerUpdateWarning(WARNING_ID_ER00051_INLET_WATER_QUALITY_WARNING,
                            D_FALSE);
    TdsManagerUpdateWarning(WARNING_ID_ER00066_OUTPUT_WATER_QUALITY_WARNING,
                            D_FALSE);
    TdsManagerUpdateWarning(WARNING_ID_ER00052_OUTLET_WATER_QUALITY_FAULT,
                            D_FALSE);
}
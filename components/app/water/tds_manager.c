#include "tds_manager.h"
#include <stdio.h>
#include <math.h>
#include "app_context.h"
#include "warning_manager.h"
#include "system_config.h"

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

static _bool IsTdsInletChanged(UI16 old_value, UI16 new_value)
{
    const UI16 TDS_REPORT_INLET_DELTA_PPM = (UI16)SystemConfigGetStateTdsInletDelta();
    if (old_value == 0)
    {
        return (new_value == 0) ? D_FALSE : D_TRUE;
    }

    UI16 delta_ppm = 0;
    if(old_value >= new_value)
    {
        delta_ppm = old_value - new_value;
    }
    else{
        delta_ppm = new_value - old_value;
    }
    // printf("TDS input report triggered by delta: inlet=%u ppm\n", delta_ppm);

    return (delta_ppm >= TDS_REPORT_INLET_DELTA_PPM) ? D_TRUE : D_FALSE;
}

static _bool IsTdsOutChanged(UI16 old_value, UI16 new_value)
{
    const UI16 TDS_REPORT_OUT_DELTA_PPM = (UI16)SystemConfigGetStateTdsOutDelta();
    if (old_value == 0)
    {
        return (new_value == 0) ? D_FALSE : D_TRUE;
    }

    UI16 delta_ppm = 0;
    if(old_value >= new_value)
    {
        delta_ppm = old_value - new_value;
    }
    else{
        delta_ppm = new_value - old_value;
    }
    // printf("TDS output report triggered by delta: ro=%u ppm\n", delta_ppm);
    return (delta_ppm >= TDS_REPORT_OUT_DELTA_PPM) ? D_TRUE : D_FALSE;
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
    const UI32 TDS_REPORT_INTERVAL_MS = SystemConfigGetStateTdsMinGapSec() * 1000;
    // periodic feedback (60s)
    if ((now_ms - previous.last_sent_ms) >= TDS_REPORT_INTERVAL_MS)
    {
        // printf("TDS report interval reached: %lu ms\n", (unsigned long)(now_ms - previous.last_sent_ms));

        _bool inlet_changed = IsTdsInletChanged(previous.last_reported_inlet_ppm, inlet_ppm);
        _bool out_changed = IsTdsOutChanged(previous.last_reported_ro_ppm, ro_ppm);

        if(inlet_changed == D_TRUE || out_changed == D_TRUE)
        {
            if (inlet_changed)
            {
                AppContextMarkTdsReported(inlet_ppm, TDS_INVALID_VALUE, now_ms);
                AppPushTdsReportToCloud(inlet_ppm, TDS_INVALID_VALUE); // post inlet only
                TdsInputUpdateRecord(inlet_ppm);
            }
            if (out_changed)
            { 
                AppContextMarkTdsReported(TDS_INVALID_VALUE, ro_ppm, now_ms);
                AppPushTdsReportToCloud(TDS_INVALID_VALUE, ro_ppm); // post ro only
                TdsOutputUpdateRecord(ro_ppm);
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
        // AppContextMarkTdsReported(inlet_ppm, ro_ppm, now_ms);
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

    const UI32 TDS_INPUT_ERROR_THRESHOLD = SystemConfigGetAlertTdsInMaxPpm();
    const UI32 TDS_OUTPUT_ERROR_THRESHOLD = SystemConfigGetAlertTdsOutMaxPpm();
    const UI32 TDS_OUTPUT_WARNING_THRESHOLD = SystemConfigGetAlertTdsOutWarnPpm();

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
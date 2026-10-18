#ifndef TDS_MANAGER_H
#define TDS_MANAGER_H

#include "common.h"

#define TDS_REPORT_INTERVAL_MS 60000U
#define TDS_REPORT_DELTA_PERCENT 5.0f

#define TDS_INPUT_ERROR_THRESHOLD 500 //ppm 
 
// #define TDS_OUTPUT_NORMAL_THRESHOLD     
#define TDS_OUTPUT_WARNING_THRESHOLD  200 
#define TDS_OUTPUT_ERROR_THRESHOLD  300

void TdsManagerInit(void);
void TdsManagerHandleSample(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms);
void TdsManagerForceReport(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms);
bool TdsManagerShouldReport(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms);
UI8 TdsManagerGetEfficiency(UI16 inlet_ppm, UI16 ro_ppm);
void TdsManagerCheckWarnings(UI16 inlet_ppm, UI16 ro_ppm);

#endif

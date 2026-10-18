#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <stdbool.h>
#include "common.h"

typedef struct {
    UI8 ro_status;
    UI8 ply_status;
    UI8 hot_tank_status;
    UI8 cold_tank_status;
    UI8 setting_val;
    UI8 input_status;
    UI8 output_status;
    UI8 switch_status;
    UI8 led_status;
} WaterProfileState_t;

typedef struct {
    UI32 total_pump_runtime;
    UI32 total_hot_tank_runtime;
    UI32 total_cold_tank_runtime;
    UI32 total_electrolysis_runtime;
} WaterPurifierRuntime_t;

typedef struct {
    UI16 inlet_ppm;
    UI16 ro_ppm;
    UI32 last_update_ms;
    UI32 last_sent_ms;
    UI16 last_reported_inlet_ppm;
    UI16 last_reported_ro_ppm;
} TdsState_t;

typedef struct {
    WaterProfileState_t water;
    WaterPurifierRuntime_t runtime;
    TdsState_t tds;
    bool water_valid;
    bool runtime_valid;
    bool tds_valid;
} AppContext_t;

void AppContextInit(void);

void AppContextSetWaterState(const WaterProfileState_t *state);
void AppContextGetWaterState(WaterProfileState_t *out_state);
bool AppContextHasWaterState(void);

void AppContextSetRuntime(const WaterPurifierRuntime_t *state);
void AppContextGetRuntime(WaterPurifierRuntime_t *out_state);
bool AppContextHasRuntime(void);

void AppContextSetTdsState(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms);
void AppContextGetTdsState(TdsState_t *out_state);
bool AppContextHasTdsState(void);
void AppContextMarkTdsReported(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms);

#endif

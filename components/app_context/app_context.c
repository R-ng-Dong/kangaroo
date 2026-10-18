#include "app_context.h"

#include <string.h>

static AppContext_t s_app_context;

void AppContextInit(void)
{
    memset(&s_app_context, 0, sizeof(s_app_context));
}

void AppContextSetWaterState(const WaterProfileState_t *state)
{
    if (state == NULL)
    {
        return;
    }

    s_app_context.water = *state;
    s_app_context.water_valid = true;
}

void AppContextGetWaterState(WaterProfileState_t *out_state)
{
    if (out_state == NULL)
    {
        return;
    }

    *out_state = s_app_context.water;
}

bool AppContextHasWaterState(void)
{
    return s_app_context.water_valid;
}


void AppContextSetRuntime(const WaterPurifierRuntime_t *state)
{
    if (state == NULL)
    {
        return;
    }

    s_app_context.runtime = *state;
    s_app_context.runtime_valid = true;
}

void AppContextGetRuntime(WaterPurifierRuntime_t *out_state)
{
    if (out_state == NULL)
    {
        return;
    }

    *out_state = s_app_context.runtime;
}

bool AppContextHasRuntime(void)
{
    return s_app_context.runtime_valid;
}

void AppContextSetTdsState(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms)
{
    s_app_context.tds.inlet_ppm = inlet_ppm;
    s_app_context.tds.ro_ppm = ro_ppm;
    s_app_context.tds.last_update_ms = now_ms;
    s_app_context.tds_valid = true;
}

void AppContextGetTdsState(TdsState_t *out_state)
{
    if (out_state == NULL)
    {
        return;
    }

    *out_state = s_app_context.tds;
}

bool AppContextHasTdsState(void)
{
    return s_app_context.tds_valid;
}

void AppContextMarkTdsReported(UI16 inlet_ppm, UI16 ro_ppm, UI32 now_ms)
{
    s_app_context.tds.last_reported_inlet_ppm = inlet_ppm;
    s_app_context.tds.last_reported_ro_ppm = ro_ppm;
    s_app_context.tds.last_sent_ms = now_ms;
}


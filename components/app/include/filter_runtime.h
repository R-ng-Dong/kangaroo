#ifndef FILTER_RUNTIME_H
#define FILTER_RUNTIME_H

#include <stdbool.h>

#include "common.h"
#include "water_profile.h"

#define FILTER_RUNTIME_MODEL_UART 0
#define FILTER_RUNTIME_MODEL_LOCAL 1

#define FILTER_RUNTIME_FILTER_COUNT 5U

typedef int FilterRuntimeModel_t;

typedef struct {
    UI32 seconds[FILTER_RUNTIME_FILTER_COUNT];
    bool valid[FILTER_RUNTIME_FILTER_COUNT];
} FilterRuntimeSnapshot_t;

void FilterRuntimeInit(void);
void FilterRuntimeSetModel(FilterRuntimeModel_t model);
FilterRuntimeModel_t FilterRuntimeGetModel(void);
void FilterRuntimeReset(void); // not use
bool FilterRuntimeLoadFromFlash(void);
bool FilterRuntimeSaveToFlash(void);

bool FilterRuntimeRefreshFromUart(void);
bool FilterRuntimeUpdateLocal(bool pump_active, UI32 now_ms);

bool FilterRuntimeGetSnapshot(UI32 now_ms, FilterRuntimeSnapshot_t *out_snapshot);
bool FilterRuntimeHasValue(FilterTimeIndex filter_index);
bool FilterRuntimeHasAnyValue(void);

void FilterRuntimeSet(FilterTimeIndex filter_index, UI32 seconds);
bool FilterRuntimeGet(FilterTimeIndex filter_index, UI32 *seconds_out);

#endif

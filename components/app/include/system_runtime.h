#ifndef SYSTEM_RUNTIME_H
#define SYSTEM_RUNTIME_H

#include <stdbool.h>

#include "common.h"


void SystemRuntimeInit(void);
bool SystemRuntimeReconfig(UI32 time_s);

bool SystemRuntimeUpdate(UI32 now_ms);
UI32 SystemRuntimeGetSeconds(UI32 now_ms);

#endif

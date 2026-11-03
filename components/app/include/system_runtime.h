#ifndef SYSTEM_RUNTIME_H
#define SYSTEM_RUNTIME_H

#include "common.h"

#define SYS_UPDATE_ALL 0xff
#define SYS_RUNTIME_INDEX 5

_bool SystemRuntimeInit(void);
_bool SystemRuntimeReconfig(UI8 filter_idx, UI32 time_s, UI32 now_ms);

_bool SystemRuntimeUpdate(UI8 filter_idx, UI32 now_ms);
UI32 SystemRuntimeGetSeconds(UI8 filter_idx, UI32 now_ms);

#endif

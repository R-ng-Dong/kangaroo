#ifndef FLUSH_CYCLE_STATE_H
#define FLUSH_CYCLE_STATE_H

#include "common.h"

typedef struct
{
    UI32 drain_time_s;
    UI32 post_filter_flush_time_s;
    UI16 total_drain_water_filter1;
    UI16 total_drain_water_filter2;
    UI16 total_drain_water_filter3;
    UI16 total_drain_water_ro;
    UI16 total_drain_water_post;
    UI8 drain_done;
    UI8 post_filter_flush_done;
} FlushCycleState_t;

void FlushCycleStateInit(void);
_bool FlushCycleStateGet(FlushCycleState_t *state_out);
_bool FlushCycleStateSetDrainTime(UI32 time_s);
_bool FlushCycleStateSetPostFilterFlushTime(UI32 time_s);
_bool FlushCycleStateSetDrainDone(_bool done);
_bool FlushCycleStateSetPostFilterFlushDone(_bool done);
_bool FlushCycleStateSetTotalDrainWater(UI8 filter_idx);

#endif

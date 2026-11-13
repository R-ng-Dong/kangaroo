#pragma once

/**
 * @file IOT_ContractRestore.h
 * @brief Restore at boot: replay the root device's stored controls through apply.
 */

#include "IOT_Contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Replay now, on the calling task, according to the configured policy. */
void IOT_ContractRestoreRun(const IOT_ContractCallbacks_t *cb);

/** Post IOT_ContractRestoreRun() to core's service task, where every command runs, so a restore
 *  never interleaves with one. If the post fails, it runs here, with a warning. @p cb must outlive
 *  the posted job (it is kept here: the job itself carries no argument, because core's queue frees
 *  the argument of every job it runs). */
void IOT_ContractRestoreSchedule(const IOT_ContractCallbacks_t *cb);

#ifdef __cplusplus
}
#endif

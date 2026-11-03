/**
 * @file IOT_CoreHeapProbe.h
 * @brief Heap sampling around a scope, for diagnosing fragmentation (dev only)
 *
 * Compiled out entirely unless CONFIG_IOT_HEAP_PROBE_ENABLED is set. Leave it OFF in
 * shipped firmware.
 *
 * The question this answers is "did this scope fragment the heap", which free-heap
 * alone cannot: under fragmentation total free barely moves while the largest free
 * block — the one a TLS session needs — collapses, and the heap gains holes. So a
 * sample carries all three, and the delta line prints all three.
 *
 * Typical use, around a scope suspected of churning the heap:
 *
 *     IOT_HeapProbeSample_t before;
 *     IOT_HeapProbeSample(&before);
 *     ... build and send the message ...
 *     IOT_HeapProbeLogDelta("ROOM RPT devices=10", &before);
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "IOT_ErrorManager.h"
#include "IOT_Osal.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** One heap reading: the three numbers that separate fragmentation from a leak. */
typedef struct
{
    uint32_t freeBytes;        /**< total free bytes */
    uint32_t largestFreeBlock; /**< biggest single allocatable block */
    uint32_t freeBlocks;       /**< number of free holes */
} IOT_HeapProbeSample_t;

/** Take a reading. No-op on `out == NULL`; zeroed if the platform has no counters. */
void IOT_HeapProbeSample(IOT_HeapProbeSample_t *out);

/**
 * @brief Format the before/after change into `out` as
 *        "free A->B (dD) largest A->B (dD) freeblk A->B (dD)".
 *
 * Always NUL-terminates within `outSize` and never writes past it.
 */
void IOT_HeapProbeFormatDelta(const IOT_HeapProbeSample_t *before,
                              const IOT_HeapProbeSample_t *after,
                              char *out, size_t outSize);

/** Sample now and log the change since `before`, prefixed by `label`. */
void IOT_HeapProbeLogDelta(const char *label, const IOT_HeapProbeSample_t *before);

#ifdef __cplusplus
}
#endif

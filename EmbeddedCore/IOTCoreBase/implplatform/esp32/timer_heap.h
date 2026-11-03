#pragma once

/*
 * The ESP32 timer heap: a min-heap of pending callbacks keyed on a 64-bit deadline in
 * milliseconds of uptime.
 *
 * Pure C with no FreeRTOS or ESP-IDF dependency, so it can be tested on a host.
 * Locking, handle allocation and the timer task
 * stay in IOT_TimerMgmt.c, which calls this under its own mutex.
 *
 * 64 bits on purpose. The previous heap stored uint32 deadlines and compared them raw, so
 * after 49.7 days of uptime a timer armed just before the wrap carried a deadline the
 * post-wrap clock would not reach for another 49.7 days.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "IOT_TimerMgmt.h"

typedef struct
{
    IOT_TimerEventCb cb;
    void *userData;
    uint64_t dueMs;
    IOT_TimerHandle_t id;
} TimerHeapEntry_t;

typedef struct
{
    TimerHeapEntry_t *entries;
    size_t size;
    size_t capacity;
} TimerHeap_t;

/** @brief Allocate an empty heap. A capacity of 0 means 16. */
bool TimerHeap_Init(TimerHeap_t *h, size_t capacity);

/** @brief Add an entry, doubling the capacity when full. False only on allocation failure. */
bool TimerHeap_Push(TimerHeap_t *h, const TimerHeapEntry_t *e);

/** @brief Take the earliest entry if its deadline is at or before nowMs. */
bool TimerHeap_PopDue(TimerHeap_t *h, uint64_t nowMs, TimerHeapEntry_t *out);

/** @brief Remove the entry with this handle. False if no entry has it. */
bool TimerHeap_Remove(TimerHeap_t *h, IOT_TimerHandle_t id);

/** @brief Drop every pending entry. The storage is kept for reuse. */
void TimerHeap_Clear(TimerHeap_t *h);

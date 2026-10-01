#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "IOT_ErrorManager.h"

// Callback function type
typedef void (*IOT_TimerEventCb)(void *);

// Opaque timer handle: a monotonic ID returned by IOT_TimerInsertCb, used to cancel.
// Never reused, so cancelling an already-fired handle is safe (returns IOT_ERR_NOT_FOUND).
typedef uint32_t IOT_TimerHandle_t;
#define IOT_TIMER_HANDLE_INVALID 0u

// Timer event structure
typedef struct
{
    IOT_TimerEventCb cb;
    void *userData;
    uint32_t timeMs;
    IOT_TimerHandle_t id;
} IOT_TimerEventCall;

// ============================================================================
// TIMER HEAP API
// ============================================================================
void IOT_TimerCallbackSupportInit(void);
iot_err_t IOT_TimerInit(void);
void IOT_TimerDeinit(void);
// Schedule cb(userData) to fire after timeMs. Returns a handle (> 0) usable with
// IOT_TimerCancel, or IOT_TIMER_HANDLE_INVALID (0) on failure. Existing callers that
// treat the result as a bool still work: INVALID is falsy, any real handle is truthy.
IOT_TimerHandle_t IOT_TimerInsertCb(IOT_TimerEventCb cb, void *userData, uint32_t timeMs);
// Cancel a pending timer by handle (before it fires).
// Returns IOT_OK if it was pending and is now cancelled, IOT_ERR_NOT_FOUND if the handle
// is invalid or already fired/cancelled.
iot_err_t IOT_TimerCancel(IOT_TimerHandle_t handle);
size_t IOT_TimerGetCounting(void);
uint32_t IOT_TimerGetCurrentTimeMs(void);
void IOT_TimerDelayMs(uint32_t delayMs);
iot_err_t IOT_TimerSetTimeZone(const char *timeZone);
iot_err_t IOT_TimerGetUnixTimeMs(uint8_t *outTimeBytes);
iot_err_t IOT_TimerGetU64UnixTimeMs(uint64_t *outUnixTimeMs);
bool IOT_TimerIsTimeSynced(void);
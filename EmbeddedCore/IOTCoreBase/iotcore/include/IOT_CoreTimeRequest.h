#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Did the caller ask for the time?
 *
 * GET_STATE carries an optional 8-byte time field, and we read it in two places -- core
 * deciding whether to attach the time-sync block, and the SDK deciding whether a
 * time-sync failure matters. Both tested `unixTime != NULL`; the cloud tests
 * `timeRequest > 0`, and sends eight zero bytes for "not asking".
 *
 * One function rather than two comparisons, because the two sites must agree: if core
 * attaches a block the SDK thought was not requested, or the SDK fails a reply core
 * would have answered, the app sees an empty screen and nothing is logged anywhere.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief True when `unixTime` is a real request: non-NULL and not eight zero bytes.
 *
 * @param unixTime 8-byte big-endian epoch milliseconds, or NULL.
 */
static inline bool IOT_CoreTimeRequested(const uint8_t *unixTime)
{
    if (unixTime == NULL)
    {
        return false;
    }
    for (int i = 0; i < 8; i++)
    {
        if (unixTime[i] != 0)
        {
            return true;
        }
    }
    return false;
}

#ifdef __cplusplus
}
#endif

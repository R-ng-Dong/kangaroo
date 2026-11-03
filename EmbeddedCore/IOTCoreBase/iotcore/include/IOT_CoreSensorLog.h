#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"

/*
 * Sensor log responses.
 *
 * Split out of IOT_CoreReporter.h on 2026-10-06. That header remains as an umbrella that
 * includes this one, so existing includes keep working.
 */

/* ============================
 *  SENSOR_LOG Response
 * ============================ */

/**
 * @brief One day's log for one element, as facts rather than bytes.
 *
 * The SDK supplies timestamps and values; core owns the wire layout, the value width,
 * the ordering and the truncation policy. Nothing here is a serialised form -- do not
 * pre-pack entries.
 *
 * This struct used to carry a raw `logData` blob whose layout was never published. If
 * you are porting from that field: stop building bytes, hand over the entries, and
 * delete your packing code.
 *
 * All arrays are borrowed for the duration of the call; core copies what it sends.
 */

#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint16_t year;
    uint16_t dayOfYear;

    /** Minutes since LOCAL midnight, one per entry. Any order -- core sorts. */
    const uint16_t *minutes;
    /** count * valueStride bytes. Entry i starts at values[i * valueStride]. */
    const uint8_t *values;
    /** Number of entries. 0 is valid: it sends an empty log, which is not an error. */
    uint16_t count;
    /** Bytes per value AS SUPPLIED. Core never reads past this per entry. */
    uint8_t valueStride;
    /** Bytes of each value to LOG. 0 derives from attrId, correct for every standard
     *  attribute. Set it for a partner custom attribute (0xF700-0xFEFF), or to keep a
     *  width core does not know about. */
    uint8_t logValueBytes;

    IOT_RequestContext_t ctx;
} IOT_CoreSensorLogResponse_t;

/**
 * @brief Build and send a SENSOR_LOG response message.
 *
 * Echoes the request params and serialises the entries: core derives the log value width
 * from attrId, sorts newest-first, widens a value narrower than two bytes, drops the
 * oldest entries past what one reply carries, and packs. Sends the response even when
 * count is 0 -- an empty log is an answer, not an error.
 *
 * **This is Record V1, and the shipped library does not include it.** The call sends
 * NOTHING -- not an empty reply -- and returns IOT_ERR_NOT_SUPPORTED with a named log
 * line; the app's query times out. Answer record queries with Record V2
 * (IOT_CoreRecord.h) instead.
 *
 * @param response Sensor log query echo + the day's entries (SDK retains ownership of
 *                 the arrays; they need only outlive the call)
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG for a null entry array or a zero
 *         valueStride alongside a non-zero count, IOT_ERR_NOT_SUPPORTED in the
 *         shipped library (no V1)
 */
iot_err_t IOT_CoreRequestSensorLogResponse(const IOT_CoreSensorLogResponse_t *response);
#ifdef __cplusplus
}
#endif

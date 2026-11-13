#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h"

/*
 * STATE events: GET/SET requests and the sensor-log request, with their target resolution.
 *
 * Split out of IOT_CoreEventHandler.h on 2026-10-14. That header remains the umbrella
 * (category enum, IOT_CoreEvent_t, callback registration) and includes this file, so
 * existing includes keep working. Prefer this header in new code that only needs it.
 */

/* ============================
 *  STATE EVENTS
 * ============================ */


#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    IOT_EVENT_TYPE_STATE_SET,
    IOT_EVENT_TYPE_STATE_GET,
    IOT_EVENT_TYPE_STATE_SENSOR_LOG,
    /* Record V2. APPENDED, never inserted: an app built on older headers keeps its own
     * numbering for the values above, and renumbering SENSOR_LOG under it would route
     * every sensor-log query to whatever handler now owns that number. */
    IOT_EVENT_TYPE_STATE_RECORD,
} IOT_EventTypeState_t;

typedef struct
{
    uint16_t eid;
    IOT_RequestContext_t ctx;
    uint8_t *unixTime;
} IOT_EventStateGetRequest_t;

typedef struct {
    uint16_t elemId;
} elem_set_t;

/**
 * @brief Element and attribute data for a SET command.
 *
 * Contains the list of elements to change and the attribute value to set.
 * @note The `eid` field is NOT used in SET events — use IOT_EventStateSetRequest_t.targetType
 *       and the target union instead. This field exists for legacy/internal use only.
 */
typedef struct {
    uint16_t numOfElems;
    uint16_t eid;
    elem_set_t *elemList;
    uint16_t attrId;
    uint16_t attrSize;
    uint8_t *attrValue;
} dev_set_info_t;

/**
 * @brief Target type for SET state commands.
 *
 * Determined by the EID range in the message, not the protocol field:
 *   eid <  0xC000  →  DEVICE   (single device, core has already resolved eid)
 *   eid == 0xC000  →  LOCATION (all devices in this location — SDK expands via device DAO)
 *   eid >  0xC000  →  GROUP    (specific room/scene — SDK expands via group DAO)
 *
 * VOICE is a separate path (cloud addressed by devId, not eid). When the voice command
 * carries a group, core resolves it to LOCATION or GROUP instead.
 */
typedef enum
{
    IOT_SET_TARGET_DEVICE,   // Single device  — use target.device.eid
    IOT_SET_TARGET_LOCATION, // All devices    — no target field needed; targetType alone is sufficient
    IOT_SET_TARGET_GROUP,    // Specific group — use target.group.groupEid (> 0xC000)
    IOT_SET_TARGET_VOICE,    // Voice path     — use target.voice.devId; SDK resolves devId→eid
} IOT_SetTargetType_t;

/**
 * @brief SET state request event data.
 *
 * When handling IOT_EVENT_TYPE_STATE_SET, the SDK must:
 * 1. Check targetType and resolve the target to device EID(s) using the target union
 * 2. For each resolved device: update DAO state, actuate hardware, build report
 *
 * The devSetInfo contains the element list, attribute ID, and value to set — shared
 * across all target devices. The devSetInfo.eid field is UNUSED; use targetType instead.
 */
typedef struct
{
    IOT_SetTargetType_t targetType;
    union
    {
        struct
        {
            uint16_t eid;
        } device;
        struct
        {
            /* STRICTLY GREATER than IOT_GROUP_EID_BASE. 0xC000 itself is NOT a group —
             * it means LOCATION (all devices), and core reports it as
             * IOT_SET_TARGET_LOCATION, never as GROUP. Do not resolve a target by
             * comparing the EID yourself; read targetType, which core has already
             * decided. A partner who wrote `if (eid >= 0xC000) treat_as_group()` had
             * every room command fail, because no group record with id 0xC000 exists. */
            uint16_t groupEid; // > IOT_GROUP_EID_BASE
        } group;
        struct
        {
            uint8_t *devId;     // 12-byte device ID from cloud
            uint16_t devIdLen;
            uint16_t groupEid;  // Not used: a voice command that names a group reaches you as LOCATION or GROUP
        } voice;
    } target;
    dev_set_info_t devSetInfo;  // elemList, attrId, attrValue (eid field unused — use target instead)
    IOT_RequestContext_t ctx;
    uint8_t targetDeviceType;
    /** Your answer to the command. Core presets it to IOT_ERR_NOT_SUPPORTED, dispatches
     *  synchronously and reads it when your callback returns: an acknowledged command is answered
     *  success for IOT_OK and failure with the code otherwise. Leaving it untouched, or registering
     *  no IOT_EVENT_STATE handler, answers "not supported". With sdkcontract the contract writes it
     *  from its handling of the command (see IOT_ContractStart()). */
    iot_err_t result;
} IOT_EventStateSetRequest_t;

typedef struct
{
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint16_t year;
    uint16_t dayOfYear;
    IOT_RequestContext_t ctx;
} IOT_EventStateSensorLogRequest_t;

/**
 * @brief A Record V2 query.
 *
 * Core has already parsed the message into the attribute, the element and the
 * checkpoint below. Answer it synchronously with
 * IOT_CoreRequestRecordResponse(): ctx.senderId points into the message reader's buffer
 * and is freed when the handler returns.
 */
typedef struct
{
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    /**
     * Epoch MILLISECONDS of the block being asked for, or 0 for the current one.
     *
     * 0 is both "I have never seen a checkpoint" (the first request after GET_STATE) and
     * "give me the open block". There is no third meaning: 0 is also the end-of-chain
     * sentinel, so no real block ever carries it.
     */
    uint64_t checkpoint;
    IOT_RequestContext_t ctx;
} IOT_EventStateRecordRequest_t;

typedef struct
{
    IOT_EventTypeState_t type;
    union
    {
        IOT_EventStateGetRequest_t getRequest;
        IOT_EventStateSetRequest_t setRequest;
        IOT_EventStateSensorLogRequest_t sensorLogRequest;
        IOT_EventStateRecordRequest_t recordRequest;
    } data;
} IOT_CoreStateEvent_t;
#ifdef __cplusplus
}
#endif

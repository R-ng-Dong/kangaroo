#pragma once
#include <inttypes.h>

#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"

/*
 * Automations: trigger reports, sync-on-off, condition groups, the SMART command
 * responses, and the legacy stair-switch send.
 *
 * Split out of IOT_CoreReporter.h on 2026-10-06. That header remains as an umbrella that
 * includes this one, so existing includes keep working.
 */

/* ============================
 *  Smart Trigger Report (to cloud)
 * ============================ */

/**
 * Data for a smart trigger activation report sent to cloud.
 *
 * Cloud receives this and dispatches bind commands to target devices
 * (cross-device smart automation).
 */

#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    uint16_t smid;            // Smart automation ID
    uint16_t attrId;          // Triggered attribute ID
    uint16_t attrSize;        // Size of triggered attribute value
    const uint8_t *attrValue; // Triggered attribute value (caller retains ownership)
    uint8_t triggerKey[8];    // Trigger key (ownerEid(2) + ownerCount(2) + extEid(2) + extCount(2))
    uint8_t timeCfg[4];      // Time config (type(2) + value(2))
} IOT_CoreSmartTriggerReport_t;

/**
 * @brief Send a smart trigger activation report to cloud via MQTT.
 *
 * Reports the SMID, trigger key, attribute value and time config. The cloud uses
 * this to dispatch bind commands to target devices in cross-device automations.
 *
 * @param report Trigger data (caller retains ownership of all pointers)
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreRequestSmartTriggerReport(const IOT_CoreSmartTriggerReport_t *report);

// ============================================================================
// Smart Sync-On-Off Report (subSmartType 71)
// ============================================================================

typedef struct
{
    uint16_t smid;
    uint16_t attrId;
    uint16_t attrSize;
    const uint8_t *attrValue;
} IOT_CoreSmartSyncOnOffReport_t;

/**
 * @brief Broadcast a sync-on-off activation to the location command topic.
 *
 * Carries the SMID, the new value, and this device's own root EID so peers can
 * suppress the echo of their own broadcast. Peers holding a sync-on-off trigger
 * with the same SMID apply the value to every element in their list.
 *
 * @return IOT_ERR_INVALID_ARG if report or attr value is missing/empty.
 */
iot_err_t IOT_CoreRequestSmartSyncOnOffReport(const IOT_CoreSmartSyncOnOffReport_t *report);

/**
 * @brief Legacy stair-switch propagation (old partner only) — mirror ONE on/off to the
 *        element the cloud paired with ours.
 *
 * This is NOT a general cross-device control, and deliberately cannot become one: there is
 * no attribute id and no value buffer, so the only thing it can express is the on/off
 * mirror that subSmartType 64 needs. The attribute it sends (313) and the frame shape are
 * fixed by the old partner's wire format.
 *
 * It unicasts to @p pairRootEid 's own command topic, not the location topic.
 *
 * A legacy compatibility layer. It is not compiled into the shipped library, where this
 * call is a stub returning IOT_ERR_NOT_SUPPORTED -- use sync-on-off (subSmartType 71).
 *
 * @param pairRootEid Device that receives the frame; 0 is rejected, having no topic.
 * @param pairEid     EID written into the frame's element block.
 * @param pairElemId  Element to drive on that device.
 * @param on          The value to mirror.
 * @return IOT_ERR_INVALID_ARG if @p pairRootEid is 0, IOT_ERR_NOT_SUPPORTED when the
 *         legacy layer is not compiled into this build.
 */
/* Always declared, and always a real symbol. Core supplies a stub returning
 * IOT_ERR_NOT_SUPPORTED when the legacy layer is not compiled in, so this header's shape
 * never depends on anyone's build configuration -- which is the whole point: a header is
 * read by every consumer, while the config is resolved once by whoever owns the build. */
iot_err_t IOT_CoreSmartStairSwitchMirror(uint16_t pairRootEid, uint16_t pairEid,
                                         uint16_t pairElemId, bool on);

// ============================================================================
// Distributed cond-group (AND/OR, subSmartType 2048)
// ============================================================================

// One group's identity + status.
typedef struct
{
    uint16_t smid;
    uint16_t eid;       // element/device EID this group's condition belongs to (identity block)
    uint16_t version;   // config generation (2 bytes on the wire)
    uint8_t condCount;  // N (<= 8)
    uint8_t fireCount;  // K
    uint8_t condIndex;  // this device's bit position (0..7)
    uint8_t mask;       // latched bitmask; the sender's OWN bit is (mask >> condIndex) & 1.
                        // Receivers consume only that one bit — the rest is not merged.
    // Set only on an ask-before-fire GET, where a pending fire is gated on the answer. A
    // reboot re-sync leaves it 0 so non-members stay silent.
    uint8_t askForFire;
    // Set only on the farewell a departing member broadcasts as it leaves a group; receivers
    // retire seat `condIndex` rather than adopting `condCount`.
    uint8_t departing;
} IOT_CoreSmartReportCondState_t;

/**
 * @brief Broadcast this device's cond-group report (own bit + version + N/K; mask carried).
 *
 * Carries this device's own root EID for echo suppression. The report carries the sender's
 * full latched mask, but a device is authoritative ONLY for its own bit — the merge
 * uses only the sender's own index and state (the mask field is not consumed). Peers merge via the
 * version rule (higher → adopt version + N/K, take the sender's bit; equal → take the
 * sender's bit; both keep their own bit).
 *
 * @return IOT_ERR_INVALID_ARG if report is NULL.
 */
iot_err_t IOT_CoreRequestSmartReportCondState(const IOT_CoreSmartReportCondState_t *report);

typedef struct
{
    const IOT_CoreSmartReportCondState_t *groups; // caller retains ownership
    uint16_t groupCount;
} IOT_CoreSmartSyncCondRequest_t;

/**
 * @brief Broadcast a reboot re-sync REQUEST (GET) — asks AND announces in one message.
 *
 * Carries one identity + status pair per group: the requester publishes its OWN
 * bitmask/version while asking. Peers
 * compare and stay SILENT when nothing differs; only a peer with a difference answers
 * (via IOT_CoreRequestSmartSyncCondResponse, unicast, jittered).
 *
 * @return IOT_ERR_INVALID_ARG if request is NULL, groups is NULL, or count is 0.
 */
iot_err_t IOT_CoreRequestSmartSyncCondRequest(const IOT_CoreSmartSyncCondRequest_t *request);

/**
 * @brief Answer a SYNC GET for a group this device is NOT in — UNICAST to the asker.
 *
 * Sent when the DAO holds no trigger for @p smid. This is what lets an asker tell a REMOVED
 * peer apart from an offline one: silence means offline and must never retire a seat, while
 * this reply is positive confirmation that the seat is gone.
 *
 * Carries the SMID and this device's root EID. The seat index is not carried — the
 * responder deleted its trigger and no longer knows it.
 *
 * @param targetEid the asker's EID.
 */
iot_err_t IOT_CoreRequestSmartCondNotMember(uint16_t smid, uint16_t targetEid);

/**
 * @brief Broadcast a SYNC PUSH (SET) — proactively announce after a cond-group BIND_TRIGGER.
 *
 * Same per-group framing, broadcast to the peers at this location. Used when a bind
 * changes this device's N/K/version so peers catch up without waiting for a natural
 * condition flip.
 *
 * @return IOT_ERR_INVALID_ARG if groups is NULL or count is 0.
 */
iot_err_t IOT_CoreRequestSmartSyncCondPush(const IOT_CoreSmartReportCondState_t *groups,
                                           uint16_t groupCount);

/**
 * @brief Answer a SYNC request — UNICAST to the asking device only.
 *
 * Routed to the asking device only, so the broker does not fan the answer out to the
 * whole location. Send only for groups where this device actually differs from the
 * request.
 *
 * @param targetEid the requester's EID (taken from the GET's identity block).
 * @return IOT_ERR_INVALID_ARG if reports is NULL or count is 0.
 */
iot_err_t IOT_CoreRequestSmartSyncCondResponse(const IOT_CoreSmartReportCondState_t *reports,
                                               uint16_t reportCount, uint16_t targetEid);

/* ============================
 *  SMART Command Response Enums
 * ============================ */

typedef enum {
    IOT_SMART_CMD_BIND_TRIGGER,
    IOT_SMART_CMD_UNBOUND_TRIGGER,
    IOT_SMART_CMD_TRIGGER_MODE,
    IOT_SMART_CMD_BIND_CMD,
    IOT_SMART_CMD_UNBOUND_CMD,
    IOT_SMART_CMD_ENABLE_DISABLE,
} IOT_SmartCmdType_t;

/* ============================
 *  SMART Command Response
 * ============================ */

/**
 * @brief Response data for DAO-completed SMART commands.
 *
 * Used for BIND_TRIGGER, UNBOUND_TRIGGER, TRIGGER_MODE, BIND_CMD, UNBOUND_CMD:
 * core answers with the cfm value on success, or the error code on failure. The
 * answer carries no body — the outcome is the whole reply.
 *
 * ENABLE_DISABLE is answered through this same call, but the cloud expects a
 * different shape for it (agreed with cloud 2026-08-25) and core builds that for
 * you: the answer carries this device's identity alongside the automation id, so
 * the cloud can attribute it to a device and an automation. Set `smid` for this
 * cmdType; `cfm` is unused.
 *
 * No-ops silently if ctx.requiresResponse is false. ENABLE_DISABLE additionally
 * no-ops on IOT_ERR_NOT_FOUND — a device that does not hold the SMID must not
 * answer at all, so one command yields one reply instead of one per device.
 *
 * @field cmdType  Which SMART command this response is for
 * @field smid     Smart automation ID — ENABLE_DISABLE only, ignored otherwise
 * @field cfm      Confirmation value (0 if not applicable, e.g. BIND_CMD/UNBOUND_CMD)
 * @field result   IOT_OK reports success, any other value reports failure
 * @field ctx      Routing context from the original event — pass through unchanged
 */
typedef struct {
    IOT_SmartCmdType_t   cmdType;
    uint16_t             smid;
    uint16_t             cfm;
    iot_err_t            result;
    IOT_RequestContext_t ctx;
} IOT_CoreSmartCmdResponse_t;

iot_err_t IOT_CoreRespondSmartCmd(const IOT_CoreSmartCmdResponse_t *response);

/* ============================
 *  CHECK_SYNC Response
 * ============================ */

/**
 * @brief Response data for SMART CHECK_SYNC command.
 *
 * Core answers with the stored trigger if exists=true.
 * Sends nothing if exists=false (silent by design for NOT_FOUND).
 * No-ops silently if ctx.requiresResponse is false.
 *
 * @field smid        Smart automation ID being synced
 * @field exists      false = trigger not stored locally, send nothing
 * @field triggerEid  Trigger source EID (only valid if exists=true)
 * @field enabled     Whether the trigger is currently enabled (only valid if exists=true)
 * @field cfm         Confirmation value (only valid if exists=true)
 * @field ctx         Routing context from the original event — pass through unchanged
 */
typedef struct {
    uint16_t             smid;
    bool                 exists;
    uint16_t             triggerEid;
    bool                 enabled;
    uint16_t             cfm;
    IOT_RequestContext_t ctx;
} IOT_CoreSmartCheckSyncResponse_t;

iot_err_t IOT_CoreRespondSmartCheckSync(const IOT_CoreSmartCheckSyncResponse_t *response);
#ifdef __cplusplus
}
#endif

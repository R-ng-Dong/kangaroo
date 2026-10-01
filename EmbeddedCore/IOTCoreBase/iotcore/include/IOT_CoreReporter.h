#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"

/* ============================
 *  GET_STATE Response
 * ============================ */

typedef struct
{
    uint16_t eid;
    devStateElemsInfo_t *fullState; // Full device state (SDK retains ownership)
    IOT_RequestContext_t ctx;       // Routing back to original sender
    uint8_t *unixTime;              // Time sync (NULL if not requested)
} IOT_CoreStateResponse_t;

/**
 * @brief Build and send a GET_STATE response message.
 *
 * Core builds the response from the provided device state, adds time sync
 * if unixTime is non-NULL, and sends to the requesting sender.
 *
 * @param response Device state + sender routing info (SDK retains ownership of all pointers)
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreRequestReportState(const IOT_CoreStateResponse_t *response);

/* ============================
 *  SENSOR_LOG Response
 * ============================ */

typedef struct
{
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    uint16_t year;
    uint16_t dayOfYear;
    uint8_t *logData;    // Raw log data (SDK retains ownership)
    uint16_t logDataLen; // 0 if no data found
    IOT_RequestContext_t ctx;
} IOT_CoreSensorLogResponse_t;

/**
 * @brief Build and send a SENSOR_LOG response message.
 *
 * Echoes request params, adds sort metadata, and includes log data.
 * Sends response even if logDataLen is 0 (empty log).
 *
 * @param response Sensor log query echo + log data (SDK retains ownership)
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreRequestSensorLogResponse(const IOT_CoreSensorLogResponse_t *response);

/* ============================
 *  State Change Report (SET, local control, smart notification)
 * ============================ */

/**
 * Per-device data for a state change report.
 *
 * @field eid             Endpoint ID of the device
 * @field mac             Device MAC address
 * @field macLen          Length of mac (typically 6 or 8)
 * @field devId           Cloud-assigned device ID
 * @field devIdLen        Length of devId
 * @field fullState       Complete device state — all elements and their attributes.
 *                        Core reads this to build the full-state dump and to look up
 *                        elemType for each element. SDK retains ownership; core only
 *                        reads during this call.
 * @field numChangedElems Number of elements whose state changed
 * @field changedElemIds  Array of element IDs that changed (length = numChangedElems)
 * @field changedAttrId   The attribute ID that was modified (same across all changed elements)
 * @field pushNotify      If true, cloud sends a push notification to the user's phone.
 *                        Typically true only for safety/alert events (smoke, tamper, low battery).
 */
typedef struct
{
    uint16_t eid;
    uint8_t *mac;
    uint8_t macLen;
    uint8_t *devId;
    uint8_t devIdLen;
    devStateElemsInfo_t *fullState;
    uint16_t numChangedElems;
    uint16_t *changedElemIds;
    uint16_t changedAttrId;
    bool pushNotify;
} IOT_CoreStateChangeDeviceData_t;

/**
 * State change report envelope — wraps one or more devices into a single MQTT report.
 *
 * @field devices    Array of per-device state change data
 * @field numDevices Number of devices in the array (>1 for batch/group commands)
 * @field ctx        Sender routing context (senderFrom, senderId, requestId, etc.).
 *                   For local control or smart automation triggers, leave senderFrom=0
 *                   and senderId=NULL.
 */
typedef struct
{
    IOT_CoreStateChangeDeviceData_t *devices;
    uint8_t numDevices;
    IOT_RequestContext_t ctx;
} IOT_CoreStateChangeReport_t;

/**
 * @brief Build and send a state change report message.
 *
 * Constructs a report with device identity (Phase 1), full state (Phase 2, if provided),
 * voice enrichment (if senderFrom is voice type), and state change blocks (Phase 3).
 * Supports batch: multiple devices in a single MQTT message.
 *
 * @param report Device data array + voice info (SDK retains ownership of all pointers)
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreRequestReportStateChanged(const IOT_CoreStateChangeReport_t *report);

/* ============================
 *  Device Status Report
 * ============================ */

/**
 * Device health/capability data reported to cloud.
 * Sent on MQTT reconnect and on cloud GET request.
 */
/**
 * @brief Device operating modes for IOT_CoreDeviceStatusData_t.protocolFlags[0].
 *
 * Wire values shared with the cloud and the mobile app. There is deliberately no
 * zero value — 0 is "unset", and reporting it means the app cannot classify the
 * device (it will not be shown as a hub).
 */
#define IOT_DEVICE_MODE_MASTERSLAVE  0x01
#define IOT_DEVICE_MODE_MASTER       0x02
#define IOT_DEVICE_MODE_SLAVE        0x03
#define IOT_DEVICE_MODE_INDEPENDENT  0x04  /**< stands alone; the usual value */

typedef struct
{
    const char *firmwareVersion;  // Firmware version string (e.g., "1.2.3")
    int64_t uptimeSeconds;        // Seconds since boot
    int64_t lastActivationTime;   // Unix timestamp of last state change (0 if unknown)
    /** What the cloud reads to decide what this device is:
     *  [mode, mesh, zigbee, matter, reserved, reserved].
     *
     *  mode   — one of IOT_DEVICE_MODE_*. Must be a defined value; 0 is not one,
     *           and a device reporting 0 is not recognised as a hub.
     *  mesh / zigbee / matter — 1 if this device can actually serve that
     *           transport, else 0. Do not set a flag for a transport the build
     *           cannot serve: the cloud will offer capabilities that then fail. */
    uint8_t protocolFlags[6];
    uint8_t ramUsagePercent;      // RAM usage 0-100%
    uint8_t nvsUsagePercent;      // NVS usage 0-100%
    uint32_t totalRamKB;          // Total RAM in KB
    uint32_t totalNvsKB;          // Total NVS in KB
} IOT_CoreDeviceStatusData_t;

/**
 * @brief Build and send a device status report message.
 *
 * Reports firmware version, uptime, last activation time, protocol capabilities
 * and memory stats to the cloud over MQTT.
 *
 * @param status Device status data (SDK retains ownership)
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreRequestReportDeviceStatus(const IOT_CoreDeviceStatusData_t *status);

/* ============================
 *  IR Keyset Request (to cloud)
 * ============================ */

/**
 * @brief Ask the cloud to push a sub-device's IR keyset.
 *
 * Sent at IR sub-device join so the cloud responds with the appliance's code book
 * (which the SDK stores via the IR STORE handler). Selector is the cloud device id.
 *
 * @param eid        Sub-device (appliance) EID whose keyset is needed
 * @param deviceType Appliance device type (carried in msgData)
 * @param devId      Cloud-assigned device id (selector; may be NULL)
 * @param devIdLen   Length of devId (0 if NULL)
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreRequestIrData(uint16_t eid, uint16_t deviceType, const uint8_t *devId, uint16_t devIdLen);

/* ============================
 *  Smart Trigger Report (to cloud)
 * ============================ */

/**
 * Data for a smart trigger activation report sent to cloud.
 * Matches WiLe's rgmgt_smart_trigger() message format.
 *
 * Cloud receives this and dispatches bind commands to target devices
 * (cross-device smart automation).
 */
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
 * uses only ownCondIndex/ownState (the mask field is not consumed). Peers merge via the
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

typedef enum {
    IOT_DEVICE_CMD_BIND_GRP,
    IOT_DEVICE_CMD_BIND_GRP_ELMS,
    IOT_DEVICE_CMD_UNBOUND_GRP,
    IOT_DEVICE_CMD_SYNC_DEV_REMOVED,
    IOT_DEVICE_CMD_SYNC_DEV_JOINED,
    IOT_DEVICE_CMD_DEVICE_ATTR,
} IOT_DeviceCmdType_t;

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

/* ============================
 *  DEVICE Command Response
 * ============================ */

/**
 * @brief Response data for DAO-completed DEVICE commands.
 *
 * Used for SYNC_DEV_JOINED, SYNC_DEV_REMOVED, BIND_GRP,
 * BIND_GRP_ELMS, UNBOUND_GRP, and DEVICE_ATTR.
 * Core answers success or failure, echoing cmdData2 back
 * (its meaning is command-dependent).
 * No-ops silently if ctx.requiresResponse is false.
 *
 * @field cmdType   Which DEVICE command this response is for
 * @field cmdData2  Raw echo value — meaning varies by command, for example sync step byte
 * @field result    IOT_OK reports success, any other value reports failure
 * @field ctx       Routing context from the original event — pass through unchanged
 */
typedef struct {
    IOT_DeviceCmdType_t  cmdType;
    uint8_t              cmdData2;
    iot_err_t            result;
    IOT_RequestContext_t ctx;
} IOT_CoreDeviceCmdResponse_t;

iot_err_t IOT_CoreRespondDeviceCmd(const IOT_CoreDeviceCmdResponse_t *response);

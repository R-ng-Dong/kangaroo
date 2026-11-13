#pragma once
#include <inttypes.h>

#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_CoreRecord.h"   /* IOT_CoreRecordStateBlock_t */

/*
 * Device and element state: GET_STATE answers, state-change reports, device status,
 * and the DEVICE command response.
 *
 * Split out of IOT_CoreReporter.h on 2026-10-06. That header remains as an umbrella that
 * includes this one, so existing includes keep working.
 */

/* ============================
 *  GET_STATE Response
 * ============================ */


#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    uint16_t eid;
    devStateElemsInfo_t *fullState; // Full device state (SDK retains ownership)
    IOT_RequestContext_t ctx;       // Routing back to original sender
    uint8_t *unixTime;              // Time sync (NULL if not requested)

    /* Each recorded attribute's CURRENT history block, so the app learns its first
     * checkpoint without a second round trip. Attached only when the request asked for
     * the time, and capped at IOT_CORE_RECORD_STATE_BLOCKS_MAX. NULL/0 is normal --
     * a device that records nothing, or has not synced its clock yet.
     *
     * Appended at the end on 2026-10-22: a partner's libiotcore.a was compiled against
     * the older layout. */
    const IOT_CoreRecordStateBlock_t *recordBlocks;
    uint8_t numRecordBlocks;
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
 * @brief Build and send an unsolicited device status report.
 *
 * Reports firmware version, uptime, last activation time, protocol capabilities
 * and memory stats to the cloud. Use it for the periodic report (e.g. after the
 * cloud connects). To answer DEV_EVENT_DEVICE_STATUS_REQUEST use
 * IOT_CoreRespondDeviceStatus() instead, which replies to whoever asked.
 *
 * @param status Device status data (SDK retains ownership)
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreRequestReportDeviceStatus(const IOT_CoreDeviceStatusData_t *status);

/**
 * @brief Answer a device status request.
 *
 * Sends the same status as IOT_CoreRequestReportDeviceStatus(), but as the reply
 * to DEV_EVENT_DEVICE_STATUS_REQUEST: on the transport the request arrived on
 * (BLE, LAN or cloud), to whoever asked, with the request's transaction id. Call
 * it from your handler, before returning, and write its return value into the
 * event's `data.deviceStatusRequest.result`.
 *
 * Blocks: briefly, while the message is sent. Task: your DEV_EVENT_DEVICE_STATUS_REQUEST
 * handler, on the task that delivers core events.
 *
 * @param ctx    The request's context, `data.deviceStatusRequest.ctx`, unchanged
 * @param status Device status data (SDK retains ownership)
 * @return IOT_OK when the reply was sent; IOT_ERR_INVALID_ARG for a NULL argument;
 *         otherwise the error that stopped it
 */
iot_err_t IOT_CoreRespondDeviceStatus(const IOT_RequestContext_t *ctx, const IOT_CoreDeviceStatusData_t *status);

/* ============================
 *  DEVICE Command Response Enum
 * ============================ */

typedef enum {
    IOT_DEVICE_CMD_BIND_GRP,
    IOT_DEVICE_CMD_BIND_GRP_ELMS,
    IOT_DEVICE_CMD_UNBOUND_GRP,
    IOT_DEVICE_CMD_SYNC_DEV_REMOVED,
    IOT_DEVICE_CMD_SYNC_DEV_JOINED,
    IOT_DEVICE_CMD_DEVICE_ATTR,
} IOT_DeviceCmdType_t;

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
#ifdef __cplusplus
}
#endif

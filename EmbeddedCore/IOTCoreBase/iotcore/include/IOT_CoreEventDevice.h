#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h"

/*
 * DEVICE events: provisioning, identification, WiFi, removal, groups, attributes, Zigbee joins.
 *
 * Split out of IOT_CoreEventHandler.h on 2026-10-14. That header remains the umbrella
 * (category enum, IOT_CoreEvent_t, callback registration) and includes this file, so
 * existing includes keep working. Prefer this header in new code that only needs it.
 */

/* ============================
 *  ZIGBEE DEVICE EVENT SIZES
 *  Mirrors IOT_ZigbeeCoordinator.h constants without a platform header dependency.
 * ============================ */

#ifdef __cplusplus
extern "C" {
#endif
#define IOT_CORE_ZIGBEE_IEEE_SIZE    8
#define IOT_CORE_ZIGBEE_MODEL_SIZE  32
#define IOT_CORE_ZIGBEE_VENDOR_SIZE 32

/* ============================
 *  DEVICE EVENTS
 * ============================ */
typedef enum
{
    DEV_EVENT_NONE,

    /* Internal lifecycle — no cloud response. */
    DEV_EVENT_IDENTIFICATION,
    DEV_EVENT_PROVISION_CANCELLED,
    DEV_EVENT_PROVISION_COMPLETED,
    DEV_EVENT_BOOTED,
    DEV_EVENT_WIFI_CONNECTED,
    DEV_EVENT_WIFI_DISCONNECTED,
    DEV_EVENT_CLOUD_CONNECTED,
    DEV_EVENT_CLOUD_DISCONNECTED,
    DEV_EVENT_WIFI_CONNECTING,
    DEV_EVENT_CLOUD_CONNECTING,
    DEV_EVENT_ERROR,

    /* GET — the status report itself is the response; no separate ack is needed. */
    DEV_EVENT_DEVICE_STATUS_REQUEST,

    /* DAO-completed — call IOT_CoreRespondDeviceCmd(&rsp) after DAO work.
     * For DEV_EVENT_NEW_DEVICE_JOINED: respond only for DEVICE_INFO and ELM_INFO phases
     * (ctx.requiresResponse encodes this — core sets it false for SYNC_COMPLETE and FULL paths). */
    DEV_EVENT_NEW_DEVICE_JOINED,
    DEV_EVENT_DEVICE_REMOVED,

    /* DAO-completed — update your group DAO, then call IOT_CoreRespondDeviceCmd(&rsp)
     * when ctx.requiresResponse is set. */
    DEV_EVENT_GROUP_BIND,
    DEV_EVENT_GROUP_UNBIND,

    /* DAO-completed — call IOT_CoreRespondDeviceCmd(&rsp) after DAO work. */
    DEV_EVENT_DEVICE_ATTR_SET,

    /* Zigbee sub-device events (hub role, CONFIG_IOT_ZIGBEE_ENABLED).
     * No EID at this stage — EID is assigned later via cloud round-trip. */
    DEV_EVENT_ZIGBEE_DEVICE_JOINING,      ///< Device joined network, interview pending
    DEV_EVENT_ZIGBEE_DEVICE_INTERVIEWED,  ///< Interview succeeded, known model
    DEV_EVENT_ZIGBEE_DEVICE_UNKNOWN_MODEL,///< Interview succeeded, unknown model
    DEV_EVENT_ZIGBEE_DEVICE_LEFT,         ///< Device left the network
} IOT_EventTypeDevice_t;

/**
 * @brief Device join sync phases. Core dispatches DEV_EVENT_NEW_DEVICE_JOINED
 *        once per phase during device sync.
 *
 * - DEVICE_INFO: Device identity + common info available. Save to DAO.
 * - ELM_INFO:    Element list with attribute IDs. Initialize element state in DAO.
 * - SYNC_COMPLETE: Sync finished. Persist mesh key if present.
 */
typedef enum {
    IOT_DEV_JOIN_PHASE_DEVICE_INFO,
    IOT_DEV_JOIN_PHASE_ELM_INFO,
    IOT_DEV_JOIN_PHASE_SYNC_COMPLETE
} IOT_DevJoinPhase_t;
typedef struct
{
    uint16_t elemId;
    uint16_t elemType;
    uint8_t numOfAttr;
    uint16_t *attrIdList; // Array of attribute IDs only; size/value from IOT_AttrDefines
} IOT_DevJoinElmInfo_t;

typedef struct
{
    IOT_DevJoinPhase_t phase;
    IOT_RequestContext_t ctx;     /**< Routing context — pass through to IOT_CoreRespondDeviceCmd */
    uint8_t cmdData2;             /**< Command data byte 2 — pass through to IOT_CoreRespondDeviceCmd */
    union
    {
        struct
        {
            uint8_t devId[12];
            uint8_t mac[6];
            uint8_t macType;
            uint16_t eid;
            uint16_t rootEid;
            uint16_t protocol;
            uint16_t nwkAddr;
            uint16_t manufacturer;
            uint16_t deviceType;
            uint16_t eleNum;
            uint16_t groupId;
        } deviceInfo;
        struct
        {
            uint16_t eid;
            uint8_t numOfElms;
            IOT_DevJoinElmInfo_t *elmList;
        } elmInfo;
        struct
        {
            uint16_t eid;
            /* Mesh device key, as an 18-byte TLV: block type, length, then the 16-byte
             * key. NOT a bare key, and NOT encrypted -- core hands mesh key material to
             * the SDK in the clear, because the SDK is its consumer and needs the
             * plaintext to join. (Until 2026-09-15 this was ciphertext, which only forced
             * a decrypt back across the core/SDK boundary.) NULL if no mesh key. */
            uint8_t *meshKey;
            uint16_t meshKeyLen; // 0 if no mesh key
        } syncComplete;
    } data;
} IOT_CoreDeviceEventNewDeviceJoinedData_t;

typedef struct {
    uint8_t statusCode; // 1 for completed, 0 for failed
} IOT_CoreDeviceEventProvisionCompletedData_t;

typedef struct
{
    // No additional data for now
} IOT_CoreDeviceEventIdentificationData_t;

typedef struct
{
    int errorCode;
} IOT_CoreDeviceEventErrorData_t;

typedef struct
{
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
} IOT_CoreDeviceEventWifiConnectedData_t;

typedef struct
{
    uint8_t reason; // IOT_NwkErrorReason_t from IOT_NetworkMgmt.h
} IOT_CoreDeviceEventWifiDisconnectedData_t;

typedef struct
{
    uint16_t eid;
    bool isRootDevice;
    IOT_RequestContext_t ctx;
    uint8_t cmdData2;
} IOT_CoreDeviceEventDeviceRemovedData_t;

typedef struct
{
    uint16_t eid;
    uint16_t newGroupId;
    uint16_t oldGroupId;   // 0 if no old group (BIND_GRP_ELMS path)
    uint8_t *elemData;     // Element bindings [elemId(2B)]... (NULL if none — means all elements)
    uint16_t elemDataLen;  // 0 if no element bindings
    IOT_RequestContext_t ctx;
    uint8_t cmdData2;      /**< Protocol metadata for response echo-back */
} IOT_CoreDeviceEventGroupBindData_t;

typedef struct
{
    uint16_t eid;
    uint16_t groupId;      // Group to unbind from
    IOT_RequestContext_t ctx;
    uint8_t cmdData2;      /**< Protocol metadata for response echo-back */
} IOT_CoreDeviceEventGroupUnbindData_t;

typedef struct
{
    uint16_t eid;
    uint16_t elm;
    uint16_t attrId;
    const uint8_t *attrData;
    uint16_t attrDataLen;
    IOT_RequestContext_t ctx;
} IOT_CoreDeviceEventAttrSetData_t;

typedef struct
{
    uint8_t ieeeAddr[IOT_CORE_ZIGBEE_IEEE_SIZE];
    char    friendlyName[IOT_CORE_ZIGBEE_MODEL_SIZE];
} IOT_CoreDeviceEventZigbeeJoiningData_t;

typedef struct
{
    uint8_t ieeeAddr[IOT_CORE_ZIGBEE_IEEE_SIZE];
    char    friendlyName[IOT_CORE_ZIGBEE_MODEL_SIZE];
    char    model[IOT_CORE_ZIGBEE_MODEL_SIZE];
    char    vendor[IOT_CORE_ZIGBEE_VENDOR_SIZE];
} IOT_CoreDeviceEventZigbeeInterviewedData_t;

typedef struct
{
    uint8_t ieeeAddr[IOT_CORE_ZIGBEE_IEEE_SIZE];
} IOT_CoreDeviceEventZigbeeLeftData_t;

typedef struct
{
    IOT_EventTypeDevice_t type;
    union
    {
        IOT_CoreDeviceEventIdentificationData_t identification;
        IOT_CoreDeviceEventProvisionCompletedData_t provisionCompleted;
        IOT_CoreDeviceEventNewDeviceJoinedData_t newDeviceJoined;
        IOT_CoreDeviceEventDeviceRemovedData_t deviceRemoved;
        IOT_CoreDeviceEventWifiConnectedData_t wifiConnected;
        IOT_CoreDeviceEventWifiDisconnectedData_t wifiDisconnected;
        IOT_CoreDeviceEventErrorData_t error;
        IOT_CoreDeviceEventGroupBindData_t groupBind;
        IOT_CoreDeviceEventGroupUnbindData_t groupUnbind;
        IOT_CoreDeviceEventAttrSetData_t attrSet;
        IOT_CoreDeviceEventZigbeeJoiningData_t zigbeeJoining;
        IOT_CoreDeviceEventZigbeeInterviewedData_t zigbeeInterviewed;
        IOT_CoreDeviceEventZigbeeLeftData_t zigbeeLeft;
    } data;
} IOT_CoreDeviceEvent_t;
#ifdef __cplusplus
}
#endif

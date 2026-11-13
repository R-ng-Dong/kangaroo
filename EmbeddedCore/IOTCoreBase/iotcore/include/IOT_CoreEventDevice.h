#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h"

/*
 * DEVICE events: provisioning, identification, network (WiFi, Ethernet), removal, groups, attributes, Zigbee joins.
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

    /** The cloud or the app asked the device to show itself (blink, beep). Only that request raises
     *  it: a phone connecting for setup is DEV_EVENT_PROVISION_STARTED. A REQUEST you claim: write
     *  `data.identification.result` (IOT_CoreDeviceEventIdentificationData_t). Core presets it to
     *  IOT_ERR_NOT_SUPPORTED, dispatches it on the task handling the request and answers the sender
     *  from it. */
    DEV_EVENT_IDENTIFICATION,
    /** The phone left before the setup finished: before it sent the WiFi details, while the device
     *  was still joining with them (a wrong password included), or straight after connecting without
     *  sending anything. Raised at once, with no timer. Ends a DEV_EVENT_PROVISION_STARTED. A phone
     *  that hangs up while the device waits for the cloud is routine and raises nothing
     *  (DEV_EVENT_PROVISION_FAILED or _COMPLETED ends that setup). Carries no data. Delivered on
     *  core's task; asks nothing back. */
    DEV_EVENT_PROVISION_CANCELLED,
    /** Setup succeeded: the device reached the cloud with the details it was given and is now
     *  provisioned. IOT_CoreIsProvisioned() is already true inside it, and it comes before the
     *  DEV_EVENT_CLOUD_CONNECTED of the session that completed it. Ends a DEV_EVENT_PROVISION_STARTED.
     *  Success only: a setup that gave up is DEV_EVENT_PROVISION_FAILED. Carries no data. Delivered on
     *  core's task; asks nothing back. */
    DEV_EVENT_PROVISION_COMPLETED,
    /** The first event of every normal boot, before any setup or network event. Sent once by
     *  IOT_CoreInit(), synchronously on the caller's task, once core's set-up has succeeded and before
     *  it goes online (WiFi, a network already up, or the setup advert); a failed IOT_CoreInit() sends
     *  none. IOT_CoreIsProvisioned() already holds this boot's answer. An update boot sends
     *  OS_EVENT_OTA_BOOT_STARTED instead. */
    DEV_EVENT_BOOTED,
    /** A network interface got an address: the device is on the network. `data.networkConnected`
     *  names the interface (WiFi or Ethernet) and the address. Also sent when core adopts a network
     *  that was already up at start. Delivered on core's task; asks nothing back. */
    DEV_EVENT_NETWORK_CONNECTED,
    /** A working link dropped: always after a DEV_EVENT_NETWORK_CONNECTED for the same interface.
     *  `data.networkDisconnected` names the interface and the reason. An attempt that never got an
     *  address is DEV_EVENT_NETWORK_CONNECT_FAILED instead. Delivered on core's task. */
    DEV_EVENT_NETWORK_DISCONNECTED,
    /** The cloud session is up: connected AND every topic subscription confirmed, so
     *  IOT_CoreIsCloudConnected() is already true inside this event and a send made from it is
     *  accepted. Sent once per established session, including each reconnect. Carries no data.
     *  Delivered on core's task; asks nothing back. */
    DEV_EVENT_CLOUD_CONNECTED,
    /** A working cloud session ended: it dropped, IOT_CoreCloudDisconnect() stopped it, or a new
     *  connect attempt replaced it. Always after a DEV_EVENT_CLOUD_CONNECTED. An attempt that never
     *  got that far is DEV_EVENT_CLOUD_CONNECT_FAILED instead. After a drop while a network is still
     *  up, a DEV_EVENT_CLOUD_CONNECTING follows for the reconnect core makes by itself (none after a
     *  stop). Carries no data. Delivered on core's task. */
    DEV_EVENT_CLOUD_DISCONNECTED,
    /** Core is joining the network (WiFi association started, before an address). Carries no data.
     *  Delivered on core's task. */
    DEV_EVENT_NETWORK_CONNECTING,
    /** Core is starting a cloud connect attempt: after the network comes up, on a retry, on
     *  IOT_CoreCloudConnect(), during setup, and for each reconnect core makes by itself after a
     *  DEV_EVENT_CLOUD_DISCONNECTED or DEV_EVENT_CLOUD_CONNECT_FAILED. Sent once per attempt; the
     *  attempt ends in one DEV_EVENT_CLOUD_CONNECTED or DEV_EVENT_CLOUD_CONNECT_FAILED. An attempt that
     *  a newer one replaces, or that IOT_CoreCloudDisconnect() stops, is ended with CONNECT_FAILED
     *  first. One exception: an attempt core held until the clock was set and that then could not
     *  start is ended only when the next attempt begins. Carries no data. Delivered on core's task. */
    DEV_EVENT_CLOUD_CONNECTING,
    DEV_EVENT_RESERVED_1, ///< never sent; value reserved

    /** The cloud or the app asked for this device's status. A REQUEST you claim: answer with
     *  IOT_CoreRespondDeviceStatus(&data.deviceStatusRequest.ctx, &status) and write its return into
     *  `data.deviceStatusRequest.result` (IOT_CoreDeviceEventStatusRequestData_t). That reply is the
     *  answer; core adds none on success, and answers failure with `result` otherwise (preset to
     *  IOT_ERR_NOT_SUPPORTED). */
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

    /* This device was reset by a call to IOT_CoreFactoryReset(). Core has already wiped its own
     * identity; this event is delivered on the caller's task before that call returns, and it
     * is NOT DEV_EVENT_DEVICE_REMOVED, which only the cloud's removal command raises. A product
     * wipes what it owns and restarts. No cloud response is owed. */
    DEV_EVENT_FACTORY_RESET,

    /* The WiFi-over-BLE recovery advert went on the air. Core raises it when the connection has
     * been lost for good (repeated reconnect failures) and the device now advertises so the
     * installer's app can hand it new WiFi credentials. Raised once per recovery episode: a
     * phone that connects and leaves while the outage lasts does not raise it again. A start
     * that was refused raises nothing. Delivered on core's task; carries no data; asks nothing
     * back. Use it to show "needs new WiFi" (an LED pattern, a screen message). */
    DEV_EVENT_WIFI_RECOVERY_STARTED,

    /* The recovery advert is no longer on the air: the cloud has been stable again, or the
     * advert could not be kept up after a client left. Raised once, and only for an episode
     * DEV_EVENT_WIFI_RECOVERY_STARTED announced. Delivered on core's task; carries no data;
     * asks nothing back. Use it to clear what STARTED showed. */
    DEV_EVENT_WIFI_RECOVERY_ENDED,

    /** An attempt to join the network failed: the interface was not up and did not come up (wrong
     *  password, access point not found, no stored credentials). `data.networkConnectFailed` names the
     *  interface and the reason. Sent once per failed attempt, so a device retrying WiFi sends one per
     *  retry; never a DEV_EVENT_NETWORK_DISCONNECTED, which means a working link dropped. Delivered on
     *  core's task; asks nothing back. */
    DEV_EVENT_NETWORK_CONNECT_FAILED,

    /** A cloud connect attempt failed: the session did not come up, came up and dropped before its
     *  subscriptions were confirmed, or was stopped or replaced before then. Ends the attempt a DEV_EVENT_CLOUD_CONNECTING announced, so
     *  it is sent once per failed attempt; never a DEV_EVENT_CLOUD_DISCONNECTED, which means a working
     *  session dropped. Core keeps trying while a network is up (each try announced by its own
     *  DEV_EVENT_CLOUD_CONNECTING). Carries no data. Delivered on core's task; asks nothing back. */
    DEV_EVENT_CLOUD_CONNECT_FAILED,

    /** A setup of this unprovisioned device began: a phone connected over BLE, or, for a setup over
     *  the local network (no phone on BLE), the moment it asked the device to reach for the cloud.
     *  Every STARTED ends in exactly
     *  one of DEV_EVENT_PROVISION_CANCELLED, DEV_EVENT_PROVISION_FAILED or
     *  DEV_EVENT_PROVISION_COMPLETED, so a "being set up" indication shown from it always has an end.
     *  A wrong WiFi password does not end it: the phone is told and may send new details on the same
     *  link. Never raised for a provisioned device. Carries no data. Delivered on core's task; asks
     *  nothing back. */
    DEV_EVENT_PROVISION_STARTED,

    /** Nothing worked within the setup window: 5 minutes after the last WiFi details the phone sent
     *  (a join that never succeeded), or the cloud never came. Ends a DEV_EVENT_PROVISION_STARTED. The
     *  device is not set up: core stops the cloud connection, drops the phone's BLE link and puts the
     *  setup advert back, so a retry is a new setup (a new STARTED); a cloud connect after this never
     *  completes it. Carries no data. Delivered on core's task; asks nothing back. */
    DEV_EVENT_PROVISION_FAILED,
} IOT_EventTypeDevice_t;

/** The network interface a network event is about. */
typedef enum
{
    IOT_CORE_NET_IFACE_WIFI = 0,
    IOT_CORE_NET_IFACE_ETHERNET = 1,
} IOT_CoreNetIface_t;

/** `reason` of DEV_EVENT_NETWORK_CONNECT_FAILED when core could not start the attempt at all (no
 *  stored credentials, or the platform refused the request), so no reason came from the network. */
#define IOT_CORE_NET_REASON_NOT_STARTED 0u

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
    uint16_t *attrIdList; // Array of attribute IDs only; sizes from the attribute table (IOT_ContractAttrSize())
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


/**
 * @brief Answer for DEV_EVENT_IDENTIFICATION.
 *
 * Core presets `result` to IOT_ERR_NOT_SUPPORTED, dispatches synchronously and reads it back when
 * your callback returns. Write IOT_OK once the device is identifying itself, or an error code if it
 * cannot. The sender is told success for IOT_OK and failure with that code otherwise; leaving it
 * untouched, or registering no IOT_EVENT_DEVICE handler, tells it the request is not supported.
 */
typedef struct
{
    iot_err_t result; /**< Your answer. Preset to IOT_ERR_NOT_SUPPORTED. */
} IOT_CoreDeviceEventIdentificationData_t;

/**
 * @brief The request behind DEV_EVENT_DEVICE_STATUS_REQUEST, and your answer to it.
 *
 * Reply with IOT_CoreRespondDeviceStatus(&ctx, &status): it goes back on the transport the request
 * arrived on, with its transaction id. Write the call's return into `result`. Core presets `result`
 * to IOT_ERR_NOT_SUPPORTED and reads it when your callback returns: IOT_OK means your reply was the
 * answer, anything else is answered as a failure with that code. `ctx` is valid only for the call:
 * reply before returning.
 */
typedef struct
{
    IOT_RequestContext_t ctx; /**< Who asked, and where the reply goes. Pass through unchanged. */
    iot_err_t result;         /**< Your answer. Preset to IOT_ERR_NOT_SUPPORTED. */
} IOT_CoreDeviceEventStatusRequestData_t;

/** @brief DEV_EVENT_NETWORK_CONNECTED: which interface came up, and its address. */
typedef struct
{
    IOT_CoreNetIface_t iface; /**< The interface that got the address. */
    uint32_t ip;              /**< Its IPv4 address. */
} IOT_CoreDeviceEventNetworkConnectedData_t;

/** @brief DEV_EVENT_NETWORK_DISCONNECTED: which working link dropped, and why. */
typedef struct
{
    IOT_CoreNetIface_t iface; /**< The interface that was up. */
    uint8_t reason;           /**< IOT_NwkErrorReason_t (network/IOT_NetworkMgmt.h); 0 when the platform gave none. */
} IOT_CoreDeviceEventNetworkDisconnectedData_t;

/** @brief DEV_EVENT_NETWORK_CONNECT_FAILED: which interface failed to come up, and why. */
typedef struct
{
    IOT_CoreNetIface_t iface; /**< The interface the attempt was on. */
    uint8_t reason;           /**< IOT_NwkErrorReason_t (network/IOT_NetworkMgmt.h), or
                                   IOT_CORE_NET_REASON_NOT_STARTED when the attempt never started. */
} IOT_CoreDeviceEventNetworkConnectFailedData_t;

typedef struct
{
    uint16_t eid;
    bool isRootDevice;
    IOT_RequestContext_t ctx;
    uint8_t cmdData2;
} IOT_CoreDeviceEventDeviceRemovedData_t;

typedef struct
{
    bool wasProvisioned; /**< The device was provisioned when the reset began. */
    uint16_t rootEid;    /**< This device's EID before the wipe; 0 only when it had none. */
} IOT_CoreDeviceEventFactoryResetData_t;

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
        IOT_CoreDeviceEventIdentificationData_t identification;   /**< DEV_EVENT_IDENTIFICATION */
        IOT_CoreDeviceEventStatusRequestData_t deviceStatusRequest; /**< DEV_EVENT_DEVICE_STATUS_REQUEST */

        IOT_CoreDeviceEventNewDeviceJoinedData_t newDeviceJoined;
        IOT_CoreDeviceEventDeviceRemovedData_t deviceRemoved;
        IOT_CoreDeviceEventFactoryResetData_t factoryReset;
        IOT_CoreDeviceEventNetworkConnectedData_t networkConnected;         /**< DEV_EVENT_NETWORK_CONNECTED */
        IOT_CoreDeviceEventNetworkDisconnectedData_t networkDisconnected;   /**< DEV_EVENT_NETWORK_DISCONNECTED */
        IOT_CoreDeviceEventNetworkConnectFailedData_t networkConnectFailed; /**< DEV_EVENT_NETWORK_CONNECT_FAILED */
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

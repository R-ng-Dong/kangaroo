#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"

/* ============================
 *  SENDER TYPES (for senderFrom field)
 * ============================ */

#define IOT_SENDER_UNKNOWN              0x00
#define IOT_SENDER_CONTROL              0x01
#define IOT_SENDER_APP                  0x0D
#define IOT_SENDER_CLOUD                0x0E
#define IOT_SENDER_IOS                  0x0F
#define IOT_SENDER_ANDROID              0x10
#define IOT_SENDER_DEVICE               0x11
#define IOT_SENDER_DEVICE_SYNC          0x12
#define IOT_SENDER_SCENARIO_UUID_V4     0x13
#define IOT_SENDER_AUTOMATION           0x14
#define IOT_SENDER_AUTOMATION_UUID_V4   0x15
#define IOT_SENDER_SCHEDULE             0x16
#define IOT_SENDER_SCHEDULE_UUID_V4     0x17
#define IOT_SENDER_COUNTDOWN            0x18
#define IOT_SENDER_GOOGLE_VOICE         0x20
#define IOT_SENDER_ALEXA                0x21
#define IOT_SENDER_OLLI                 0x22
#define IOT_SENDER_THIRD_PARTY          0x29

/* ============================
 *  GROUP & SENTINEL CONSTANTS
 * ============================ */

/** Group EID threshold — EIDs >= this value are group addresses, not device addresses */
#define IOT_GROUP_EID_BASE              0xC000
/** Secondary group address (auto-added during mesh provisioning) */
#define IOT_GROUP_EID_SECONDARY         0xC001
/** Invalid/unset sentinel for 16-bit IDs (EID, time, connection handle, etc.) */
#define IOT_INVALID_U16                 0xFFFF

/* ============================
 *  REQUEST CONTEXT
 * ============================ */

/** Transport protocol the request arrived on. */
typedef enum
{
    /** Unset — a context built without .protocol. Responses fall back to MQTT. */
    IOT_REQUEST_PROTOCOL_NONE = 0x00,
    IOT_REQUEST_PROTOCOL_BLE  = 0x01,
    IOT_REQUEST_PROTOCOL_TCP  = 0x02,
    IOT_REQUEST_PROTOCOL_MQTT = 0x03,
} IOT_RequestProtocol_t;

/**
 * @brief Routing/identity context for an incoming request.
 *
 * Bundles the sender routing fields needed to build a state response or
 * report. Carried by request events and reporter API structs.
 *
 * @note SDK usage is pass-through only: `.ctx = setData->ctx;` when building
 *       a report or response. The SDK should not read individual fields except
 *       for `senderFrom` which can be used for policy decisions (e.g. detecting
 *       automation/voice senders to prevent trigger loops).
 *
 * @field protocol        Transport the request arrived on (BLE, TCP, MQTT)
 * @field senderFrom    Sender type (IOT_SENDER_*)
 * @field senderId      Identity bytes (e.g., voice user ID, app UUID). NULL for local.
 * @field senderSize    Length of senderId. 0 when senderId is NULL.
 * @field tcpPort       TCP port for direct response routing (0 if not applicable)
 * @field tid           Transaction ID echoed back in responses
 * @field requestId     Optional cloud request correlation ID (NULL if none)
 * @field requestIdLen  Length of requestId. 0 when requestId is NULL.
 * @field requiresResponse  true if the sender expects a response; false = Reporter APIs no-op
 */
typedef struct
{
    IOT_RequestProtocol_t protocol; /**< Transport the request arrived on — used to route response correctly */
    uint8_t senderFrom;
    uint8_t senderSize;
    uint8_t *senderId;
    uint16_t tcpPort;
    uint16_t tid;
    uint8_t *requestId;
    uint16_t requestIdLen;
    bool requiresResponse; /**< false = sender expects no response; Reporter APIs no-op silently */
} IOT_RequestContext_t;

/* ============================
 *  DOMAIN TYPES
 * ============================ */

typedef struct
{
    uint16_t attrId;
    uint16_t attrSize;
    uint8_t *attrValue;
} elemStateAttr_t;

typedef struct
{
    uint16_t elemId;
    uint16_t elemType;
    uint8_t numOfAttr;
    elemStateAttr_t *attrList;
} devStateElemState_t;

typedef struct
{
    uint16_t numOfElem;
    devStateElemState_t *elemList;
} devStateElemsInfo_t;

/* ============================
 *  STATE EVENTS
 * ============================ */

typedef enum
{
    IOT_EVENT_TYPE_STATE_SET,
    IOT_EVENT_TYPE_STATE_GET,
    IOT_EVENT_TYPE_STATE_SENSOR_LOG,
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
 * carries a groupEid in CINF, core resolves it to LOCATION or GROUP instead.
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
            uint16_t groupEid; // >= IOT_GROUP_EID_BASE
        } group;
        struct
        {
            uint8_t *devId;     // 12-byte device ID from cloud
            uint16_t devIdLen;
            uint16_t groupEid;  // Group override from CINF (>= IOT_GROUP_EID_BASE), or 0 if none
        } voice;
    } target;
    dev_set_info_t devSetInfo;  // elemList, attrId, attrValue (eid field unused — use target instead)
    IOT_RequestContext_t ctx;
    uint8_t targetDeviceType;
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

typedef struct
{
    IOT_EventTypeState_t type;
    union
    {
        IOT_EventStateGetRequest_t getRequest;
        IOT_EventStateSetRequest_t setRequest;
        IOT_EventStateSensorLogRequest_t sensorLogRequest;
    } data;
} IOT_CoreStateEvent_t;

/* ============================
 *  ZIGBEE DEVICE EVENT SIZES
 *  Mirrors IOT_ZigbeeCoordinator.h constants without a platform header dependency.
 * ============================ */
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

    /* Parse-acknowledged in core; SDK updates group DAO fire-and-forget. */
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
            uint8_t *meshKey;    // Encrypted mesh device key (NULL if no mesh key)
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

/* ============================
 *  MESH EVENTS
 * ============================ */
typedef enum
{
    MESH_EVENT_NONE = 0,
    MESH_EVENT_NODE_JOINED, ///< A mesh node joined the network
    MESH_EVENT_NODE_LEFT,   ///< A mesh node left the network
    MESH_EVENT_MESSAGE,     ///< Inbound message from a mesh sub-node
    /** The hub itself joined the mesh network.
     *
     *  Raised after a successful join, whatever triggered it (boot, provisioning
     *  completion, or mesh keys arriving later). The SDK must respond by pushing
     *  the platform node cache — that data lives in the SDK-owned device DAO and
     *  core cannot build it. Without this the obligation is carried by statement
     *  order in IOT_SDK.c, which any new join trigger silently breaks. */
    MESH_EVENT_JOINED,

    /** Mesh network/application key delivered by the cloud.
     *
     *  Core parses the frame; the SDK decides what to do with it. This mirrors the
     *  per-child devKey handoff, which likewise parses in
     *  core and persists in the SDK — the mesh hub is SDK logic, so core must not
     *  own the keys it joins a network with.
     *
     *  `key` is owned by the dispatcher and is valid ONLY for the duration of the
     *  handler call — copy what you keep. */
    MESH_EVENT_KEYS_RECEIVED,
} IOT_EventTypeMesh_t;

typedef struct
{
    uint16_t nodeAddr; ///< Mesh unicast address of the node
} IOT_CoreMeshEventNodeData_t;

/** Which mesh key the cloud delivered. Mirrors the wire's key section extras. */
typedef enum
{
    IOT_MESH_KEY_NETWORK = 0,
    IOT_MESH_KEY_APPLICATION = 1,
} IOT_MeshKeyKind_t;

/**
 * Inbound mesh message.
 *
 * The opcode is normalised by the platform to an IOT_MESH_OPCODE_* value, so no
 * stack-specific framing reaches the SDK. `payload` is owned by the dispatcher
 * and is valid ONLY for the duration of the handler call — copy what you keep.
 */
typedef struct
{
    uint32_t       opcode;
    uint16_t       srcAddr;    ///< mesh element address of the sender
    uint16_t       payloadLen;
    const uint8_t *payload;
} IOT_CoreMeshEventMsgData_t;

/**
 * A mesh key delivered by the cloud.
 *
 * `keyIndex` is carried through verbatim from the wire (cmdData1) rather than
 * being assumed to be 0. Core does not interpret it; the SDK decides how many
 * subnets it can represent.
 */
typedef struct
{
    IOT_MeshKeyKind_t kind;     ///< network or application key
    uint8_t           keyIndex; ///< key index as sent by the cloud
    const uint8_t    *key;      ///< encrypted key bytes, dispatcher-owned
    uint16_t          keyLen;
} IOT_CoreMeshEventKeysData_t;

typedef struct
{
    IOT_EventTypeMesh_t type;
    union
    {
        IOT_CoreMeshEventNodeData_t node;    ///< Valid for MESH_EVENT_NODE_JOINED/LEFT
        IOT_CoreMeshEventMsgData_t  message; ///< Valid for MESH_EVENT_MESSAGE
        IOT_CoreMeshEventKeysData_t keys;    ///< Valid for MESH_EVENT_KEYS_RECEIVED
    } data;
} IOT_CoreMeshEvent_t;

/* ============================
 *  SETTING EVENTS
 * ============================ */
typedef enum
{
    SETTING_EVENT_WIFI_CHANGED,
} IOT_EventTypeSetting_t;

typedef struct
{
    const char *ssid;
} IOT_CoreSettingWifiChangedData_t;

typedef struct
{
    IOT_EventTypeSetting_t type;
    union
    {
        IOT_CoreSettingWifiChangedData_t wifiChanged;
    } data;
} IOT_CoreSettingEvent_t;

/* ============================
 *  SMART EVENTS
 * ============================ */
typedef enum
{
    /* Broadcast commands — sent to all devices in the location; only the device that
     * owns the smid should respond. Send nothing (no Reporter call) if smid not found locally.
     * TODO: implement execution-completed Reporter calls for these (currently generic notify). */
    SMART_EVENT_ACTIVE_BY_USER,
    SMART_EVENT_ACTIVE_BY_TRIGGER,
    SMART_EVENT_ACTIVE_BY_SCHEDULE,
    SMART_EVENT_REMOVE_ANNOUNCE,
    SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE,
    SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE,
    SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE,

    /* DAO-completed — call IOT_CoreRespondSmartCmd(&rsp) after DAO work. */
    SMART_EVENT_TRIGGER_MODE,
    SMART_EVENT_ENABLE_DISABLE,
    SMART_EVENT_BIND_TRIGGER,
    SMART_EVENT_UNBOUND_TRIGGER,
    SMART_EVENT_BIND_CMD,
    SMART_EVENT_UNBOUND_CMD,

    /* DAO-completed — call IOT_CoreRespondSmartCheckSync(&rsp) after DAO read. */
    SMART_EVENT_CHECK_SYNC,

    /* Broadcast — location-topic sync-on-off (subSmartType 71). Receivers apply
     * the ON/OFF value to EVERY element in their trigger's elemList; no Reporter
     * response. */
    SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF,

    /* Distributed cond-group (AND/OR, subSmartType 2048) with config versioning.
     * REPORT_COND_STATE: a peer reports its own bit + version + N/K (+ its full mask,
     * carried but not consumed) — merge via the version rule (also reused for each report
     * inside a SYNC response). SYNC_COND_REQUEST:
     * a peer's reboot re-sync query (smid list) — answer with our reports. No Reporter
     * response event (the completing device fires the existing ACTIVE_BY_TRIGGER). */
    SMART_EVENT_REPORT_COND_STATE,
    SMART_EVENT_SYNC_COND_REQUEST,
    /* COND_NOT_MEMBER: a peer answered our SYNC GET with "I hold no trigger for this SMID".
     * Positive confirmation that a seat is gone, as opposed to silence, which only means the
     * peer is offline and must never retire anything. */
    SMART_EVENT_COND_NOT_MEMBER,
} IOT_EventTypeSmart_t;

typedef struct {
    uint16_t smid;        // the group the responder is NOT a member of
    uint16_t responderEid; // who answered (for logging/diagnosis only)
} IOT_CoreSmartEventCondNotMemberData_t;

typedef struct {
    uint16_t ownerEid;
    uint16_t ownerEidCount;
    uint16_t extEid;
    uint16_t extEidCount;
} IOT_CoreSmartBlockTriggerKey_t;

typedef struct {
    // No additional data for now
    uint16_t minuteOfWeekScheduleUTC0;
    uint16_t smid;
    uint8_t *uuidv4;
    uint16_t uuidv4Len;
} IOT_CoreSmartEventActiveByScheduleData_t;
typedef struct {
    uint16_t attrId;
    uint16_t attrSize;
    uint8_t *attrValue;
} IOT_CoreSmartBlockAttrValue_t;
typedef struct {
    uint16_t timCfgType;
    uint16_t timCfgValue;
} IOT_CoreSmartBlockTimeConfig_t;
typedef struct {
    // No additional data for now
    uint16_t smid;
    uint16_t triggerType;
    // triggerKey
    IOT_CoreSmartBlockTriggerKey_t triggerKey;
    // attrValue
    IOT_CoreSmartBlockAttrValue_t attrValue;
    // time config
    IOT_CoreSmartBlockTimeConfig_t timeConfig;
    uint8_t activeSection;
    uint8_t controlCount;
    // TODO: continue
} IOT_CoreSmartEventActiveByTriggerData_t;

/* Sync-on-off (subSmartType 71) broadcast payload. Receivers apply the ON/OFF
 * value to every element in their trigger's elemList. */
typedef struct {
    uint16_t smid;
    IOT_CoreSmartBlockAttrValue_t attrValue;
    uint16_t senderEid; /* root EID of the broadcasting device; 0 = absent (old sender) */
} IOT_CoreSmartEventActiveBySyncOnOffData_t;

/* Distributed cond-group (AND/OR, subSmartType 2048) with config versioning.
 * The sender's authoritative own bit is read OUT of `mask` at `condIndex`
 * ((mask >> condIndex) & 1) — there is no separate ownState field, and the remaining bits
 * of `mask` are NOT consumed by the merge (a device owns only its own bit).
 * Used by REPORT_COND_STATE and by every group inside a sync request, push or response. */
typedef struct {
    uint16_t smid;
    uint16_t eid;           /* element/device EID the condition belongs to (identity block) */
    uint16_t senderRootEid; /* sender's root EID (identity block) — echo suppression; 0 = absent */
    uint16_t version;       /* sender's config generation for this smid (2 bytes on the wire) */
    uint8_t condCount;      /* N (<= 8) */
    uint8_t fireCount;      /* K */
    uint8_t condIndex;      /* sender's bit position (0..7) */
    uint8_t mask;           /* sender's latched bitmask; own bit = (mask >> condIndex) & 1 */
    /* Set only on an ask-before-fire GET: a fire is GATED on this answer. A reboot re-sync
     * leaves it 0 — nothing is waiting, so peers that are not members stay silent instead of
     * each emitting a unicast per absent SMID. Device-to-device only; the cloud is not a
     * participant in cond-state synchronisation. */
    uint8_t askForFire;
    /* Set on the farewell a departing member broadcasts: "seat condIndex is leaving". Receivers
     * retire that seat rather than adopting condCount — cloud does not renumber seats, so after
     * a middle seat leaves the survivors' indices no longer fit inside the announced N. */
    uint8_t departing;
} IOT_CoreSmartCondStatus_t;

/* REPORT_COND_STATE carries exactly one group's status. */
typedef IOT_CoreSmartCondStatus_t IOT_CoreSmartEventReportCondStateData_t;

/* SYNC request (GET): the rebooting device asks AND announces in one message — each group
 * carries the requester's OWN status, so a peer can compare and stay silent when nothing
 * differs. Answer (only on a difference) is unicast back to requesterEid. */
typedef struct {
    IOT_CoreSmartCondStatus_t *groups; /* handler-owned array; freed after dispatch returns */
    uint16_t groupCount;
    uint16_t requesterEid; /* where to unicast the answer (identity block); 0 = absent */
} IOT_CoreSmartEventSyncCondRequestData_t;

typedef struct {
    uint16_t eid;
    uint16_t rootEid;
    uint16_t elemId;
    uint16_t elemType;
    // trigger key
    IOT_CoreSmartBlockTriggerKey_t triggerKey;
} IOT_CoreSmartEventTriggerUpdateAnnounceData_t;

typedef enum {
    IOT_CORE_SMART_TRIGGER_MODE_NONE,
    IOT_CORE_SMART_TRIGGER_MODE_SET_TRIGGER = 1,
    IOT_CORE_SMART_TRIGGER_MODE_SCHEDULE = 2,
    IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_AUTOMATION = 10,
    IOT_CORE_SMART_TRIGGER_MODE_ENABLE_DISABLE_ALL_AUTOMATION = 255
} IOT_CoreSmartTriggerModeType_t;

typedef struct {
    IOT_CoreSmartTriggerModeType_t type;
    uint8_t mode;
    union {
        struct {
            uint16_t minuteDisable;
            uint16_t smid;
        } setTrigger;
        struct {
            uint16_t smid;
        } schedule;
        struct {
            uint16_t eid;
        } enableDisableAutomation;
    } data;
    IOT_RequestContext_t ctx;
} IOT_CoreSmartEventTriggerModeData_t;
typedef struct {
    uint16_t smid;
    uint8_t  enabled;   /* 1 = enable, 0 = disable */
    IOT_RequestContext_t ctx;
} IOT_CoreSmartEventEnableDisableData_t;
typedef struct {
    uint16_t type;
    uint16_t smid;
    uint16_t smartType;
    uint16_t subSmartType;
    uint16_t eid;
    uint16_t elmId;
    uint16_t condition;
    uint16_t cfm;
} IOT_CoreSmartDataBlockTriggerElmInfo_t;
typedef struct {
    // No additional data for now
    bool isDeviceUtc;
    float timeZoneFraction; // if isDeviceUtc == false, this value is ignored
    // bindTriggerKey
    IOT_CoreSmartBlockTriggerKey_t bindTriggerKey;
    // triggerElmInfo
    IOT_CoreSmartDataBlockTriggerElmInfo_t bindTriggerElmInfo;
    IOT_CoreSmartBlockAttrValue_t primaryAttrValue;
    // ext elm info
    uint16_t elmId;
    uint16_t condition;
    IOT_CoreSmartBlockAttrValue_t secondaryAttrValue;
    // trigger mix
    uint16_t eidMix;
    uint16_t rootEidMix;
    uint16_t elemIdMix;
    IOT_CoreSmartBlockTimeConfig_t timeConfigMix;
    // time job
    uint16_t timeStart;
    uint16_t timeStop;
    uint8_t weekDay; // bitmask: bit0=Sun..bit6=Sat, 0x00 = all days
    IOT_RequestContext_t ctx;
    // cond-group — hasCondGroup=0 when the bind carries no group
    uint8_t hasCondGroup;
    uint8_t condIndex; // this device's bit in the group (0..7)
    uint8_t condCount; // N: total conditions in the group (max 8)
    uint8_t fireCount; // K: fire when >= K of N are true
    uint16_t condVersion; // cloud-assigned config generation (2 bytes on the wire)
} IOT_CoreSmartEventBindTriggerData_t;
typedef struct {
    uint16_t eid;
    uint16_t smid;
    IOT_RequestContext_t ctx;
    // cond-group — the shape the group has AFTER this removal, when the sender supplies it.
    // The unbind is addressed to the removed device ONLY, so this is the sole copy of the new
    // N/K anywhere on the wire: the remaining members are never told. hasCondGroup=0 when the
    // unbind carries no cond-group shape (an ordinary automation, or an older sender).
    uint8_t hasCondGroup;
    uint8_t condCount;    // N after the removal
    uint8_t fireCount;    // K after the removal
    uint16_t condVersion; // cloud-assigned config generation (2 bytes on the wire)
} IOT_CoreSmartEventUnboundTriggerData_t;
typedef struct {
    uint16_t smid;
    uint16_t *scheduleTimes;     // Array of minute-of-week values (UTC0). Caller frees after dispatch.
    uint16_t scheduleTimeCount;  // Number of entries in scheduleTimes
} IOT_CoreSmartEventScheduleUpdateAnnounceData_t;
typedef struct {
    uint16_t smid;
} IOT_CoreSmartEventScheduleRemoveAnnounceData_t;
typedef struct {
    IOT_CoreSmartBlockAttrValue_t attrValue;
    uint16_t reverse;
    uint16_t delay;
    uint16_t elmId;
} IOT_CoreSmartBlockAttrValueReverseDelayElm_t;
typedef struct {
    uint16_t smid;
    uint16_t eid;
    uint16_t rootEid;
    uint16_t prtc;
    uint16_t filter;
    uint16_t cmdSize;
    char *linkedId;
    IOT_CoreSmartBlockAttrValueReverseDelayElm_t *attrValueList;
    IOT_RequestContext_t ctx;
} IOT_CoreSmartEventBindCmdData_t;

typedef struct {
    uint16_t smid;
    uint16_t eid;
    IOT_RequestContext_t ctx;
} IOT_CoreSmartEventUnboundCmdData_t;

typedef struct {
    uint16_t smid;
    IOT_RequestContext_t ctx;
} IOT_CoreSmartEventCheckSyncData_t;
typedef struct {
    uint16_t smid;
} IOT_CoreSmartEventRemoveAnnounceData_t;
typedef struct {
    uint16_t smid;
} IOT_CoreSmartEventActiveByUserData_t;

typedef struct
{
    IOT_EventTypeSmart_t type;
    union
    {
        IOT_CoreSmartEventRemoveAnnounceData_t removeAnnounce;
        IOT_CoreSmartEventActiveByUserData_t activeByUser;
        IOT_CoreSmartEventActiveByScheduleData_t activeBySchedule;
        IOT_CoreSmartEventActiveByTriggerData_t activeByTrigger;
        IOT_CoreSmartEventTriggerUpdateAnnounceData_t triggerUpdateAnnounce;
        IOT_CoreSmartEventTriggerModeData_t triggerMode;
        IOT_CoreSmartEventEnableDisableData_t enableDisable;
        IOT_CoreSmartEventBindTriggerData_t bindTrigger;
        IOT_CoreSmartEventUnboundTriggerData_t unboundTrigger;
        IOT_CoreSmartEventScheduleUpdateAnnounceData_t scheduleUpdateAnnounce;
        IOT_CoreSmartEventScheduleRemoveAnnounceData_t scheduleRemoveAnnounce;
        IOT_CoreSmartEventUnboundCmdData_t unboundCmd;
        IOT_CoreSmartEventBindCmdData_t bindCmd;
        IOT_CoreSmartEventCheckSyncData_t checkSync;
        IOT_CoreSmartEventActiveBySyncOnOffData_t activeBySyncOnOff;
        IOT_CoreSmartEventReportCondStateData_t reportCondState;
        IOT_CoreSmartEventCondNotMemberData_t condNotMember;
        IOT_CoreSmartEventSyncCondRequestData_t syncCondRequest;
    } data;
} IOT_CoreSmartEvent_t;

/* ============================
 *  OS EVENT
 * ============================ */
typedef enum
{
    OS_EVENT_REBOOT,
    /** Cloud asked for a firmware update. iotcore has parsed the command, acked it,
     *  and persisted the request — but it does NOT download. The SDK owns the
     *  download and decides when to run it (now, later, or after a reboot into a
     *  stripped-down mode with more contiguous heap). */
    OS_EVENT_OTA_REQUESTED,
    /** This boot exists only to run a previously-requested OTA download.
     *  IOT_CoreInit() found a pending OTA record on entry, so it ran its OTA boot
     *  path itself instead of the normal one: BLE was released and WiFi/MQTT are
     *  already up by the time this dispatches. (BLE release itself can be refused
     *  -- IOT_CoreBleDeinit() returns IOT_ERR_INVALID_STATE while a mesh hub role
     *  owns the radio -- and core logs and continues in OTA mode rather than
     *  failing the boot over it; but nothing sets that role before this point in a
     *  boot, so in practice BLE is always down here. The check stays for whatever
     *  future boot ordering might change that.)
     *
     *  THIS EVENT MUST BE CLAIMED, not merely received. An SDK that acts on it
     *  sets `data.otaBootStarted.handled = true` (see below) before returning
     *  from its callback, then skips the rest of its own initialisation and
     *  starts the download. A registered IOT_EVENT_OS callback that does
     *  something else with this event type -- including a partner's existing
     *  handler that only knows OS_EVENT_REBOOT and falls through a `default`
     *  case without touching `handled` -- leaves the flag false. Core checks it:
     *  IOT_DevCoreDispatchEvent() returning IOT_OK is NOT by itself proof anyone
     *  is running this download, because that same IOT_OK is what an
     *  IOT_EVENT_OS handler registered for a completely different reason (e.g.
     *  OS_EVENT_REBOOT, which every partner since the 08_29 sync already
     *  handles) also returns while ignoring this one. Core therefore requires
     *  BOTH the dispatch to return IOT_OK AND `handled` to be true; anything
     *  else -- no handler registered at all (IOT_ERR_NOT_FOUND), or a handler
     *  registered that did not set the flag -- is treated identically: core
     *  discards the pending OTA request (IOT_CoreOtaDiscardPending()) and
     *  reboots itself, rather than let the SDK fall through into a boot with no
     *  BLE and no OTA either. Falling through un-rebooted would be worse: BLE
     *  stays released with nothing to bring it back, so every later boot finds
     *  the same record still pending, takes the OTA path again, and never has
     *  BLE again -- permanently losing provisioning and the change-WiFi
     *  fallback. The self-reboot trades that for "this OTA request did not
     *  happen", which is recoverable. (If even discarding the record fails --
     *  e.g. NVS erase failure -- core does not reboot into that same unresolved
     *  state either: it tries to bring BLE back up in place instead, so the
     *  device is not left both OTA-stuck and BLE-less.) Register the handler AND
     *  set the flag when you claim the download -- do not rely on receiving this
     *  event, or a nonzero dispatch result, as proof of anything. */
    OS_EVENT_OTA_BOOT_STARTED,
} IOT_CoreOsEventType_t;

/**
 * @brief OTA request handed to the SDK.
 *
 * Pointers are BORROWED — valid only for the duration of the synchronous
 * dispatch, same convention as IOT_CoreIrEvent_t. To act on the request later
 * (e.g. after a reboot), call IOT_CoreOtaGetPending() instead of stashing these.
 *
 * The simplest correct handler passes the fields straight to the platform
 * downloader together with core's own progress callback, which emits every
 * protocol message for you:
 *
 *     IOT_AppManagerStartUpdateProcess(d->url, d->cert, d->certLen,
 *                                      d->authToken, d->authTokenLen,
 *                                      NULL, IOT_CoreOtaPlatformProgressCb);
 */
typedef struct
{
    const char *url;        /**< Full URL including scheme. Never NULL. */
    const char *authToken;  /**< Bearer token, or NULL when the server needs none. */
    uint16_t authTokenLen;  /**< 0 when authToken is NULL. */
    const char *cert;       /**< CA cert PEM provisioned for HTTPS, or NULL if unset. */
    uint16_t certLen;       /**< 0 when cert is NULL. */
    uint8_t autoUpdate;     /**< Cloud's auto-update preference, already persisted by core. */
} IOT_CoreOsEventOtaRequestedData_t;

/**
 * @brief Acknowledgement for OS_EVENT_OTA_BOOT_STARTED.
 *
 * Core sets `handled = false` before dispatching and reads it back (dispatch is
 * synchronous) immediately after IOT_DevCoreDispatchEvent() returns. Set it to
 * true from your IOT_EVENT_OS callback to claim this download -- this is how
 * core tells "an SDK is genuinely running the download" apart from "an SDK
 * happened to be registered for a different OS event and returned IOT_OK while
 * ignoring this one" (see OS_EVENT_OTA_BOOT_STARTED above). Leaving it false
 * (not registering a handler at all, or registering one that does not set it)
 * makes core discard the pending request and reboot to a normal boot.
 */
typedef struct
{
    bool handled; /**< Set true to claim the download. Read by core after dispatch returns. */
} IOT_CoreOsEventOtaBootStartedData_t;

typedef struct
{
    IOT_CoreOsEventType_t type;
    union
    {
        IOT_CoreOsEventOtaRequestedData_t otaRequested;         /**< Valid for OS_EVENT_OTA_REQUESTED */
        IOT_CoreOsEventOtaBootStartedData_t otaBootStarted;     /**< Valid for OS_EVENT_OTA_BOOT_STARTED */
    } data;
} IOT_CoreOsEvent_t;

/* ============================
 *  IR EVENT
 * ============================ */
/**
 * @brief IR command dispatched to the SDK.
 *
 * The inline-EXEC path: iotcore populates this event with the payload (borrowed
 * pointers valid only for the synchronous dispatch) and dispatches it. The SDK-layer handler decompresses/transmits
 * and writes `result` back (read after IOT_DevCoreDispatchEvent returns, since
 * dispatch is synchronous). QUERY_CAPABILITY is how core answers the cloud when it
 * asks what IR this device can do: the SDK fills capMode/capDetect from the
 * app-declared capability.
 */
typedef enum
{
    IOT_IR_EVENT_EXEC_RAW,          ///< raw timing pairs in `data`: send verbatim
    IOT_IR_EVENT_EXEC_RAW_ZIP,      ///< compressed timing data: decompress then send
    IOT_IR_EVENT_EXEC_PRTC,         ///< protocol id + payload: send via IR protocol encoder
    IOT_IR_EVENT_QUERY_CAPABILITY,  ///< cloud asked what IR we support: SDK fills capMode/capDetect
    IOT_IR_EVENT_STORE_KEYSET,      ///< STORE_DATA: packed keyset for `eid` in data/dataLen → IR DAO
    IOT_IR_EVENT_EXEC_AC,           ///< EXEC_PRTC_AC: differential AC; brand in `protocol`, state in ac* fields
} IOT_EventTypeIr_t;

typedef struct
{
    IOT_EventTypeIr_t type;
    /* Inputs — borrowed pointers, valid only during synchronous dispatch. */
    uint16_t eid;        ///< target sub-device EID (STORE_KEYSET)
    uint16_t protocol;   ///< IR protocol id (EXEC_PRTC)
    uint8_t *header;     ///< rawzip header (EXEC_RAW_ZIP)
    uint16_t headerLen;
    uint8_t *data;       ///< raw blob / zip data / prtc code bytes / packed keyset (STORE_KEYSET)
    uint16_t dataLen;
    /* Outputs — written by the SDK handler, read by iotcore after dispatch. */
    iot_err_t result;    ///< EXEC/STORE result reported back to the cloud
    uint8_t capMode;     ///< QUERY_CAPABILITY: 0=TX only, 1=RX only, 2=TX&RX
    uint8_t capDetect;   ///< QUERY_CAPABILITY: 0=no detect, 1=detect supported
    /* EXEC_AC (differential AC): brand is carried in `protocol` above; state below.
     * Raw fields (not IOT_AcState_t) so this core header stays free of the IR platform type. */
    uint8_t acPower;     ///< EXEC_AC: 0=off, 1=on
    uint8_t acMode;      ///< EXEC_AC: iot_attr_mode_t
    uint8_t acTemp;      ///< EXEC_AC: target temperature, degrees Celsius
    uint8_t acFan;       ///< EXEC_AC: iot_attr_fan_speed_t
    uint8_t acSwing;     ///< EXEC_AC: swing position/mode
} IOT_CoreIrEvent_t;

/* ============================
 *  MAIN EVENT WRAPPER
 * ============================ */
typedef enum
{
    IOT_EVENT_STATE,
    IOT_EVENT_DEVICE,
    IOT_EVENT_MESH,
    IOT_EVENT_SETTING,
    IOT_EVENT_SMART,
    IOT_EVENT_OS,
    IOT_EVENT_IR,
} IOT_CoreEventCategory_t;

typedef struct
{
    IOT_CoreEventCategory_t category;
    union
    {
        IOT_CoreStateEvent_t state;
        IOT_CoreDeviceEvent_t device;
        IOT_CoreMeshEvent_t mesh;
        IOT_CoreSettingEvent_t setting;
        IOT_CoreSmartEvent_t smart;
        IOT_CoreOsEvent_t os;
        IOT_CoreIrEvent_t ir;
    } evt;
} IOT_CoreEvent_t;

/* ============================
 *  CALLBACK
 * ============================ */

/**
 * @brief Event callback function type
 * @param arg User-defined argument passed during registration
 * @param event Pointer to the event structure
 */
typedef void (*IOT_CoreEventCb_t)(void *arg, const IOT_CoreEvent_t *event);

/**
 * @brief Register an event callback for a specific category
 * @param category Event category to register for
 * @param callback Callback function to be invoked when events occur
 * @param arg User argument to pass to the callback
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG if parameters are invalid,
 *         IOT_ERR_INVALID_STATE if callback already registered for this category
 */
iot_err_t IOT_DevCoreRegisterEventCb(IOT_CoreEventCategory_t category, IOT_CoreEventCb_t callback, void *arg);

/**
 * @brief Unregister an event callback for a specific category
 * @param category Event category to unregister
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG if category is invalid,
 *         IOT_ERR_NOT_FOUND if no callback is registered for this category
 */
iot_err_t IOT_DevCoreUnregisterEventCb(IOT_CoreEventCategory_t category);

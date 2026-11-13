#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h"

/*
 * SMART events: triggers, schedules, sync-on-off, condition groups, enable/disable, bind/unbind.
 *
 * Split out of IOT_CoreEventHandler.h on 2026-10-14. That header remains the umbrella
 * (category enum, IOT_CoreEvent_t, callback registration) and includes this file, so
 * existing includes keep working. Prefer this header in new code that only needs it.
 */

/* ============================
 *  SMART EVENTS
 * ============================ */

#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    /* Broadcast commands — sent to all devices in the location; only the device that
     * owns the smid should act. Core expects no answer to any of them: run the automation, or do
     * nothing if the smid is not found locally. Delivered on core's task as part of a request;
     * every pointer in the data is borrowed (valid only until the handler returns). */
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

    /* Broadcast — location-wide sync-on-off (subSmartType 71). Receivers apply
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
    uint16_t minuteOfWeekScheduleUTC0; /**< the slot to run: minute of the week (0..10079), UTC */
    uint16_t smid;
    /** Identity bytes of the sender that asked for this run (the id the request came from, e.g.
     *  an app or cloud user). It is NOT a schedule or automation identifier; the SMID and the
     *  minute above are the key. Borrowed: valid only until the handler returns. */
    uint8_t *uuidv4;
    uint16_t uuidv4Len; /**< length of uuidv4 in bytes; 0 when absent */
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
    /* attrValue.attrValue is borrowed: valid only until the handler returns. */
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
    uint16_t version;       /* sender's config generation for this smid (16-bit) */
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
    uint16_t condVersion; // cloud-assigned config generation (16-bit)
} IOT_CoreSmartEventBindTriggerData_t;
typedef struct {
    uint16_t eid;
    uint16_t smid;
    IOT_RequestContext_t ctx;
    // cond-group — the shape the group has AFTER this removal, when the sender supplies it.
    // The unbind is addressed to the removed device ONLY, so this is the sole copy of the new
    // N/K anywhere in the message: the remaining members are never told. hasCondGroup=0 when the
    // unbind carries no cond-group shape (an ordinary automation, or an older sender).
    uint8_t hasCondGroup;
    uint8_t condCount;    // N after the removal
    uint8_t fireCount;    // K after the removal
    uint16_t condVersion; // cloud-assigned config generation (16-bit)
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
#ifdef __cplusplus
}
#endif

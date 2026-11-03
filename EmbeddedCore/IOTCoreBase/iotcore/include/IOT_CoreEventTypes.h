#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"

/*
 * Shared event vocabulary: sender types, group and sentinel constants, IOT_RequestContext_t, domain types.
 *
 * Split out of IOT_CoreEventHandler.h on 2026-10-14. That header remains the umbrella
 * (category enum, IOT_CoreEvent_t, callback registration) and includes this file, so
 * existing includes keep working. Prefer this header in new code that only needs it.
 */

/* ============================
 *  SENDER TYPES (for senderFrom field)
 * ============================ */


#ifdef __cplusplus
extern "C" {
#endif
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

/** Group EID base. */
/* 0xC000 is the LOCATION address ("all devices"), NOT the first group — despite the
 * name. Real groups start at 0xC001. Core reports 0xC000 as IOT_SET_TARGET_LOCATION.
 * Read IOT_EventStateSetRequest_t.targetType rather than comparing EIDs yourself. */
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
#ifdef __cplusplus
}
#endif

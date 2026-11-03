#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h"

/*
 * MESH events: node reports, relayed messages, key delivery.
 *
 * Split out of IOT_CoreEventHandler.h on 2026-10-14. That header remains the umbrella
 * (category enum, IOT_CoreEvent_t, callback registration) and includes this file, so
 * existing includes keep working. Prefer this header in new code that only needs it.
 */

/* ============================
 *  MESH EVENTS
 * ============================ */

#ifdef __cplusplus
extern "C" {
#endif
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
     *  core cannot build it. Handle it here rather than after any one join trigger,
     *  so every way of joining is covered. */
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
 * `keyIndex` is carried through verbatim from the cloud rather than
 * being assumed to be 0. Core does not interpret it; the SDK decides how many
 * subnets it can represent.
 */
typedef struct
{
    IOT_MeshKeyKind_t kind;     ///< network or application key
    uint8_t           keyIndex; ///< key index as sent by the cloud
    const uint8_t    *key;      ///< key bytes in the clear, dispatcher-owned
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
#ifdef __cplusplus
}
#endif

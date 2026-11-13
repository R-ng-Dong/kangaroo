#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h"

/*
 * MESH events: key delivery.
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
    /* Slots 1..4 held events core never raised; they stay reserved so the
     * key event keeps its value. */
    MESH_EVENT_RESERVED_1,
    MESH_EVENT_RESERVED_2,
    MESH_EVENT_RESERVED_3,
    MESH_EVENT_RESERVED_4,

    /** Mesh network/application key delivered by the cloud.
     *
     *  Core parses the frame; the SDK decides what to do with it. This mirrors the
     *  per-child devKey handoff, which likewise parses in
     *  core and persists in the SDK — the mesh hub is SDK logic, so core must not
     *  own the keys it joins a network with.
     *
     *  `key` is owned by the dispatcher and is valid ONLY for the duration of the
     *  handler call — copy what you keep.
     *
     *  A REQUEST the cloud waits on: core answers the cloud from `data.keys.result` (see
     *  IOT_CoreMeshEventKeysData_t). The keys reach every device during setup, so the preset is
     *  IOT_OK: a device that keeps no mesh keys accepts and ignores them. */
    MESH_EVENT_KEYS_RECEIVED,
} IOT_EventTypeMesh_t;

/** Which mesh key the cloud delivered. Mirrors the key kinds the cloud distinguishes. */
typedef enum
{
    IOT_MESH_KEY_NETWORK = 0,
    IOT_MESH_KEY_APPLICATION = 1,
} IOT_MeshKeyKind_t;

/**
 * A mesh key delivered by the cloud, and your answer to it.
 *
 * `keyIndex` is carried through verbatim from the cloud rather than
 * being assumed to be 0. Core does not interpret it; the SDK decides how many
 * subnets it can represent.
 *
 * `result` is your answer. Core presets it to IOT_OK, dispatches synchronously and
 * reads it when your callback returns. The keys are sent to every device during
 * setup, and the phone abandons the setup if one is refused, so a device that keeps
 * no mesh keys (no IOT_EVENT_MESH handler, or one that ignores the key) answers
 * success. A mesh hub that tries to store the key and cannot writes the error code;
 * the cloud is then told failure with that code.
 */
typedef struct
{
    IOT_MeshKeyKind_t kind;     ///< network or application key
    uint8_t           keyIndex; ///< key index as sent by the cloud
    const uint8_t    *key;      ///< key bytes in the clear, dispatcher-owned
    uint16_t          keyLen;
    iot_err_t         result;   ///< Your answer. Preset to IOT_OK.
} IOT_CoreMeshEventKeysData_t;

typedef struct
{
    IOT_EventTypeMesh_t type;
    union
    {
        IOT_CoreMeshEventKeysData_t keys;    ///< Valid for MESH_EVENT_KEYS_RECEIVED
    } data;
} IOT_CoreMeshEvent_t;
#ifdef __cplusplus
}
#endif

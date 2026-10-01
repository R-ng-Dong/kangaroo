#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"

// ============================================================================
// BLE Mesh Management Interface
// ============================================================================
// Abstract interface for BLE Mesh node lifecycle, persistence, message routing,
// and topology events. This device acts as a mesh NODE (not provisioner) —
// credentials are pushed from cloud, the device joins with stored keys.
//
// Data-plane send operations (OnOff, CTL, HSL, Vendor) are in IOT_MeshControl.h.
//
// Implementation: implplatform/esp32/mesh/IOT_MeshMgmt.c

// ============================================================================
// Lifecycle
// ============================================================================

/**
 * @brief Initialize BLE Mesh stack and restore persisted network state.
 * Must be called once before any mesh operations. Safe to call multiple times.
 *
 * Implementation should:
 * - Load persisted mesh sequence number and IV index from NVS
 * - Register BLE mesh stack callbacks (provisioning, config server, models)
 * - Initialize the mesh stack with node composition
 * - Enable node provisioning (PB-ADV + PB-GATT)
 * - If previously provisioned, call IOT_MeshJoinNetwork() to rejoin
 *
 * Note: Node cache is loaded by SDK after init via IOT_MeshSetNodeCache().
 *
 * @return IOT_OK on success
 */
iot_err_t IOT_MeshInit(void);

/**
 * @brief Deinitialize BLE Mesh and release resources.
 * @return IOT_OK on success
 */
iot_err_t IOT_MeshDeinit(void);

/**
 * @brief Check if the mesh stack is initialized and network is joined.
 * @return true if mesh is ready for send/receive operations
 */
bool IOT_MeshIsProvisioned(void);

// ============================================================================
// Network Join (Node Role)
// ============================================================================

/**
 * @brief Mesh network credentials for joining.
 * Built by iotcore from config DAO (mesh keys + root device config).
 * Passed to platform — platform never accesses DAO directly.
 */
typedef struct
{
    uint8_t netKey[16];
    uint8_t appKey[16];
    uint8_t devKey[16];
    uint16_t netIdx;
    uint16_t appIdx;
    uint16_t unicastAddr;
    uint32_t ivIndex;
    uint16_t groupAddr; // primary group (e.g., 0xC000)
} IOT_MeshCredentials_t;

/**
 * @brief Join or rejoin the mesh network with provided credentials.
 * This device acts as a mesh NODE — credentials are loaded and decrypted
 * by iotcore from config DAO, then pushed here.
 *
 * Non-blocking — spawns a task internally.
 * Called by iotcore after IOT_MeshInit() if previously provisioned,
 * or after cloud pushes mesh keys via the MESH protocol handler.
 *
 * @param credentials Network credentials (netKey, appKey, devKey, addresses)
 * @return IOT_OK if network join started successfully
 */
iot_err_t IOT_MeshJoinNetwork(const IOT_MeshCredentials_t *credentials);

// ============================================================================
// Node Management
// ============================================================================

/**
 * @brief Send a node reset command to a mesh sub-device.
 * Called when the cloud removes a sub-device. Sends NODE_RESET opcode
 * to the mesh node so it leaves the network and erases its own credentials.
 *
 * @param nodeAddr Mesh unicast address of the node to remove
 * @param devKey 16-byte device key of the node (for encrypted config message)
 * @return IOT_OK on success
 */
iot_err_t IOT_MeshDeleteNode(uint16_t nodeAddr, const uint8_t *devKey);

// ============================================================================
// Persistence (Sequence & IV Index)
// ============================================================================

/**
 * @brief Persist current mesh sequence number and IV index to NVS.
 * MUST be called after every mesh send to prevent replay attacks after reboot.
 * Implementation may batch writes (e.g., write only when seq changes by N).
 *
 * @return IOT_OK on success
 */
iot_err_t IOT_MeshSaveSequence(void);

// ============================================================================
// Node Cache & Key Lookup
// ============================================================================

/** Entry for mesh node cache — provided by SDK */
typedef struct
{
    uint16_t addr;       // mesh unicast address
    uint8_t devKey[16];  // device key
} IOT_MeshNodeEntry_t;

/**
 * @brief Set the mesh node cache (replaces previous cache).
 * SDK iterates its device registry, builds entries for mesh nodes, and pushes here.
 * Platform stores the array and uses it for devKey lookups + dedup.
 *
 * @param entries Array of node entries (copied internally)
 * @param count Number of entries
 * @return IOT_OK on success
 */
iot_err_t IOT_MeshSetNodeCache(const IOT_MeshNodeEntry_t *entries, uint8_t count);

/**
 * @brief Look up a sub-node's device key by its mesh address.
 * Searches the in-memory node cache set by IOT_MeshSetNodeCache().
 *
 * @param nodeAddr Mesh unicast address
 * @return Pointer to 16-byte device key, or NULL if not found
 */
const uint8_t *IOT_MeshGetDevKey(uint16_t nodeAddr);

// ============================================================================
// Incoming Message Callback
// ============================================================================

/**
 * @brief Callback for incoming mesh vendor messages from remote nodes.
 * @param opcode Vendor model opcode
 * @param srcAddr Source mesh address of the sender
 * @param payload Raw message payload
 * @param payloadLen Payload length in bytes
 */
/**
 * Normalised opcodes delivered to IOT_MeshMessageCb_t.
 *
 * The platform strips the stack's vendor company/CID framing before invoking the
 * callback, so upper layers never see an ESP-BLE-MESH composite opcode or a
 * stack-specific status struct.
 */
#define IOT_MESH_OPCODE_ONOFF_STATUS   0x8204u /**< SIG Generic OnOff Status; payload = [present_onoff] */
#define IOT_MESH_OPCODE_VND_CONTROL    0x00E4u /**< Rogo vendor CONTROL  */
#define IOT_MESH_OPCODE_VND_SENSOR     0x00E5u /**< Rogo vendor SENSOR   */
#define IOT_MESH_OPCODE_VND_SENSOR_ACK 0x00E6u /**< Rogo vendor SENSOR+ACK */

typedef void (*IOT_MeshMessageCb_t)(uint32_t opcode, uint16_t srcAddr, const uint8_t *payload, uint16_t payloadLen);

/**
 * @brief Register a callback for incoming mesh vendor messages.
 * The implementation routes received vendor model messages to this callback.
 * @param cb Callback function
 * @return IOT_OK on success
 */
/**
 * @note `payload` is valid ONLY for the duration of the call — the callee must
 *       copy anything it needs. The callback runs on the BLE mesh stack task, so
 *       it must not block or do NVS/network work; defer instead.
 */
iot_err_t IOT_MeshRegisterMessageCb(IOT_MeshMessageCb_t cb);

// ============================================================================
// Topology Events
// ============================================================================

typedef enum
{
    IOT_MESH_NODE_JOINED, // A mesh node joined the network
    IOT_MESH_NODE_LEFT,   // A mesh node left the network
} IOT_MeshTopologyEventType_t;

typedef struct
{
    IOT_MeshTopologyEventType_t type;
    uint16_t nodeAddr; // Mesh network address (element address) of the node
} IOT_MeshTopologyEvent_t;

/**
 * @brief Callback invoked when mesh network topology changes.
 * @param event Topology event with type and node address
 */
typedef void (*IOT_MeshTopologyCb_t)(const IOT_MeshTopologyEvent_t *event);

/**
 * @brief Register a callback for mesh topology changes.
 * Called by the SDK layer during init to receive node join/leave notifications.
 * @param cb Callback function
 * @return IOT_OK on success
 */
iot_err_t IOT_MeshRegisterTopologyCb(IOT_MeshTopologyCb_t cb);

/**
 * @brief Notify that a mesh topology change occurred.
 * Called by the platform implementation when the BLE mesh stack detects
 * a node joining or leaving the network.
 * @param event Topology event
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if no callback registered
 */
iot_err_t IOT_MeshNotifyTopologyChange(const IOT_MeshTopologyEvent_t *event);

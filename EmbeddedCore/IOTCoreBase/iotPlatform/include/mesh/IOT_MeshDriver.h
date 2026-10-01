#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "mesh/IOT_MeshMgmt.h"
#include "mesh/IOT_MeshControl.h"

// ============================================================================
// Mesh Driver Registration
// ============================================================================
// Optional BLE Mesh feature uses a driver registration pattern.
// When libiotmesh.a is linked, the ESP32 mesh driver auto-registers via constructor.
// When not linked, IOT_Mesh*() functions return IOT_ERR_NOT_SUPPORTED.

typedef struct
{
    // Lifecycle
    iot_err_t (*init)(void);
    iot_err_t (*deinit)(void);
    bool (*isProvisioned)(void);

    // Network
    iot_err_t (*joinNetwork)(const IOT_MeshCredentials_t *credentials);
    iot_err_t (*deleteNode)(uint16_t nodeAddr, const uint8_t *devKey);

    // Persistence
    iot_err_t (*saveSequence)(void);

    // Node cache
    iot_err_t (*setNodeCache)(const IOT_MeshNodeEntry_t *entries, uint8_t count);
    const uint8_t *(*getDevKey)(uint16_t nodeAddr);

    // Callbacks
    iot_err_t (*registerMessageCb)(IOT_MeshMessageCb_t cb);
    iot_err_t (*registerTopologyCb)(IOT_MeshTopologyCb_t cb);
    iot_err_t (*notifyTopologyChange)(const IOT_MeshTopologyEvent_t *event);

    // Control (send)
    iot_err_t (*sendOnOff)(uint16_t nwkAddr, uint8_t onoff, uint16_t appIdx);
    iot_err_t (*sendLightCTL)(uint16_t nwkAddr, uint16_t lightness, uint16_t temperature, uint16_t appIdx);
    iot_err_t (*sendLightHSL)(uint16_t nwkAddr, uint16_t hue, uint16_t saturation, uint16_t lightness, uint16_t appIdx);
    iot_err_t (*sendVendor)(uint16_t deviceType, uint16_t nwkAddr, uint8_t *data, uint16_t dataLen);
} IOT_MeshDriver_t;

/**
 * @brief Register a mesh driver implementation.
 * Called by platform-specific mesh code (e.g., ESP32 BLE Mesh) to provide
 * the actual hardware implementation. Typically called from a constructor.
 *
 * @param driver Pointer to driver function table (must be static/global lifetime)
 */
void IOT_MeshRegisterDriver(const IOT_MeshDriver_t *driver);

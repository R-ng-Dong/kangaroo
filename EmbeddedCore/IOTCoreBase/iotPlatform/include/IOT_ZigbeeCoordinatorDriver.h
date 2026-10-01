#pragma once

#include "IOT_ErrorManager.h"
#include "IOT_ZigbeeCoordinator.h"

// ============================================================================
// Zigbee Coordinator Driver Registration
// ============================================================================
// Optional Zigbee feature uses a driver registration pattern.
// When the platform Zigbee impl is linked, it auto-registers via constructor.
// When not linked, IOT_ZigbeeCoordinator*() functions return IOT_ERR_NOT_SUPPORTED.

typedef struct
{
    iot_err_t (*init)(const IOT_ZigbeeCoordinatorCallbacks_t *cbs);
    iot_err_t (*deinit)(void);
    iot_err_t (*openPairing)(uint16_t gatewayEid, uint8_t timeSec, uint8_t mode, uint16_t deviceTypeFilter);
    iot_err_t (*closePairing)(uint16_t gatewayEid);
} IOT_ZigbeeCoordinatorDriver_t;

/**
 * @brief Register a Zigbee coordinator driver implementation.
 * Called by platform-specific Zigbee code to provide the actual implementation.
 * Typically called from a constructor.
 *
 * @param driver Pointer to driver function table (must have static/global lifetime)
 */
void IOT_ZigbeeCoordinatorRegisterDriver(const IOT_ZigbeeCoordinatorDriver_t *driver);

#pragma once
#include "ble/IOT_BleMgmt.h"
#include "IOT_ErrorManager.h"

// ============================================================================
// BLE Driver Registration
// ============================================================================
// BLE uses a driver registration pattern identical to mesh and Zigbee.
// When a platform BLE implementation is compiled in, it auto-registers via
// __attribute__((constructor)). When not compiled, IOT_Ble*() functions
// return IOT_ERR_NOT_SUPPORTED.

typedef struct
{
    iot_err_t (*init)(void);
    iot_err_t (*deinit)(void);
    iot_err_t (*addServices)(const IOT_BleMgmtGattServ_t *service);
    iot_err_t (*startAdvertising)(IOT_BleMgmtAdvData_t *adv);
    iot_err_t (*stopAdvertising)(void);
    iot_err_t (*startBeaconAdvertising)(IOT_BleMgmtAdvData_t *adv);
    iot_err_t (*stopBeaconAdvertising)(void);
    iot_err_t (*notifyToCharacteristic)(const char *addr, const uint8_t *charUuid,
                                        const uint8_t *data, uint16_t len);
    iot_err_t (*isConnected)(bool *out);
    iot_err_t (*disconnect)(void);
    iot_err_t (*startScan)(uint32_t intervalMs);
    iot_err_t (*stopScan)(void);
    iot_err_t (*setDeviceName)(const char *name);
    iot_err_t (*registerEventCallback)(IOT_BleMgmtEventStatusType_t event,
                                       IOT_BleEventCb_t cb, void *arg);
    iot_err_t (*unregisterEventCallback)(IOT_BleMgmtEventStatusType_t event);
} IOT_BleDriver_t;

/**
 * @brief Register a BLE driver implementation.
 * Called by platform-specific BLE code to provide the actual hardware
 * implementation. Typically called from a constructor.
 *
 * @param driver Pointer to driver function table (must have static/global lifetime)
 */
void IOT_BleRegisterDriver(const IOT_BleDriver_t *driver);

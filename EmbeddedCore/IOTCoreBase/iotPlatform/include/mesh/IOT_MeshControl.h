#pragma once
#include <inttypes.h>
#include "IOT_ErrorManager.h"

// ============================================================================
// BLE Mesh Transport Interface
// ============================================================================
// Abstract mesh send operations. Values use BLE Mesh native ranges (0-65535).
// Caller (your SDK) handles value scaling from Rogo ranges to mesh ranges.
//
// Implementation: implplatform/esp32/mesh/IOT_MeshControl.c

/**
 * @brief Send Generic OnOff Set Unacknowledged
 * @param nwkAddr Mesh network address (element address)
 * @param onoff 0=OFF, 1=ON
 * @param appIdx Application key index
 */
iot_err_t IOT_MeshSendOnOff(uint16_t nwkAddr, uint8_t onoff, uint16_t appIdx);

/**
 * @brief Send Light CTL Set Unacknowledged (brightness + color temperature)
 * @param nwkAddr Mesh network address
 * @param lightness Lightness (0-65535)
 * @param temperature Color temperature (mesh range)
 * @param appIdx Application key index
 */
iot_err_t IOT_MeshSendLightCTL(uint16_t nwkAddr, uint16_t lightness, uint16_t temperature, uint16_t appIdx);

/**
 * @brief Send Light HSL Set Unacknowledged (hue + saturation + lightness)
 * @param nwkAddr Mesh network address
 * @param hue Hue (0-65535)
 * @param saturation Saturation (0-65535)
 * @param lightness Lightness (0-65535)
 * @param appIdx Application key index
 */
iot_err_t IOT_MeshSendLightHSL(uint16_t nwkAddr, uint16_t hue, uint16_t saturation, uint16_t lightness,
                                uint16_t appIdx);

/**
 * @brief Send Vendor Model control message
 * @param deviceType Device type for payload formatting
 * @param nwkAddr Mesh network address
 * @param data Vendor payload data
 * @param dataLen Payload length
 */
iot_err_t IOT_MeshSendVendor(uint16_t deviceType, uint16_t nwkAddr, uint8_t *data, uint16_t dataLen);

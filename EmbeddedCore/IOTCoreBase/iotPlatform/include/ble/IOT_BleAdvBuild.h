/**
 * @file IOT_BleAdvBuild.h
 * @brief Shared BLE advertising / scan-response payload builder
 *
 * Packs an IOT_BleMgmtAdvData_t into raw AD (Advertising Data) elements, so every
 * BLE backend (Bluedroid, NimBLE, BlueZ) emits the same bytes and shares one set of
 * bounds checks against the 31-byte legacy advertising limit.
 *
 * Advertising payload layout:
 *   [02 01 06]                       Flags: LE General Discoverable, BR/EDR not supported
 *   [len 03 lo hi ...]               Complete list of 16-bit service UUIDs (optional)
 *   [len FF cid_lo cid_hi data...]   Manufacturer Specific Data (optional)
 *
 * Scan-response layout:
 *   [len 09 name...]                 Complete Local Name (optional)
 */
#pragma once

#include "IOT_ErrorManager.h"
#include "ble/IOT_BleMgmt.h"
#include <stdint.h>

/** Maximum length of a legacy BLE advertising or scan-response payload */
#define IOT_BLE_ADV_MAX_LEN 31

/** AD types used by the builder */
#define IOT_BLE_AD_TYPE_FLAGS         0x01
#define IOT_BLE_AD_TYPE_UUID16_CMPL   0x03
#define IOT_BLE_AD_TYPE_NAME_CMPL     0x09
#define IOT_BLE_AD_TYPE_MANUFACTURER  0xFF

/** Flags value: LE General Discoverable Mode | BR/EDR Not Supported */
#define IOT_BLE_AD_FLAGS_LE_GENERAL   0x06

/**
 * @brief Build the advertising payload for the given advertising data
 *
 * @param adv     Advertising data to pack
 * @param buf     Destination buffer
 * @param bufSize Size of buf; must be at least the packed length
 * @param outLen  Receives the number of bytes written
 *
 * @return IOT_OK on success
 * @return IOT_ERR_INVALID_ARG if any pointer is NULL, or the payload does not fit
 */
iot_err_t IOT_BleAdvBuildPayload(const IOT_BleMgmtAdvData_t *adv, uint8_t *buf, uint8_t bufSize, uint8_t *outLen);

/**
 * @brief Build the scan-response payload (Complete Local Name) for the given advertising data
 *
 * Writes nothing and reports a length of 0 when no name is set — callers should skip
 * configuring a scan response in that case.
 *
 * @param adv     Advertising data to pack
 * @param buf     Destination buffer
 * @param bufSize Size of buf; must be at least the packed length
 * @param outLen  Receives the number of bytes written (0 when there is no name)
 *
 * @return IOT_OK on success
 * @return IOT_ERR_INVALID_ARG if any pointer is NULL, or the name does not fit
 */
iot_err_t IOT_BleAdvBuildScanRsp(const IOT_BleMgmtAdvData_t *adv, uint8_t *buf, uint8_t bufSize, uint8_t *outLen);

#pragma once
#include "IOT_ErrorManager.h"
#include <inttypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Reserved dataName for the shared rawzip header of a device. */
#define IOT_IR_HEADER_DATANAME 65534

// ============================================================================
// Protocol-based IR codes (stored as u64)
// ============================================================================

/**
 * @brief Retrieve a protocol-based IR code
 * @param eid Device endpoint ID
 * @param dataName Button/command index
 * @param outCode Output: 64-bit IR protocol code
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if no code stored
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoIRGetProtocolCode(uint16_t eid, uint16_t dataName, uint64_t *outCode);

// ============================================================================
// Rawzip header (one per device, shared by all buttons)
// ============================================================================

/**
 * @brief Retrieve the rawzip decompression header
 * @param eid Device endpoint ID
 * @param outHeader Output: heap-allocated header bytes (caller must free)
 * @param outHeaderLen Output: header length
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if no header stored
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoIRGetRawZipHeader(uint16_t eid, uint8_t **outHeader, uint16_t *outHeaderLen);

// ============================================================================
// Rawzip data blobs (one per button/command)
// ============================================================================

/**
 * @brief Retrieve compressed rawzip IR data
 * @param eid Device endpoint ID
 * @param dataName Button/command index
 * @param outData Output: heap-allocated data bytes (caller must free)
 * @param outDataLen Output: data length
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if no data stored
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoIRGetRawZipData(uint16_t eid, uint16_t dataName, uint8_t **outData, uint16_t *outDataLen);

#ifdef __cplusplus
}
#endif

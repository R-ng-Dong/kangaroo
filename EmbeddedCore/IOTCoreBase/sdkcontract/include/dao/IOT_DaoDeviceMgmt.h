#pragma once
#include "dao/IOT_DaoType.h"
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Data Structures
// ============================================================================

typedef struct
{
    uint16_t attrId;
    uint16_t attrSize;
    uint8_t *attrValue;  // Caller must free, or use IOT_DaoDeviceFreeElemsInfo()
} IOT_DaoDeviceStateAttr_t;

typedef struct
{
    uint16_t elemId;
    uint16_t elemType;
    uint8_t numOfAttr;
    IOT_DaoDeviceStateAttr_t *attrList;  // Caller must free, or use IOT_DaoDeviceFreeElemsInfo()
} IOT_DaoDeviceStateElemState_t;

typedef struct
{
    uint16_t numOfElem;
    IOT_DaoDeviceStateElemState_t *elemList;  // Caller must free, or use IOT_DaoDeviceFreeElemsInfo()
} IOT_DaoDeviceElemsAttrsInfo_t;

/** Length of a device id, in bytes. */
#define IOT_DAO_DEVICE_ID_LEN 12

typedef struct
{
    uint8_t macType;
    uint8_t *mac;        // Caller must free, or use IOT_DaoDeviceFreeIdentityInfo()
    uint8_t macSize;
    uint8_t devId[IOT_DAO_DEVICE_ID_LEN];
} IOT_DevIdentityInfo_t;

typedef struct
{
    uint16_t rootEid;
    uint16_t protocol;
    uint16_t nwkAddr;
    uint16_t deviceType;
} IOT_DevCommonInfo_t;

typedef struct
{
    uint8_t *state;
    uint8_t stateLen;
} IOT_DevElemsStateInfo_t;

// ============================================================================
// Identity Info (rarely accessed - not cached)
// ============================================================================

/**
 * @brief Get device identity info (BLOCKING)
 * @param eid Element ID
 * @param outDevIdentityInfo Output: identity info (caller must call IOT_DaoDeviceFreeIdentityInfo)
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if not exists
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoDeviceGetIdentityInfo(uint16_t eid, IOT_DevIdentityInfo_t *outDevIdentityInfo);

/**
 * @brief Free identity info structure
 * @param info Identity info to free (frees mac pointer)
 * Blocks: no. Safe from a timer callback: yes. Task: any.
 */
void IOT_DaoDeviceFreeIdentityInfo(IOT_DevIdentityInfo_t *info);

// ============================================================================
// Common Info (rarely accessed - not cached)
// ============================================================================

/**
 * @brief Get device common info (BLOCKING)
 * @param eid Element ID
 * @param outDevCommonInfo Output: common info
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if not exists
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoDeviceGetCommonInfo(uint16_t eid, IOT_DevCommonInfo_t *outDevCommonInfo);

// ============================================================================
// Element State (frequently accessed - CACHED)
// ============================================================================

/**
 * @brief Get device elements state (BLOCKING, cached)
 * @param DevEid Device Element ID
 * @param outElemsInfo Output: elements info (caller must call IOT_DaoDeviceFreeElemsInfo)
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if not exists
 * Blocks: yes (the element-state cache lock; storage on a cache miss). Safe from a timer
 *         callback: no. Task: any, including your core event callback.
 */
iot_err_t IOT_DaoDeviceGetDeviceElemsState(uint16_t DevEid, IOT_DaoDeviceElemsAttrsInfo_t *outElemsInfo);

/**
 * @brief Get element type for a specific element (BLOCKING, cached)
 * @param devEid Device Element ID
 * @param elemId Element ID to look up
 * @param outElemType Output: element type
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if device or element not exists
 * Blocks: yes (the element-state cache lock; storage on a cache miss). Safe from a timer
 *         callback: no. Task: any, including your core event callback.
 */
iot_err_t IOT_DaoDeviceGetElementType(uint16_t devEid, uint16_t elemId, uint16_t *outElemType);

/**
 * @brief Free elements info structure
 * @param elemsInfo Elements info to free (frees all nested allocations)
 * Blocks: no. Safe from a timer callback: yes. Task: any.
 */
void IOT_DaoDeviceFreeElemsInfo(IOT_DaoDeviceElemsAttrsInfo_t *elemsInfo);

// ============================================================================
// Mesh Key Management
// ============================================================================

/** Largest mesh device key blob core delivers; size buffers for IOT_DaoDeviceGetMeshKey() with it. */
#define IOT_DAO_DEVICE_MESH_KEY_MAX_LEN 18

/**
 * @brief Get the stored mesh device key blob (BLOCKING)
 *
 * Opaque blob, returned exactly as stored; size buffers with IOT_DAO_DEVICE_MESH_KEY_MAX_LEN.
 *
 * @param eid Element ID
 * @param outKey Output: destination buffer
 * @param outKeyCap Capacity of outKey. A buffer of IOT_DAO_DEVICE_MESH_KEY_MAX_LEN bytes
 *                  always fits; a smaller one returns IOT_ERR_NO_MEM instead of
 *                  overflowing it.
 * @param outKeyLen Output: actual number of bytes written
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if no mesh key for this EID,
 *         IOT_ERR_NO_MEM if outKeyCap is smaller than the stored blob
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoDeviceGetMeshKey(uint16_t eid, uint8_t *outKey, uint8_t outKeyCap, uint8_t *outKeyLen);

// ============================================================================
// Enumeration
// ============================================================================

/**
 * @brief Get list of all registered device EIDs (BLOCKING)
 * @param outEids Output: heap-allocated array of EIDs (caller must free)
 * @param outCount Output: number of EIDs
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if no devices
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoDeviceGetAllEids(uint16_t **outEids, uint16_t *outCount);

#ifdef __cplusplus
}
#endif

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

typedef enum
{
    DEVICE_DATA_ID,          // corresponds to IOT_DevIdentityInfo_t
    DEVICE_DATA_COMMON,      // corresponds to IOT_DevCommonInfo_t
    DEVICE_DATA_ELEM_STATE,  // corresponds to element state data
    DEVICE_DATA_LOG,         // device logs
    DEVICE_DATA_MESH_DEVKEY, // mesh device key
    DEVICE_DATA_ALL          // all device data
} IOT_DaoDeviceDataType_t;

#define DEVICE_ID_LEN 12

typedef struct
{
    uint8_t macType;
    uint8_t *mac;        // Caller must free, or use IOT_DaoDeviceFreeIdentityInfo()
    uint8_t macSize;
    uint8_t devId[DEVICE_ID_LEN];
} IOT_DevIdentityInfo_t; // ns "device_identity"

typedef struct
{
    uint16_t rootEid;
    uint16_t protocol;
    uint16_t nwkAddr;
    uint16_t deviceType;
} IOT_DevCommonInfo_t;    // ns "dev_common_info"

typedef struct
{
    uint8_t *state;
    uint8_t stateLen;
} IOT_DevElemsStateInfo_t; // ns "elements_info"

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize device management subsystem
 * @return IOT_OK on success
 * @note Must be called before any other device functions
 * @note BLOCKING - do not call from callbacks
 */
iot_err_t IOT_DaoDeviceInit(void);

// ============================================================================
// Identity Info (rarely accessed - not cached)
// ============================================================================

/**
 * @brief Set device identity info (BLOCKING)
 * @param eid Element ID
 * @param devIdentityInfo Identity info to save
 * @return IOT_OK on success
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceSetIdentityInfo(uint16_t eid, IOT_DevIdentityInfo_t *devIdentityInfo);

/**
 * @brief Set device identity info asynchronously (NON-BLOCKING)
 * @param eid Element ID
 * @param devIdentityInfo Identity info to save (data will be copied)
 * @return IOT_OK if queued successfully
 * @note Safe to call from BLE/MQTT callbacks
 */
iot_err_t IOT_DaoDeviceSetIdentityInfoAsync(uint16_t eid, IOT_DevIdentityInfo_t *devIdentityInfo);

/**
 * @brief Get device identity info (BLOCKING)
 * @param eid Element ID
 * @param outDevIdentityInfo Output: identity info (caller must call IOT_DaoDeviceFreeIdentityInfo)
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if not exists
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceGetIdentityInfo(uint16_t eid, IOT_DevIdentityInfo_t *outDevIdentityInfo);

/**
 * @brief Free identity info structure
 * @param info Identity info to free (frees mac pointer)
 */
void IOT_DaoDeviceFreeIdentityInfo(IOT_DevIdentityInfo_t *info);

// ============================================================================
// Common Info (rarely accessed - not cached)
// ============================================================================

/**
 * @brief Set device common info (BLOCKING)
 * @param eid Element ID
 * @param devCommonInfo Common info to save
 * @return IOT_OK on success
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceSetCommonInfo(uint16_t eid, IOT_DevCommonInfo_t *devCommonInfo);

/**
 * @brief Set device common info asynchronously (NON-BLOCKING)
 * @param eid Element ID
 * @param devCommonInfo Common info to save (data will be copied)
 * @return IOT_OK if queued successfully
 * @note Safe to call from BLE/MQTT callbacks
 */
iot_err_t IOT_DaoDeviceSetCommonInfoAsync(uint16_t eid, IOT_DevCommonInfo_t *devCommonInfo);

/**
 * @brief Get device common info (BLOCKING)
 * @param eid Element ID
 * @param outDevCommonInfo Output: common info
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if not exists
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceGetCommonInfo(uint16_t eid, IOT_DevCommonInfo_t *outDevCommonInfo);

// ============================================================================
// Element State (frequently accessed - CACHED)
// ============================================================================

/**
 * @brief Add element info to device state (BLOCKING, cached)
 * @param DevEid Device Element ID
 * @param elemInfo Element info to add
 * @return IOT_OK on success
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceAddElementInfo(uint16_t DevEid, IOT_DaoDeviceStateElemState_t *elemInfo);

/**
 * @brief Update element state (BLOCKING, cached)
 * @param eid Element ID
 * @param elemInfo Element info with updated attributes
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if device not exists
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceUpdateElementState(uint16_t eid, IOT_DaoDeviceElemsAttrsInfo_t *elemInfo);

/**
 * @brief Update element state asynchronously (NON-BLOCKING)
 * @param eid Element ID
 * @param elemInfo Element info with updated attributes (data will be copied)
 * @return IOT_OK if queued successfully
 * @note Safe to call from BLE/MQTT callbacks
 * @note Requires existing device state - use IOT_DaoDeviceAddElementInfo first
 */
iot_err_t IOT_DaoDeviceUpdateElementStateAsync(uint16_t eid, IOT_DaoDeviceElemsAttrsInfo_t *elemInfo);

/**
 * @brief Get device elements state (BLOCKING, cached)
 * @param DevEid Device Element ID
 * @param outElemsInfo Output: elements info (caller must call IOT_DaoDeviceFreeElemsInfo)
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if not exists
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceGetDeviceElemsState(uint16_t DevEid, IOT_DaoDeviceElemsAttrsInfo_t *outElemsInfo);

/**
 * @brief Get element type for a specific element (BLOCKING, cached)
 * @param devEid Device Element ID
 * @param elemId Element ID to look up
 * @param outElemType Output: element type
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if device or element not exists
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceGetElementType(uint16_t devEid, uint16_t elemId, uint16_t *outElemType);

/**
 * @brief Free elements info structure
 * @param elemsInfo Elements info to free (frees all nested allocations)
 */
void IOT_DaoDeviceFreeElemsInfo(IOT_DaoDeviceElemsAttrsInfo_t *elemsInfo);

// ============================================================================
// Mesh Key Management
// ============================================================================

/**
 * @brief Get mesh device key (BLOCKING)
 * @param eid Element ID
 * @param outKey Output: mesh key buffer (must be at least 16 bytes)
 * @param outKeyLen Output: actual key length
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if no mesh key for this EID
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceGetMeshKey(uint16_t eid, uint8_t *outKey, uint8_t *outKeyLen);

/**
 * @brief Set mesh device key (BLOCKING)
 * @param eid Element ID
 * @param meshKey Mesh key data
 * @param keyLen Key length
 * @return IOT_OK on success
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceSetMeshKey(uint16_t eid, uint8_t *meshKey, uint8_t keyLen);

/**
 * @brief Set mesh device key asynchronously (NON-BLOCKING)
 * @param eid Element ID
 * @param meshKey Mesh key data (will be copied)
 * @param keyLen Key length
 * @return IOT_OK if queued successfully
 * @note Safe to call from BLE/MQTT callbacks
 */
iot_err_t IOT_DaoDeviceSetMeshKeyAsync(uint16_t eid, uint8_t *meshKey, uint8_t keyLen);

// ============================================================================
// Device Removal
// ============================================================================

/**
 * @brief Remove specific device data (BLOCKING)
 * @param eid Element ID
 * @param dataType Type of data to remove
 * @return IOT_OK on success
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceRemoveData(uint16_t eid, IOT_DaoDeviceDataType_t dataType);

/**
 * @brief Remove all device data (BLOCKING)
 * @param eid Element ID
 * @return IOT_OK on success
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
iot_err_t IOT_DaoDeviceRemoveDevice(uint16_t eid);

/**
 * @brief Remove device data asynchronously (NON-BLOCKING)
 * @param eid Element ID
 * @param dataType Type of data to remove
 * @return IOT_OK if queued successfully
 * @note Safe to call from BLE/MQTT callbacks
 */
iot_err_t IOT_DaoDeviceRemoveDataAsync(uint16_t eid, IOT_DaoDeviceDataType_t dataType);

// ============================================================================
// Device Lookup
// ============================================================================

/**
 * @brief Check if device exists by MAC address (BLOCKING)
 * @param mac MAC address
 * @param outEid Output: Element ID if found (can be NULL)
 * @return true if device exists
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
bool IOT_DaoDeviceIsDeviceExistByMac(uint8_t *mac, uint16_t *outEid);

/**
 * @brief Look up EID by 12-byte Device ID (BLOCKING)
 * Iterates device_identity namespace to find matching devId.
 * Used by the voice control path to resolve a device id to an EID.
 *
 * @param devId 12-byte device ID
 * @param outEid Output: Element ID if found
 * @return true if device found
 * @warning Do NOT call from BLE/MQTT callbacks - will block!
 */
bool IOT_DaoDeviceGetEidByDevId(uint8_t *devId, uint16_t *outEid);

// ============================================================================
// Cache Control
// ============================================================================

/**
 * @brief Flush device element state cache to NVS (BLOCKING)
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoDeviceFlushCache(void);

/**
 * @brief Invalidate device element state cache (BLOCKING)
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoDeviceInvalidateCache(void);

/**
 * @brief Factory reset device data (BLOCKING)
 * @note Erases all device data from NVS and clears cache
 */

iot_err_t IOT_DaoDeviceFactoryReset(void);

/**
 * @brief Get list of all registered device EIDs (BLOCKING)
 * @param outEids Output: heap-allocated array of EIDs (caller must free)
 * @param outCount Output: number of EIDs
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if no devices
 */
iot_err_t IOT_DaoDeviceGetAllEids(uint16_t **outEids, uint16_t *outCount);

#ifdef __cplusplus
}
#endif
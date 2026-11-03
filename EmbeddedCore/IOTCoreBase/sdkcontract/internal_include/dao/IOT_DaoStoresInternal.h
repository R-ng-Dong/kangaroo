#pragma once
/*
 * Store lifecycle functions, internal to sdkcontract.
 *
 * Two groups. The lifecycle functions: IOT_ContractInit() initialises the stores,
 * IOT_ContractRemoveDevice() and IOT_ContractFactoryReset() clear them together with the
 * Smart, Record and IR data that refer to the same devices; called directly, a removal
 * leaves that data behind. And every store write: the contract is the one writer (joins,
 * groups, attribute sets, state commits, IR keysets), because a write made anywhere else is a
 * change nobody recorded, reported or gave to Smart. The public store headers are read-only.
 */
#include <inttypes.h>
#include "IOT_ErrorManager.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "dao/IOT_DaoGroupMgmt.h"
#include "dao/IOT_DaoIRMgmt.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Device store
// ============================================================================

/**
 * @brief Initialize device management subsystem
 * @return IOT_OK on success
 * @note Must be called before any other device functions
 * @note BLOCKING - do not call from callbacks
 */
iot_err_t IOT_DaoDeviceInit(void);

/**
 * @brief Remove all device data (BLOCKING)
 * @param eid Element ID
 * @return IOT_OK on success
 * @warning Do NOT call from transport callbacks - will block!
 */
iot_err_t IOT_DaoDeviceRemoveDevice(uint16_t eid);

/**
 * @brief Factory reset device data (BLOCKING)
 * @note Erases all device data from NVS and clears cache
 */
iot_err_t IOT_DaoDeviceFactoryReset(void);

// ============================================================================
// Group store
// ============================================================================

/**
 * @brief Remove a device from every group it belongs to (BLOCKING)
 * @param eid Element ID
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoGroupRemoveDeviceFromAllGroup(uint16_t eid);

/**
 * @brief Factory reset group data (BLOCKING)
 * @note Erases all group data from NVS
 */
iot_err_t IOT_DaoGroupFactoryReset(void);

// ============================================================================
// IR store
// ============================================================================

/**
 * @brief Initialize the IR DAO
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoIRInit(void);

/**
 * @brief Remove all IR data for a device (protocol codes, headers, rawzip data)
 * @param eid Device endpoint ID
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoIRRemoveDevice(uint16_t eid);

/**
 * @brief Factory reset: remove IR data for the given devices.
 *
 * IR data is stored one namespace per device, so the caller supplies the device
 * EID list.
 *
 * @param eids  Array of device EIDs whose IR namespaces to drop (may be NULL)
 * @param count Number of EIDs
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoIRFactoryReset(const uint16_t *eids, uint16_t count);

// ============================================================================
// Device store: internal operations
// ============================================================================

typedef enum
{
    DEVICE_DATA_ID,          // corresponds to IOT_DevIdentityInfo_t
    DEVICE_DATA_COMMON,      // corresponds to IOT_DevCommonInfo_t
    DEVICE_DATA_ELEM_STATE,  // corresponds to element state data
    DEVICE_DATA_LOG,         // device logs
    DEVICE_DATA_MESH_DEVKEY, // mesh device key
    DEVICE_DATA_ALL          // all device data
} IOT_DaoDeviceDataType_t;

/**
 * @brief Set device identity info asynchronously (NON-BLOCKING)
 * @param eid Element ID
 * @param devIdentityInfo Identity info to save (data will be copied)
 * @return IOT_OK if queued successfully
 * @note Safe to call from transport callbacks
 */
iot_err_t IOT_DaoDeviceSetIdentityInfoAsync(uint16_t eid, IOT_DevIdentityInfo_t *devIdentityInfo);

/**
 * @brief Set device common info asynchronously (NON-BLOCKING)
 * @param eid Element ID
 * @param devCommonInfo Common info to save (data will be copied)
 * @return IOT_OK if queued successfully
 * @note Safe to call from transport callbacks
 */
iot_err_t IOT_DaoDeviceSetCommonInfoAsync(uint16_t eid, IOT_DevCommonInfo_t *devCommonInfo);

/**
 * @brief Insert or update a single attribute of an element asynchronously (NON-BLOCKING)
 * @param devEid Device Element ID
 * @param elemId Element ID
 * @param attrId Attribute ID
 * @param attrData Attribute data (will be copied)
 * @param attrDataLen Attribute data length
 * @return IOT_OK if queued successfully
 * @note Safe to call from transport callbacks
 */
iot_err_t IOT_DaoDeviceUpsertAttributeAsync(uint16_t devEid, uint16_t elemId, uint16_t attrId, const uint8_t *attrData, uint16_t attrDataLen);

/**
 * @brief Set mesh device key asynchronously (NON-BLOCKING)
 * @param eid Element ID
 * @param meshKey Mesh key data (will be copied)
 * @param keyLen Key length
 * @return IOT_OK if queued successfully
 * @note Safe to call from transport callbacks
 */
iot_err_t IOT_DaoDeviceSetMeshKeyAsync(uint16_t eid, uint8_t *meshKey, uint8_t keyLen);

/**
 * @brief Remove specific device data (BLOCKING)
 * @param eid Element ID
 * @param dataType Type of data to remove
 * @return IOT_OK on success
 * @warning Do NOT call from transport callbacks - will block!
 */
iot_err_t IOT_DaoDeviceRemoveData(uint16_t eid, IOT_DaoDeviceDataType_t dataType);

/**
 * @brief Remove device data asynchronously (NON-BLOCKING)
 * @param eid Element ID
 * @param dataType Type of data to remove
 * @return IOT_OK if queued successfully
 * @note Safe to call from transport callbacks
 */
iot_err_t IOT_DaoDeviceRemoveDataAsync(uint16_t eid, IOT_DaoDeviceDataType_t dataType);

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
 * @brief Update the attributes of one or more elements (BLOCKING, cached).
 *
 * Updates the element-state cache before it returns, exactly like
 * IOT_DaoDeviceUpsertAttribute() for several attributes at once. The cache is written to
 * storage about once a second.
 *
 * @param eid Device EID
 * @param elemInfo Element info with updated attributes (data is copied)
 * @return IOT_OK on success
 * @note Requires existing device state - use IOT_DaoDeviceAddElementInfo first
 * Blocks: yes (the element-state cache lock; storage on a cache miss). Safe from a timer
 *         callback: no. Task: any, including your core event callback.
 */
iot_err_t IOT_DaoDeviceUpdateElementState(uint16_t eid, IOT_DaoDeviceElemsAttrsInfo_t *elemInfo);

/**
 * @brief Store the mesh device key blob core delivered for a device (BLOCKING)
 * @param eid Element ID
 * @param meshKey The blob, exactly as delivered (opaque)
 * @param keyLen Blob length
 * @return IOT_OK on success
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoDeviceSetMeshKey(uint16_t eid, uint8_t *meshKey, uint8_t keyLen);

/**
 * @brief Set device identity info (BLOCKING)
 * @param eid Element ID
 * @param devIdentityInfo Identity info to save
 * @return IOT_OK on success
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoDeviceSetIdentityInfo(uint16_t eid, IOT_DevIdentityInfo_t *devIdentityInfo);

/**
 * @brief Set device common info (BLOCKING)
 * @param eid Element ID
 * @param devCommonInfo Common info to save
 * @return IOT_OK on success
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoDeviceSetCommonInfo(uint16_t eid, IOT_DevCommonInfo_t *devCommonInfo);

/**
 * @brief Add element info to device state (BLOCKING, cached)
 * @param DevEid Device Element ID
 * @param elemInfo Element info to add
 * @return IOT_OK on success
 * Blocks: yes (the element-state cache lock; storage on a cache miss). Safe from a timer
 *         callback: no. Task: any, including your core event callback.
 */
iot_err_t IOT_DaoDeviceAddElementInfo(uint16_t DevEid, IOT_DaoDeviceStateElemState_t *elemInfo);

/**
 * @brief Insert or update a single attribute of an element (BLOCKING, cached)
 * @param devEid Device Element ID
 * @param elemId Element ID
 * @param attrId Attribute ID
 * @param attrData Attribute data
 * @param attrDataLen Attribute data length
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if device not exists, IOT_ERR_NOT_FOUND if element not exists
 * Blocks: yes (the element-state cache lock; storage on a cache miss). Safe from a timer
 *         callback: no. Task: any, including your core event callback.
 */
iot_err_t IOT_DaoDeviceUpsertAttribute(uint16_t devEid, uint16_t elemId, uint16_t attrId, const uint8_t *attrData, uint16_t attrDataLen);

/**
 * @brief Check if device exists by MAC address (BLOCKING)
 * @param mac MAC address
 * @param outEid Output: Element ID if found (can be NULL)
 * @return true if device exists
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
bool IOT_DaoDeviceIsDeviceExistByMac(uint8_t *mac, uint16_t *outEid);


/**
 * @brief Look up EID by device ID (BLOCKING)
 * Searches the stored identity records for a matching devId.
 * Used by the voice control path to resolve a device id to an EID.
 *
 * @param devId Device ID, IOT_DAO_DEVICE_ID_LEN bytes
 * @param outEid Output: Element ID if found
 * @return true if device found
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
bool IOT_DaoDeviceGetEidByDevId(uint8_t *devId, uint16_t *outEid);


// ============================================================================
// Group store: internal operations
// ============================================================================

/**
 * @brief Remove a device from one group.
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoGroupRemoveDeviceFromGroup(uint16_t groupId, uint16_t eid);

/**
 * @brief Remove a device from every custom group it belongs to (not from the location).
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoGroupRemoveDeviceFromCustomGroups(uint16_t eid);

/**
 * @brief Add a device to a group, bound to the given elements (NULL / 0: every element).
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoGroupAddDeviceToGroup(uint16_t groupId, uint16_t eid, uint8_t *elemInfo, uint16_t elemInfoLen);


/**
 * @brief Get raw group device list blob from NVS (BLOCKING)
 * @note  Prefer IOT_DaoGroupGetMemberList for most use cases
 */
iot_err_t IOT_DaoGroupGetDeviceList(uint16_t groupId, uint8_t **outDeviceList, uint16_t *outGroupListSize);

// ============================================================================
// IR store: internal operations
// ============================================================================

/**
 * @brief One parsed keyset entry to persist for a device (borrowed pointers).
 *
 * `dataName == IOT_IR_HEADER_DATANAME` → the rawzip header (blob).
 * else `isProtocol` → a protocol code (`code`), otherwise a rawzip data blob (`data`).
 */
typedef struct
{
    uint16_t dataName;
    bool isProtocol;
    uint64_t code;        ///< valid when isProtocol
    const uint8_t *data;  ///< valid when !isProtocol (blob / header)
    uint16_t dataLen;
} IOT_IRKeysetEntry_t;

/**
 * @brief Persist a whole keyset for one device in a SINGLE flash commit.
 *
 * Writes all @p count entries into the device's IR store via one batched write,
 * instead of committing per entry. Header/protocol/data entries are routed by
 * `IOT_IRKeysetEntry_t`. Caller retains ownership of @p entries + their data.
 *
 * @return IOT_OK on success; nothing is committed if the batch fails.
 * Blocks: yes (storage). Safe from a timer callback: no. Task: any, including your core event
 *         callback.
 */
iot_err_t IOT_DaoIRStoreKeyset(uint16_t eid, const IOT_IRKeysetEntry_t *entries, uint16_t count);


/**
 * @brief Store a protocol-based IR code for a device button
 * @param eid Device endpoint ID
 * @param dataName Button/command index (derived from attrValue)
 * @param code 64-bit IR protocol code
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoIRSetProtocolCode(uint16_t eid, uint16_t dataName, uint64_t code);

/**
 * @brief Store the rawzip decompression header for a device
 * @param eid Device endpoint ID
 * @param header The IR header blob (opaque)
 * @param headerLen Header length in bytes
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoIRSetRawZipHeader(uint16_t eid, const uint8_t *header, uint16_t headerLen);

/**
 * @brief Store compressed rawzip IR data for a device button
 * @param eid Device endpoint ID
 * @param dataName Button/command index
 * @param data Compressed data bytes
 * @param dataLen Data length in bytes
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoIRSetRawZipData(uint16_t eid, uint16_t dataName, const uint8_t *data, uint16_t dataLen);

#ifdef __cplusplus
}
#endif

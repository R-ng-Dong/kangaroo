#pragma once
#include "dao/IOT_DaoType.h"
#include "IOT_ErrorManager.h"
#include <inttypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Device Cleanup
// ============================================================================

iot_err_t IOT_DaoSmartRemoveDeviceData(uint16_t eid);

/**
 * @brief Factory reset: remove all smart bind command namespaces for given SMIDs
 * @param smids Array of SMID values whose namespaces to delete
 * @param smidCount Number of SMIDs in the array
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartFactoryReset(const uint16_t *smids, uint16_t smidCount);

// ============================================================================
// Bind Command Storage
// ============================================================================

/**
 * @brief Store bound commands for a SMID+EID pair (BLOCKING)
 * @param smid Smart automation ID
 * @param eid Target device EID
 * @param rootEid Root device EID
 * @param prtc Protocol
 * @param filter Filter value
 * @param cfm Device-managed confirmation counter (incremented by caller on each save)
 * @param cmdData Serialized command data
 * @param cmdDataLen Command data length
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartSetBindCmd(uint16_t smid, uint16_t eid,
                                  uint16_t rootEid, uint16_t prtc, uint16_t filter,
                                  uint16_t cfm,
                                  uint8_t *cmdData, uint16_t cmdDataLen);

/**
 * @brief Load all bound commands for a SMID (BLOCKING)
 * @param smid Smart automation ID
 * @param outKeyList Output: array of EID key strings (caller must free each + array)
 * @param outKeyCount Output: number of keys
 * @param outDataList Output: array of serialized command data blobs (caller must free each + array)
 * @param outDataSizes Output: array of data sizes (caller must free)
 * @param outRootEid Output: root device EID (can be NULL)
 * @param outPrtc Output: protocol (can be NULL)
 * @param outCfm Output: current cfm counter (can be NULL)
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if SMID not exists
 */
iot_err_t IOT_DaoSmartGetBindCmds(uint16_t smid,
                                   char ***outKeyList, uint16_t *outKeyCount,
                                   void ***outDataList, uint16_t **outDataSizes,
                                   uint16_t *outRootEid, uint16_t *outPrtc,
                                   uint16_t *outCfm);

/**
 * @brief Remove bound commands for a single EID within a SMID (BLOCKING)
 * @param smid Smart automation ID
 * @param eid Target device EID to remove
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartRemoveBindCmd(uint16_t smid, uint16_t eid);

/**
 * @brief Remove all data for a SMID (BLOCKING)
 * @param smid Smart automation ID
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartRemoveSmid(uint16_t smid);

#ifdef __cplusplus
}
#endif

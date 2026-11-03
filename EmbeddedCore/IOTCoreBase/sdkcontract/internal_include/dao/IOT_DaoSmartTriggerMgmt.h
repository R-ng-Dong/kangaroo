#pragma once
#include "IOT_ErrorManager.h"
#include <inttypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Trigger Storage Struct (flat packed for NVS blob storage)
// ============================================================================

// Max local elements a single sync-on-off trigger (subSmartType 71) can bind.
// Matches the deferred-eval element cap and the cond-group max N.
#define SMART_TRIGGER_ELM_CAP 8

typedef struct __attribute__((packed))
{
    // Identity
    uint16_t smid;
    uint16_t eid;

    // From the trigger's element info
    uint16_t type;
    uint16_t smartType;
    uint16_t subSmartType;
    uint16_t elmIdInfo;     // elmId from the trigger's element info
    uint16_t conditionInfo; // condition from the trigger's element info
    uint16_t cfm;

    // From the trigger's extended element info (actual monitoring target)
    uint16_t elmId;    // element ID to monitor
    uint16_t condition; // COND_ANY=1, COND_EQUAL=2, COND_EXIST=3, COND_BETWEEN=4

    // Primary attribute to match (from the trigger's attribute value)
    uint16_t attrId;
    uint16_t attrSize;
    uint8_t attrValue[8];

    // Secondary attribute for COND_BETWEEN upper bound
    uint16_t secondaryAttrSize;
    uint8_t secondaryAttrValue[8];

    // Time config (from the trigger's time configuration)
    uint16_t timCfgType;
    uint16_t timCfgValue;
    uint16_t timeStart; // from the trigger's time window, 0 = no time window
    uint16_t timeStop;
    uint8_t weekDay; // from the trigger's time window, bitmask: bit0=Sun..bit6=Sat, 0x00 = all days

    // Trigger key (from the trigger key, for sync/announce verification)
    uint16_t triggerKeyOwnerEid;
    uint16_t triggerKeyOwnerEidCount;
    uint16_t triggerKeyExtEid;
    uint16_t triggerKeyExtEidCount;

    // Mix: the partner a stair-switch trigger mirrors to
    uint16_t eidMix;
    uint16_t rootEidMix;
    uint16_t elemIdMix;

    // Timezone
    uint8_t isDeviceUtc;
    int16_t timeZoneQuarterHours; // timeZoneFraction * 4

    // Runtime state (persisted)
    uint8_t enabled;        // 1 = enabled, 0 = disabled
    uint16_t minuteDisable; // 0 = permanent, >0 = minutes until auto-re-enable

    // Cond-group (distributed AND/OR, subSmartType 2048) — appended fields.
    // Shorter stored blobs load with these = 0 (zero-init on short read), so a 0
    // condCount marks a legacy / non-cond-group record. See
    // IOT_DaoSmartTriggerGet size-tolerant load.
    uint8_t condIndex; // this device's bit position in the group (0..7)
    uint8_t condCount; // N: total conditions in the group (0 = not a cond-group)
    uint8_t fireCount; // K: fire when >= K of N conditions are true (K==N=AND, K==1=OR)
    uint16_t condVersion; // cloud-assigned config generation (bumped on add/remove)

    // Sync-on-off element set (subSmartType == SMART_SUB_TYPE_SYNC_ON_OFF, 71) —
    // appended fields. The local elements that mirror one ON/OFF value together,
    // parsed from the primary attr-value (attrId IOT_ATTR_SYNC_ONOFF_ELEMENTS 315).
    // elmNum == 0 => legacy / not a sync-on-off trigger (use the single elmId).
    // Old blobs load with elmNum == 0 via the size-tolerant short read.
    uint8_t elmNum;                          // number of valid entries in elemList (0..CAP)
    uint16_t elemList[SMART_TRIGGER_ELM_CAP];
} IOT_SmartTrigger_t;

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize the trigger DAO (registers cache namespace)
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartTriggerInit(void);

// ============================================================================
// CRUD Operations
// ============================================================================

/**
 * @brief Store a trigger (overwrite if exists). Also updates the EID index.
 * @param smid Smart automation ID (unique trigger identifier)
 * @param trigger Pointer to trigger struct to store
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartTriggerSet(uint16_t smid, const IOT_SmartTrigger_t *trigger);

/**
 * @brief Load a trigger by SMID
 * @param smid Smart automation ID
 * @param outTrigger Output: populated trigger struct
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if not exists
 */
iot_err_t IOT_DaoSmartTriggerGet(uint16_t smid, IOT_SmartTrigger_t *outTrigger);

/**
 * @brief Read ONE seat of an automation. A device may hold several conditions of the
 *        same SMID (e.g. two thresholds on one multi-sensor), each stored separately.
 * @return IOT_OK, or IOT_ERR_NOT_FOUND if that seat is not stored.
 */
iot_err_t IOT_DaoSmartTriggerGetSeat(uint16_t smid, uint8_t seat, IOT_SmartTrigger_t *outTrigger);

/**
 * @brief Bitmask of the seats this device holds for @p smid (bit i = seat i).
 *        Probes all seats, so callers on a hot path must cache the result.
 * @return IOT_OK with a non-zero mask, or IOT_ERR_NOT_FOUND with *outMask = 0.
 */
iot_err_t IOT_DaoSmartTriggerGetSeatMask(uint16_t smid, uint8_t *outMask);

/**
 * @brief Remove a trigger by SMID. Also updates the EID index.
 * @param smid Smart automation ID
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartTriggerRemove(uint16_t smid);

/**
 * @brief Get all SMIDs that monitor a given EID (via EID index)
 * @param eid Device EID to look up
 * @param outSmids Output: heap-allocated array of SMIDs (caller must free)
 * @param outCount Output: number of SMIDs
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if no triggers for this EID
 */
iot_err_t IOT_DaoSmartTriggerGetByEid(uint16_t eid, uint16_t **outSmids, uint16_t *outCount);

/**
 * @brief Enable or disable a single trigger
 * @param smid Smart automation ID
 * @param enabled true = enable, false = disable
 * @param minuteDisable Minutes until auto-re-enable (0 = permanent change)
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if not exists
 */
iot_err_t IOT_DaoSmartTriggerSetEnabled(uint16_t smid, bool enabled, uint16_t minuteDisable);

/**
 * @brief Enable or disable all triggers for an EID
 * @param eid Device EID
 * @param enabled true = enable all, false = disable all
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartTriggerSetEnabledByEid(uint16_t eid, bool enabled);

/**
 * @brief Remove all triggers for an EID (including EID index)
 * @param eid Device EID
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartTriggerRemoveByEid(uint16_t eid);

/**
 * @brief Factory reset: remove ALL trigger data (all SMIDs + all EID indexes).
 * Also returns the list of unique SMIDs found, so the caller can clean
 * up related smart bind command namespaces.
 *
 * @param outSmids Output: heap-allocated array of SMIDs (caller must free), can be NULL
 * @param outCount Output: number of SMIDs, can be NULL
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartTriggerFactoryReset(uint16_t **outSmids, uint16_t *outCount);

/**
 * @brief Get ALL stored trigger SMIDs (non-destructive namespace walk).
 * Used by the cond-group boot re-sync to enumerate every trigger this device
 * owns. Only trigger ("T") keys are returned; EID-index ("I") keys are skipped.
 *
 * @param outSmids Output: heap-allocated array of SMIDs (caller must free);
 *                 set to NULL when none exist.
 * @param outCount Output: number of SMIDs (0 when none exist).
 * @return IOT_OK on success (including the empty case), IOT_ERR_INVALID_ARG on
 *         NULL args.
 */
iot_err_t IOT_DaoSmartTriggerGetAllSmids(uint16_t **outSmids, uint16_t *outCount);

#ifdef __cplusplus
}
#endif

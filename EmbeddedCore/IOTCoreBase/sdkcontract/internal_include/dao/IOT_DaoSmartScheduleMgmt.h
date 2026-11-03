#pragma once
#include "IOT_ErrorManager.h"
#include <inttypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the schedule DAO (registers cache namespace)
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartScheduleInit(void);

/**
 * @brief Add a schedule time for a SMID. Deduplicates — if the time already exists, does nothing.
 * @param smid Smart automation ID
 * @param minuteOfWeek Device-local minute-of-week (0-10079): day*1440 + hour*60 + minute
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartScheduleSet(uint16_t smid, uint16_t minuteOfWeek);

/**
 * @brief Get all schedule times for a SMID
 * @param smid Smart automation ID
 * @param outTimes Output: heap-allocated array of minute-of-week values (caller must free)
 * @param outCount Output: number of times
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if no schedule for this SMID
 */
iot_err_t IOT_DaoSmartScheduleGet(uint16_t smid, uint16_t **outTimes, uint16_t *outCount);

/**
 * @brief Remove all schedule times for a SMID, AND its enable/disable flag.
 *
 * For when the automation itself goes away (REMOVE_ANNOUNCE / SCHEDULE_REMOVE_ANNOUNCE).
 * To replace the times of an automation that still exists, use
 * IOT_DaoSmartScheduleClearTimes() — this one resets the user's enable choice, because
 * IOT_DaoSmartScheduleIsEnabled() defaults to true once the flag is gone.
 *
 * @param smid Smart automation ID
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartScheduleRemove(uint16_t smid);

/**
 * @brief Remove the schedule times for a SMID but KEEP its enable/disable flag.
 *
 * The wholesale-replace path: SCHEDULE_UPDATE_ANNOUNCE clears the times and stores the new
 * list. Using IOT_DaoSmartScheduleRemove() there would delete the "disabled" flag, and
 * IsEnabled defaults to true — so editing the times of a switched-off schedule would
 * switch it back on.
 *
 * @param smid Smart automation ID
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartScheduleClearTimes(uint16_t smid);

/**
 * @brief Load all stored schedules (for periodic tick iteration).
 *
 * Returns parallel arrays: outSmids[i] has outTimeCounts[i] times in outTimeLists[i].
 * Caller must free all allocated memory.
 *
 * @param outSmids      Output: heap-allocated array of SMIDs
 * @param outSmidCount  Output: number of SMIDs
 * @param outTimeLists  Output: array of uint16_t* arrays (one per SMID)
 * @param outTimeCounts Output: array of counts (one per SMID)
 * @return IOT_OK on success, IOT_ERR_NOT_FOUND if no schedules stored
 */
iot_err_t IOT_DaoSmartScheduleGetAll(uint16_t **outSmids, uint16_t *outSmidCount,
                                     uint16_t ***outTimeLists, uint16_t **outTimeCounts);

/**
 * @brief Enable or disable a schedule SMID. Disabled schedules are skipped during periodic tick.
 *        Schedules are enabled by default (if no explicit flag is stored).
 * @param smid Smart automation ID
 * @param enabled true to enable, false to disable
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartScheduleSetEnabled(uint16_t smid, bool enabled);

/**
 * @brief Check if a schedule SMID is enabled. Returns true if no explicit flag is stored (default enabled).
 * @param smid Smart automation ID
 * @return true if enabled (or no flag stored), false if explicitly disabled
 */
bool IOT_DaoSmartScheduleIsEnabled(uint16_t smid);

/**
 * @brief Factory reset: remove ALL schedule data
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoSmartScheduleFactoryReset(void);

#ifdef __cplusplus
}
#endif

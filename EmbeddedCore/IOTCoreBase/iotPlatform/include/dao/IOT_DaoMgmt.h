#pragma once

#include "IOT_ErrorManager.h"
#include <stdbool.h>
#include <inttypes.h>
#include "dao/IOT_DaoType.h"

iot_err_t IOT_DaoInit(void);
iot_err_t IOT_DaoDeinit(void);

/**
 * @brief Factory reset ALL DAO subsystems (BLOCKING).
 * Clears config, device data, groups, smart triggers, smart schedules,
 * smart bind commands, IR data, and sensor logs.
 * Call before device restart to ensure a clean slate.
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoFactoryResetAll(void);

/**
 * @brief Get NVS storage usage statistics (BLOCKING)
 * @param usedEntries Output: number of used NVS entries
 * @param totalEntries Output: total available NVS entries
 * @return IOT_OK on success
 */
iot_err_t IOT_DaoGetStorageStats(uint32_t *usedEntries, uint32_t *totalEntries);
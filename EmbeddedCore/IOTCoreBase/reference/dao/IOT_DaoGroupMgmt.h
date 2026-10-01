#pragma once
#include "dao/IOT_DaoType.h"
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
iot_err_t IOT_DaoGroupGetDeviceList(uint16_t groupId, uint8_t **outDeviceList, uint16_t *outGroupListSize);
iot_err_t IOT_DaoGroupRemoveDeviceFromGroup(uint8_t groupId, uint16_t eid);
iot_err_t IOT_DaoGroupRemoveDeviceFromAllGroup(uint16_t eid);
iot_err_t IOT_DaoGroupAddDeviceToGroup(uint16_t groupId, uint16_t eid, uint8_t *elemInfo, uint16_t elemInfoLen);

/**
 * @brief Factory reset group data (BLOCKING)
 * @note Erases all group data from NVS
 */

iot_err_t IOT_DaoGroupFactoryReset(void);
#pragma once

/**
 * @file IOT_ContractDeviceStatus.h
 * @brief The contract's default answer to the device status request. Internal to the contract.
 */

#include "IOT_CoreEventHandler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Answer a device status request the product left unclaimed: its result is still core's preset
 * IOT_ERR_NOT_SUPPORTED. Fills the status from the platform, passes it to the product's hook
 * (IOT_ContractDeviceStatusConfigure()), replies once with IOT_CoreRespondDeviceStatus() and
 * writes the reply's return into @p req->result. A claimed request (any other result) is left
 * alone: the product has answered it. NULL: nothing. Called by the router after the product's
 * onCoreEvent, on the task that delivers core events.
 */
void IOT_ContractDeviceStatusAnswer(IOT_CoreDeviceEventStatusRequestData_t *req);

#ifdef __cplusplus
}
#endif

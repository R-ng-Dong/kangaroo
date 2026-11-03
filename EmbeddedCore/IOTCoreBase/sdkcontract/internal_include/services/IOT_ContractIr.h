#pragma once

#include "IOT_ErrorManager.h"
#include "IOT_CoreEventIr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The IR requests the contract owns: storing a device's keyset and answering what IR this
 *        device can do (from IOT_ContractIrConfigure()).
 *
 * The router calls this for IOT_IR_EVENT_STORE_KEYSET and IOT_IR_EVENT_QUERY_CAPABILITY only;
 * sending and learning are the product's and are forwarded. Writes the outcome into the event:
 * `result` for a store, `capMode`/`capDetect` for a capability query.
 *
 * @return IOT_OK, the store's error, IOT_ERR_INVALID_ARG for a NULL event or an unusable keyset,
 *         or IOT_ERR_NOT_SUPPORTED for any other IR event type (left for the product).
 */
iot_err_t IOT_ContractHandleIrEvent(IOT_CoreIrEvent_t *irEvent);

#ifdef __cplusplus
}
#endif

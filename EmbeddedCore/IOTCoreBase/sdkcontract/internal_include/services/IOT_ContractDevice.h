#pragma once

/*
 * The contract's DEVICE events: a sub-device joining, a device removed, group bind/unbind and a
 * device attribute set. The contract does the store work and answers core; the product's own
 * duties are reached through the hooks in IOT_ContractCallbacks_t (onDeviceJoined,
 * onDeviceRemoving, onDeviceRemoved).
 *
 * Every other DEVICE event is the router's to forward; the cloud-connect duties run in the router
 * before it forwards that event.
 *
 * Internal to the contract. Runs on the task that delivers core events.
 */

#include "IOT_Contract.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_ErrorManager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle one of the DEVICE events the contract owns.
 *
 * NEW_DEVICE_JOINED: store what the phase carries (a stale entry with the same MAC is removed
 *   first, through the removal hooks), call onDeviceJoined, answer core.
 * DEVICE_REMOVED: onDeviceRemoving, then the store delete (root: every contract store), answer
 *   core, then onDeviceRemoved.
 * GROUP_BIND / GROUP_UNBIND: update the Group store, answer core.
 * DEVICE_ATTR_SET: apply (with the device's own element id) -> commit (store, Record) -> answer
 *   core -> Smart with the request's sender.
 *
 * @return The result core was answered with. IOT_ERR_INVALID_ARG for a NULL event or callbacks,
 *         IOT_ERR_NOT_SUPPORTED for an event type the contract does not own (nothing done).
 */
iot_err_t IOT_ContractHandleDeviceEvent(IOT_CoreDeviceEvent_t *event, const IOT_ContractCallbacks_t *cb);

#ifdef __cplusplus
}
#endif

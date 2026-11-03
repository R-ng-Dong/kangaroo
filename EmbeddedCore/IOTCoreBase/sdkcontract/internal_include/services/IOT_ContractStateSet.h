#pragma once

/*
 * The contract's state command: a request from the app, the cloud or an automation that sets an
 * attribute on one device, a group or every device of the location.
 *
 *   resolve the target to devices -> IOT_ContractAcceptStateSet() (refused: nothing happens) ->
 *   per device, per element: apply (the product's callback, with the device's own element ids) ->
 *   commit what was applied (store, Record) -> ONE state report for the whole command ->
 *   Smart per device with the request's sender.
 *
 * Partial failure: apply's first error other than IOT_ERR_NOT_FOUND stops that device; the
 * elements applied before it are committed and reported. Other devices carry on.
 *
 * Internal to the contract. Runs on the task that delivers core events, or on the timer task for
 * an automation's actions.
 */

#include <stdbool.h>
#include <stdint.h>
#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_ErrorManager.h"
#include "dao/IOT_DaoGroupMgmt.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The product's apply callback, as IOT_ContractCallbacks_t declares it. */
typedef iot_err_t (*IOT_ContractApplyFn)(const IOT_ContractApply_t *req);

/**
 * The devices a state command addresses.
 *
 * `eids` points at `inlineEid` for the single-device targets (device, voice) and at heap memory
 * for the expanded ones (location, group); `owned` says which. Release with
 * IOT_ContractStateFreeTargets(), which also frees `members`.
 *
 * Must NOT be copied by value: `eids` can point at this struct's own `inlineEid`.
 */
typedef struct
{
    uint16_t *eids;
    uint16_t count;
    bool owned;
    uint16_t inlineEid;
    IOT_GroupMemberList_t members;
} IOT_ContractStateTargets_t;

/**
 * @brief Expand a state command's target into the device EIDs it addresses.
 *
 * @return IOT_OK with count == 0 when the target addresses nothing on this device (no devices
 *         stored, or not a member of the group): not an error, nothing to do.
 */
iot_err_t IOT_ContractStateResolveTargets(const IOT_EventStateSetRequest_t *setData, IOT_ContractStateTargets_t *out);

/** @brief Release what IOT_ContractStateResolveTargets() allocated. Safe on a zeroed struct. */
void IOT_ContractStateFreeTargets(IOT_ContractStateTargets_t *set);

/**
 * @brief Run a state command on an already-resolved EID list.
 *
 * @param groupMembers Per-member element bindings for a group target; a zeroed list otherwise.
 * @return IOT_OK even when individual devices fail (each is logged and skipped), IOT_OK for a
 *         refused sync-on-off carrier, IOT_ERR_NO_MEM when the per-command arrays cannot be
 *         allocated (nothing applied).
 */
iot_err_t IOT_ContractStateApplySet(const uint16_t *eids, uint16_t numDevices, IOT_EventStateSetRequest_t *setData,
                                    IOT_GroupMemberList_t *groupMembers, IOT_ContractApplyFn apply);

/**
 * @brief The whole state command: resolve the target, then IOT_ContractStateApplySet().
 *
 * The request is modified in place (see IOT_ContractAcceptStateSet()).
 */
iot_err_t IOT_ContractStateHandleSet(IOT_EventStateSetRequest_t *setData, IOT_ContractApplyFn apply);

#ifdef __cplusplus
}
#endif

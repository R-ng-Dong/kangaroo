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
 * Internal to the contract. Runs on the task that delivers core events, an automation's actions
 * included.
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
 * The return is the command's answer to its sender (core sends it for an acknowledged command).
 *
 * @param groupMembers Per-member element bindings for a group target; a zeroed list otherwise.
 * @return IOT_OK when at least one device applied an element (the others are logged and skipped), for a
 *         refused sync-on-off carrier, for a group or location command whatever its members did,
 *         and when the device is not on this board (IOT_ERR_NOT_FOUND from it); for a device or
 *         voice command that moved nothing, the device's error (apply's, or IOT_ERR_NOT_SUPPORTED
 *         when no element could be driven or the device type does not match); IOT_ERR_NO_MEM
 *         when the per-command arrays cannot be allocated (nothing applied). A failed state report
 *         is logged, not returned: the change happened.
 */
iot_err_t IOT_ContractStateApplySet(const uint16_t *eids, uint16_t numDevices, IOT_EventStateSetRequest_t *setData,
                                    IOT_GroupMemberList_t *groupMembers, IOT_ContractApplyFn apply);

/**
 * @brief The whole state command: resolve the target, then IOT_ContractStateApplySet().
 *
 * The request is modified in place (see IOT_ContractAcceptStateSet()). The return is the
 * command's answer (see IOT_ContractStateApplySet()); IOT_OK when the target addresses nothing on
 * this device. The router writes it into the request's `result`.
 */
iot_err_t IOT_ContractStateHandleSet(IOT_EventStateSetRequest_t *setData, IOT_ContractApplyFn apply);

#ifdef __cplusplus
}
#endif

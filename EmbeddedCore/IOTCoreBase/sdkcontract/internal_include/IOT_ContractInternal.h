#pragma once
/*
 * Contract functions the contract calls for itself, internal to sdkcontract.
 *
 * IOT_ContractStart() runs a request end to end (apply, store, Record, report, Smart), joins and
 * removals, Smart events and the cloud-connect duties, so a product never calls these: a second
 * caller would record, report or evaluate the same change twice. They stay functions so the
 * contract's own paths and its tests share one implementation.
 */

#include <stdbool.h>
#include <stdint.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_Contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One element-state change, as the cloud/app contract needs to hear about it.
 *
 * Fill one of these whenever an element's state changes -- your hardware changed it, a
 * STATE_SET was applied, or a sub-device reported it -- and hand it to
 * IOT_ContractStateChanged(). The entry appends the change to Record and feeds Smart, so the
 * history and the automations see the same change in the same way on every device.
 *
 * For a STATE_SET, call IOT_ContractAcceptStateSet() first and fill this from the request as it
 * is AFTER that call: the attribute and senderFrom may have been rewritten.
 *
 * Two rules the contract cannot check for you:
 *  - Element ids are always the cloud's: the ids the cloud assigned at provisioning, as they
 *    arrive in requests and are stored in the Device store. Never your application's own
 *    element numbering. Translate before filling this (the commit does).
 *  - senderFrom is the sender of the request that caused the change, copied from that request
 *    (after IOT_ContractAcceptStateSet()). Do not substitute your own value: Smart's loop guard
 *    reads it, and a change an automation caused that arrives as a manual one fires the
 *    automations again (the contract logs a warning when it sees this shortly after an
 *    automation set the element).
 */
typedef struct
{
    uint16_t eid;            /**< Device EID: the root device's, or the sub-device's own. */
    const uint16_t *elemIds; /**< Element ids that changed, in the cloud's numbering (the ids the
                                  cloud assigned at provisioning, not your hardware's indices). */
    uint16_t numElems;       /**< Number of entries in elemIds; at least 1. */
    uint16_t attrId;         /**< Attribute that changed (IOT_ATTR_*). */
    const uint8_t *value;    /**< New attribute value bytes, as stored in the Device DAO. */
    uint16_t valueLen;       /**< Number of bytes in value. */
    uint8_t senderFrom;      /**< IOT_SENDER_* from the request context that caused the change.
                                  For a STATE_SET, read it from the request AFTER
                                  IOT_ContractAcceptStateSet(), which may have changed it.
                                  A change your device made on its own (a button, a sensor
                                  reading, a sub-device report) passes IOT_SENDER_DEVICE. */
} IOT_ContractStateChange_t;

/**
 * @brief Decide whether to act on a STATE_SET. Call it first in your STATE_SET handler, once the
 *        request is resolved to the device EID(s) it addresses, before you actuate anything.
 *
 * Owns the sync-on-off rule. With a sync-on-off automation, a paired device mirrors its on/off
 * to its partner as a STATE_SET carrying the sync-on-off carrier attribute. For such a request:
 *  - it is accepted only when there is at least one element and one device, and every named
 *    element of every device belongs to a sync-on-off or stair-switch pairing this device holds;
 *  - on accept, the attribute is rewritten to the ordinary on/off attribute and the sender is set
 *    to IOT_SENDER_AUTOMATION, so the Smart loop guard keeps the mirrored change from firing the
 *    pairing back (which would mirror back again, forever);
 *  - on refuse, a warning is logged and it returns false: do nothing with the request.
 * Any other request is left untouched and accepted.
 *
 * The request is modified IN PLACE. Read the attribute and set->ctx.senderFrom from the request
 * AFTER this call -- for actuating, for the state report and for IOT_ContractStateChange_t.
 *
 * Safe and cheap on every STATE_SET -- device-, group-, location- and voice-targeted, and those
 * Smart dispatches itself: an ordinary request costs one attribute comparison.
 *
 * The request may be modified even when this returns false: the attribute is rewritten before
 * the pairing is checked, so a refused request can already read as the ordinary on/off
 * attribute. Never act on a refused request, whatever it now says.
 *
 * Who answers when it returns false: not you. Return from your handler as you would for a
 * request you handled (IOT_OK). A refusal is normal -- a device that holds no pairing for the
 * named elements is the common case -- not an error. Core answers the sender itself, whatever your
 * handler returns: an acknowledged request gets its acknowledgement and a success result, an
 * unacknowledged one gets no reply. Send no state report for a refused request.
 *
 * Your attribute table must provide IOT_AttrResolveSyncOnOffCarrier() (it is in the reference
 * attribute table); an older table without it does not link.
 *
 * Blocks: only for a sync-on-off request, which reads the Smart trigger store; an ordinary
 * request does not. Safe from a timer callback: no. Task: your STATE_SET handler, on the task
 * that delivers core events.
 *
 * @param set        The STATE_SET request from the event; modified in place.
 * @param eids       The device EID(s) the request resolved to on this device.
 * @param numDevices Number of entries in eids.
 * @return true to act on the request (as it now reads), false to ignore it (also for a NULL set).
 */
bool IOT_ContractAcceptStateSet(IOT_EventStateSetRequest_t *set, const uint16_t *eids, uint16_t numDevices);

/**
 * @brief Append the change to Record: one entry per element.
 *
 * The device type and each element's type are looked up from the Device DAO, because Record
 * rules match on them; a failed lookup records the field as 0, which still lets an exact
 * element rule match. Whether anything is stored is Record's decision (its registered rules).
 *
 * Blocks: yes -- reads the Device store, waits up to about a second for Record's lock, and may
 * read storage when a new history block opens. Safe from a timer callback: no. Task: any.
 *
 * @return IOT_OK, or IOT_ERR_INVALID_ARG for a NULL change, no elements, or a NULL value with
 *         a non-zero length.
 */
iot_err_t IOT_ContractRecordStateChange(const IOT_ContractStateChange_t *change);

/**
 * @brief Queue Smart trigger evaluation for the change: one deferred evaluation, all elements.
 *
 * Loop guard: a change caused by a scenario, automation, schedule, countdown or voice sender
 * (senderFrom >= IOT_SENDER_SCENARIO_UUID_V4) is NOT fed to Smart and this returns IOT_OK.
 * Such a change is itself the output of Smart; feeding it back would re-fire triggers and
 * chain automations.
 *
 * Precondition: the new state is already written to the Device DAO. Condition-group
 * evaluation re-reads element state from it.
 *
 * Blocks: no -- the change is copied and the evaluation runs later on the timer task. Safe from
 * a timer callback: yes. Task: any.
 *
 * @return IOT_OK (also when the loop guard skips the evaluation), IOT_ERR_INVALID_ARG as for
 *         IOT_ContractRecordStateChange(), or the error from queueing the evaluation.
 */
iot_err_t IOT_ContractEvaluateSmart(const IOT_ContractStateChange_t *change);

/**
 * @brief Record the change, then feed Smart: IOT_ContractRecordStateChange() followed by
 *        IOT_ContractEvaluateSmart().
 *
 * Same precondition as IOT_ContractEvaluateSmart(): write the new state to the Device DAO
 * first. This does not write the Device DAO and does not report state to the cloud; both stay
 * with the caller. A caller that must do something between the two steps (for example, send
 * the state report and skip Smart when the report could not read the device) calls the two
 * halves itself.
 *
 * Blocks: yes, as IOT_ContractRecordStateChange(). Safe from a timer callback: no. Task: any.
 *
 * @return IOT_OK, IOT_ERR_INVALID_ARG (nothing done), or the first error from either step.
 */
iot_err_t IOT_ContractStateChanged(const IOT_ContractStateChange_t *change);

/**
 * @brief Bring the contract stores up: Device, Smart triggers, Smart schedules, the
 *        condition-group state, IR and Record.
 *
 * IOT_ContractStart() calls it first. Once per start, after the platform storage is initialised
 * and before the first event. The custom history store (IOT_ContractRecordUseCustomStore()) is
 * installed first, so Record setup sees it. The
 * Device store is brought up next and a failure there stops the sequence.
 *
 * Blocks: yes -- scans storage and rebuilds Record's state. Safe from a timer callback: no.
 * Task: the boot task (app_main), once, before core starts delivering events.
 *
 * @param config Boot-time choices, or NULL for all defaults.
 * @return IOT_OK, or the error from the Device store (nothing else was started).
 */
iot_err_t IOT_ContractInit(const IOT_ContractConfig_t *config);

/**
 * @brief Forget one device everywhere the contract keeps data for it.
 *
 * Removes the device from the Device store, from every group, and clears its Smart data,
 * Smart triggers, Record history and IR codes. Every store is tried even when an earlier one
 * fails. Anything of yours that needs the device record (for example retiring a mesh node)
 * must run BEFORE this call, because the record is gone afterwards.
 *
 * Blocks: yes (storage erases). Safe from a timer callback: no. Task: any.
 *
 * @return IOT_OK, or IOT_ERR_FAIL when the Device, Group or Smart removal failed. A failure
 *         to clear Record, triggers or IR alone does not change the result.
 */
iot_err_t IOT_ContractRemoveDevice(uint16_t eid);

/**
 * @brief The cloud connection came up. Call on EVERY cloud connect, including reconnects.
 *
 * Kicks Record's history push, starts the Smart scheduler (stored schedules run again after
 * a reboot) and re-syncs the condition groups with their peers, whose changes may have been
 * missed while offline. Safe to call repeatedly: it starts no second scheduler timer and
 * queues at most one history push. Does not wait for the time to be known; the scheduler
 * waits for it itself.
 *
 * With IOT_ContractStart() the contract calls this itself on every cloud connect; call it
 * nowhere else.
 *
 * Blocks: no -- it only queues work and arms timers. Safe from a timer callback: yes. Task: any.
 */
void IOT_ContractOnCloudConnected(void);

/**
 * @brief One state-change report for one device. Zero-initialise, then set what you need.
 */
typedef struct
{
    uint16_t eid;                   /**< Device EID: the root device's, or the sub-device's own. */
    const uint16_t *changedElemIds; /**< Element ids that changed. NULL means "every element this
                                         device has" -- the full-sync shape: numChangedElems is
                                         then ignored, the list is built from the device's stored
                                         state, and changedAttrId is taken from the first attribute
                                         of the first element. */
    uint16_t numChangedElems;       /**< Number of entries in changedElemIds. */
    uint16_t changedAttrId;         /**< The attribute that changed (IOT_ATTR_*), the same one for
                                         every element in changedElemIds. */
    bool pushNotify;                /**< true asks the cloud to send a push notification to the
                                         user's phone. Normally true only for safety or alert events
                                         (smoke, tamper, low battery); false for ordinary changes. */
    IOT_RequestContext_t ctx;       /**< Routing context. Pass through from the request that caused
                                         the change, or {0} for a local/unsolicited one. */
} IOT_ContractStateReport_t;

/**
 * @brief Build and send one device's state-change report.
 *
 * @return IOT_OK on success. IOT_ERR_INVALID_ARG for a NULL request.
 *         IOT_ERR_DATA_NOT_FOUND means the report could not even be assembled —
 *         the device's stored element state could not be read, came back empty,
 *         or its identity info could not be read — and nothing was sent.
 *         IOT_ERR_NO_MEM means the same: assembly failed (the list of every element,
 *         for the full-sync shape) before any send
 *         was attempted, so nothing was sent either. Any OTHER non-OK return
 *         (e.g. from the underlying transport) means the report WAS assembled
 *         from real state but the send itself failed. Callers that gate a side
 *         effect on "did the device's state actually get read" (e.g. whether
 *         to still evaluate smart triggers) must test specifically for
 *         IOT_ERR_DATA_NOT_FOUND (and, for full correctness, IOT_ERR_NO_MEM),
 *         not for `!= IOT_OK` — a send failure is not a reason to skip that
 *         side effect.
 *
 * Blocks: yes -- reads the Device store and hands the report to the cloud connection. Safe from
 * a timer callback: no. Task: any.
 */
iot_err_t IOT_ContractReportState(const IOT_ContractStateReport_t *req);

/**
 * @brief Handle an IOT_EVENT_SMART event. Call it from your Smart event callback.
 *
 * Blocks: yes -- reads and writes the Smart stores and may send replies. Safe from a timer
 * callback: no. Task: your IOT_EVENT_SMART callback, on the task that delivers core events.
 *
 * @return IOT_OK, or the error from handling the event.
 */
iot_err_t IOT_ContractHandleSmartEvent(IOT_CoreSmartEvent_t *smartEvent);

#ifdef __cplusplus
}
#endif

#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include "IOT_CoreEventHandler.h"
#include "IOT_ErrorManager.h"

/**
 * SDK-owned smart automation service.
 *
 * Event handlers adapt core SMART events into this service. The service owns
 * smart command execution, trigger evaluation, schedule ticking, persistence
 * updates, and cloud smart reports.
 */

// ============================================================================
// subSmartType — WHAT KIND of action a matched automation performs.
//
// This is one axis. The other, independent, axis is HOW MANY conditions the
// automation has, which is carried by the bind's condition-group description: one seat
// is an ordinary trigger, two or more is a distributed cond-group. The two
// compose freely — e.g. a notification with two conditions is
// SMART_SUB_TYPE_NOTIFICATION plus a two-seat condition group.
//
// These are classified on subSmartType, not on `smartType` (0=scene/1=schedule/
// 2=automation): they describe the action, not the automation class. `smartType`
// is parsed and stored, but nothing dispatches on it.
// ============================================================================


#ifdef __cplusplus
extern "C" {
#endif
#define SMART_SUB_TYPE_DEFAULT                     0  // run the stored bind commands
#define SMART_SUB_TYPE_STAIR_SWITCH                64 // two switches mirror ON/OFF; action synthesised from the trigger mix
#define SMART_SUB_TYPE_NOTIFICATION                65 // push notification, no bind cmds
#define SMART_SUB_TYPE_DETECTED_MOTION             66
#define SMART_SUB_TYPE_DETECT_NO_MOTION_BY_EVENT   67
#define SMART_SUB_TYPE_DETECT_NO_MOTION_BY_TIME    68
#define SMART_SUB_TYPE_REVERSE_ON_OFF              70 // auto-reverse; action synthesised from the trigger, no bind cmd

// One device mirrors ON/OFF across a LIST of its own elements (elemList) and
// broadcasts to peers on the same SMID. Handled by its own dispatch arm before
// the group/ordinary split, so it never occupies a scoreboard seat.
#define SMART_SUB_TYPE_SYNC_ON_OFF 71

/**
 * Execute stored bind commands for a smart automation.
 */
iot_err_t SmartService_ExecuteCommands(uint16_t smid);

/**
 * Execute stored bind commands using a trigger source value for sync attrs.
 */
iot_err_t SmartService_ExecuteCommandsEx(uint16_t smid,
                                         const uint8_t *triggerValue,
                                         uint16_t triggerValueSize);

/**
 * Mark/query locally fired triggers to suppress cloud echo re-execution.
 */
void SmartService_MarkLocalTrigger(uint16_t smid);
bool SmartService_IsRecentLocalTrigger(uint16_t smid);

/**
 * Execute an incoming ACTIVE_BY_TRIGGER at most once per dedup window. Skips if the SMID
 * was recently executed (own echo, or a duplicate from a second cond-group device that
 * fired in the cond-group convergence race), else runs the bind-cmds and marks the SMID. Returns
 * IOT_OK for a cross-device broadcast with no bind-cmds here (normal). Use this from the
 * ACTIVE_BY_TRIGGER handler so non-idempotent actions never run twice.
 */
iot_err_t SmartService_ExecuteTriggerDeduped(uint16_t smid, const uint8_t *triggerValue,
                                             uint16_t triggerValueSize);

/**
 * Execute a schedule's bind commands at most once for a given due minute.
 *
 * A scheduled automation arrives twice: the cloud pushes ACTIVE_BY_SCHEDULE carrying the
 * due minute-of-week, and the offline tick independently matches the same minute against
 * the stored schedule. Both call this, keyed on (smid, minuteOfWeek), so a non-idempotent
 * action runs once. The claim expires well inside a week, so the weekly recurrence of the
 * same slot still runs.
 *
 * NOT the trigger dedup above: that keys on SMID alone over a 2 s window, far shorter than
 * the up-to-60 s gap between the cloud push and the tick that sees the same minute.
 *
 * @param smid         Smart automation ID
 * @param minuteOfWeek UTC0 minute-of-week the run is due at (0..10079)
 * @return IOT_OK when skipped as a duplicate, else the execution result.
 */
iot_err_t SmartService_ExecuteSchedule(uint16_t smid, uint16_t minuteOfWeek);

/**
 * Serialize and persist bind commands. Returns the device-owned cfm.
 */
iot_err_t SmartService_StoreBindCmd(IOT_CoreSmartEventBindCmdData_t *data, uint16_t *outCfm);

/**
 * Evaluate stored smart triggers for a device state change.
 */
iot_err_t SmartService_TriggerEvaluate(uint16_t eid, dev_set_info_t *devSetInfo);

/**
 * Schedule trigger evaluation on the timer thread.
 */
iot_err_t SmartService_TriggerEvaluateDeferred(uint16_t eid, dev_set_info_t *devSetInfo);

/**
 * Start the offline smart schedule ticker.
 */
void SmartService_SchedulerStart(void);

/**
 * Convert a Unix timestamp to minute-of-week in UTC.
 *
 * The cloud sends offline schedule times as UTC0 minute-of-week values, so the
 * scheduler must compare against UTC (not the local device timezone).
 */
iot_err_t SmartService_GetCurrentUtcMinuteOfWeek(uint64_t unixMs, uint16_t *outMinuteOfWeek);

/**
 * Enable or disable a smart entity (automation or schedule) by SMID.
 *
 * The command carries no automation-vs-schedule discriminator, so the flag is
 * applied to BOTH DAOs, each guarded so only the DAO that actually owns the
 * SMID is written:
 *   - trigger DAO self-guards (Get-first) — no write if SMID is not a trigger;
 *   - schedule DAO is gated on an existing schedule to avoid stray keys.
 *
 * @return IOT_OK if the SMID matched at least one DAO; IOT_ERR_NOT_FOUND otherwise.
 */
iot_err_t SmartService_SetEnabledBySmid(uint16_t smid, bool enabled);

/**
 * Enable or disable EVERY automation on this device at once (one switch for all).
 *
 * Scope is all TRIGGER SMIDs, which is what the app calls an "automation". A pure
 * schedule with no trigger is not included -- schedules are a separate list in the app
 * and there is no bulk schedule DAO. An SMID that happens to own both is covered, because
 * SmartService_SetEnabledBySmid writes whichever DAO owns it.
 *
 * A stale SMID in the list is skipped rather than aborting the sweep, and zero
 * automations is success.
 *
 * @return IOT_OK unless the automation list itself could not be read.
 */
iot_err_t SmartService_SetEnabledAll(bool enabled);

/**
 * @brief May a mirror carrier (attribute 311 or 313) drive this element?
 *
 * Those attributes mean "apply this ON/OFF but do NOT evaluate your own triggers". Honoured
 * for any sender, they would let any device on the location move any of our elements, and
 * more quietly than an ordinary control, because trigger evaluation is skipped.
 *
 * So they are accepted only for an element that genuinely participates in a mirror: an
 * ENABLED stair-switch (64) or sync-on-off (71) bind we hold. For anything else the carrier
 * gets no special treatment.
 *
 * @return true if a carrier is legitimate for this element.
 */
bool SmartService_ElementAcceptsMirror(uint16_t eid, uint16_t elemId);

/**
 * Apply a received ACTIVE_BY_SYNC_ON_OFF broadcast (subSmartType 71).
 *
 * Looks up this device's own trigger record by SMID; if it is an enabled
 * sync-on-off trigger bound to the same attrId, applies the ON/OFF value to
 * EVERY element in the trigger's elemList (all bound elements mirror together)
 * via a local STATE_SET (senderFrom = IOT_SENDER_AUTOMATION, so the change never
 * re-enters trigger evaluation). Skips silently when this device is not a
 * participant.
 *
 * @param senderEid  Root EID of the broadcasting device; 0 = untagged
 *                   sender. Equal to our own root EID => own echo, skipped
 *                   (reliable regardless of delivery timing).
 * @return IOT_OK (including skip cases); IOT_ERR_INVALID_ARG on empty value.
 */
iot_err_t SmartService_ApplySyncOnOff(uint16_t smid, uint16_t attrId,
                                      const uint8_t *attrValue, uint16_t attrSize,
                                      uint16_t senderEid);

/**
 * Parse a sync-on-off element list (attrId IOT_ATTR_SYNC_ONOFF_ELEMENTS 315) from
 * the primary attr value (a list of element ids). Called by the
 * BIND_TRIGGER handler when subSmartType == SMART_SUB_TYPE_SYNC_ON_OFF.
 *
 * @param value  raw attr-value bytes (may be NULL)
 * @param size   number of bytes; a trailing odd byte is ignored
 * @param out    destination array (at least `cap` entries)
 * @param cap    max ids to write
 * @return number of element ids written (0 on NULL/empty), <= cap.
 */
uint8_t SmartService_ParseSyncOnOffElements(const uint8_t *value, uint16_t size, uint16_t *out,
                                            uint8_t cap);
#ifdef __cplusplus
}
#endif

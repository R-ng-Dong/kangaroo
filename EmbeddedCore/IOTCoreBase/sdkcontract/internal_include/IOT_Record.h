#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "IOT_ErrorManager.h"
#include "IOT_ContractRecord.h"  /* the partner Record API: events, rules, registration */
#include "IOT_CoreEventHandler.h" /* IOT_RequestContext_t */
#include "IOT_CoreRecord.h"       /* IOT_CoreRecordStateBlock_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Argument check shared by IOT_ContractRecordRegisterRule() with Record on and off, so a
 *        rule is rejected the same way in both builds.
 *
 * The width is a format contract, not a free number: the app parses a day as a fixed stride of
 * 16-bit fields, so an odd width desynchronises every entry after the first, and an unbounded
 * one reaches fixed buffers on the read path.
 */
static inline bool IOT_RecordRuleIsValid(const IOT_ContractRecordRule_t *rule)
{
    return rule != NULL && rule->elemType != 0 && rule->attrId != 0
           && rule->logValueBytes <= IOT_CONTRACT_RECORD_MAX_LOG_VALUE_BYTES && (rule->logValueBytes & 1u) == 0;
}

/** @brief Argument check shared by IOT_ContractRecordSetElementEnabled() with Record on and off. */
static inline bool IOT_RecordOverrideIdsAreValid(uint16_t eid, uint16_t elemId, uint16_t attrId)
{
    return eid != 0 && elemId != 0 && attrId != 0;
}

/**
 * @brief Prepare the Record subsystem. Called by IOT_ContractInit(); apps do not call it.
 *
 * Creates the instance mutex and rebuilds each instance's ring index from storage, unless an
 * append override is installed.
 *
 * @return IOT_OK; IOT_ERR_NO_MEM or IOT_ERR_TIMEOUT when the lock cannot be set up, in which
 *         case recording stays off for this boot.
 */
iot_err_t IOT_RecordInit(void);

/**
 * @brief Replace the append path of Record.
 *
 * While an override is set, IOT_RecordAppend() hands every captured event to @p fn and
 * touches no V2 state, and IOT_RecordInit() skips V2 setup. Install it before
 * IOT_RecordInit(). Passing NULL restores V2.
 *
 * @param fn Override routine, or NULL to restore the default.
 * @return IOT_OK.
 */
iot_err_t IOT_RecordSetAppendOverride(IOT_ContractRecordAppendOverrideFn fn);

/**
 * @brief Whether an append override is installed, so Record's own history is not the one kept.
 *
 * The contract reads it to decide who answers a sensor-log request: the product, whose override
 * keeps that history, or the contract itself with an empty reply. Always false with Record off,
 * which ignores the override.
 */
bool IOT_RecordHasAppendOverride(void);

/**
 * @brief Maps one of the device's own element ids to the cloud's element id.
 *
 * @param rootEid   The device's own EID.
 * @param appElemId An element id in the device's own numbering.
 * @return The cloud's element id for it, or @p appElemId unchanged when it cannot be mapped
 *         (for example before provisioning).
 */
typedef uint16_t (*IOT_ContractRecordElemIdResolverFn)(uint16_t rootEid, uint16_t appElemId);

/**
 * @brief Let IOT_ContractRecordSetElementEnabled() accept the device's own element ids.
 *
 * Stored history is keyed by the cloud's element ids. The contract installs its element map here
 * when the product declares its elements (IOT_ContractElementsDeclare()), and
 * IOT_ContractRecordSetElementEnabled() then translates ids for the device's own EID through it.
 * Without a resolver, ids are used as given. IOT_ContractInit() removes it; passing NULL does too.
 *
 * @param fn Resolver, or NULL.
 * @return IOT_OK.
 */
iot_err_t IOT_RecordSetElementIdResolver(IOT_ContractRecordElemIdResolverFn fn);

/**
 * @brief Check whether a state-change event should be recorded.
 *
 * This evaluates exact overrides, semantic rules, valueSize, and the optional
 * filter callback. It does not write to storage.
 *
 * @param event Event to evaluate.
 * @return true if the event should be recorded, false otherwise.
 */
bool IOT_RecordShouldCapture(const IOT_ContractRecordEvent_t *event);

/**
 * @brief Append a record if the event matches registered Record configuration.
 *
 * Normal STATE_SET handling calls this automatically. App-writers only need to
 * call it for custom data paths that bypass normal SDK state handling.
 *
 * @param event Event to evaluate and append.
 * @return IOT_OK when skipped or appended successfully; DAO error on append failure.
 */
iot_err_t IOT_RecordAppend(const IOT_ContractRecordEvent_t *event);

/**
 * @brief Default timing for an attribute, used where a rule says IOT_CONTRACT_RECORD_USE_DEFAULT.
 *
 * Exposed so an app-writer can see what they are inheriting, and check the inheritance
 * rather than the numbers. Both outputs are always written.
 *
 * The heartbeat defaults are floored deliberately -- see the heartbeatSec field for the
 * arithmetic.
 */
void IOT_RecordGetAttrDefaults(uint16_t attrId, uint16_t *outMinIntervalSec,
                               uint16_t *outHeartbeatSec);

/**
 * @brief Flush every dirty open block to storage.
 *
 * Called by the Record timer and by IOT_ContractRecordClearRules(). NOT by factory reset,
 * which wipes and reboots.
 *
 * @return IOT_OK when everything dirty was written; the last error otherwise, with those
 *         instances left dirty for the next attempt.
 */
iot_err_t IOT_RecordFlushAll(void);

/**
 * @brief Flush every dirty open block, BLOCKING until each write completes.
 *
 * For the one case that cannot tolerate a queued write: deinit or a planned restart
 * (IOT_ContractPrepareRestart()), after which anything still queued may be dropped.
 * Everywhere else use IOT_RecordFlushAll(), which queues and returns.
 */
iot_err_t IOT_RecordFlushAllSync(void);

/**
 * @brief Close any held window whose deadline has passed, writing the settled value.
 *
 * Called by the Record timer. The value written is the LAST one seen in the window, not
 * the first deviation, and it is stamped from when it arrived -- so a reading that
 * wandered and came back writes nothing at all.
 *
 * @return The earliest remaining window deadline in epoch seconds, or 0 when nothing is
 *         pending. The caller arms a one-shot for it, so an idle device arms nothing:
 *         a periodic tick would wake the CPU 86,400 times a day to find no work.
 */
int32_t IOT_RecordServiceWindows(void);

/**
 * @brief Ask for closed history blocks that the cloud has not received yet to be pushed to it.
 *
 * Call it on every cloud connect, so history recorded while the device was offline reaches
 * the cloud without waiting for the next block to close. Record calls it itself when a block
 * closes and at the end of IOT_RecordInit().
 *
 * Safe from any task: it only queues work for the core task, which paces the pushes itself.
 * Does nothing when CONFIG_IOT_RECORD_PUSH_ENABLED is off or an append override
 * (IOT_RecordSetAppendOverride()) is installed.
 */
void IOT_RecordPushKick(void);

/**
 * @brief The current block of every recorded attribute of one device, for a caller that
 *        is building its own reply.
 *
 * Unlike IOT_ContractRecordServeGet(), which SENDS a notification of its own, this hands the
 * blocks back so they can ride on a reply core is still assembling -- which is how a
 * state-request answer carries each attribute's current checkpoint without the app needing
 * a second round trip.
 *
 * Instances whose checkpoint is still provisional (recorded before the clock synced and
 * not yet corrected) are OMITTED. Returning one would hand the app a checkpoint derived
 * from the monotonic clock, which it would then anchor to.
 *
 * Blocks: yes -- waits up to about a second for Record's lock and may read storage. Safe from
 * a timer callback: no. Task: the handler building the reply (the contract's state-request
 * handler).
 *
 * @param eid        Device to report. Only this EID's own instances are returned; a hub
 *                   does not attach its children's blocks.
 * @param outBlocks  Caller-owned array, filled with at most @p maxBlocks entries. The
 *                   `fields` pointers are borrowed and valid until the next Record call.
 * @param maxBlocks  Capacity of @p outBlocks.
 * @param outCount   Number filled. Zero is a normal answer, not an error: the device may
 *                   record nothing for this EID.
 * @return IOT_OK, or IOT_ERR_INVALID_ARG for a NULL @p outBlocks or @p outCount.
 */
iot_err_t IOT_ContractRecordGetCurrentBlocks(uint16_t eid, IOT_CoreRecordStateBlock_t *outBlocks,
                                     uint8_t maxBlocks, uint8_t *outCount);

/**
 * @brief Answer the app's history request by walking the chain and sending the reply.
 *
 * The walk is INCLUSIVE -- the block at `checkpoint` first, then its prev, newest first
 * -- and `checkpoint` 0 means the open block, served from RAM so entries no flush has
 * reached yet are not missed.
 *
 * An unknown or evicted checkpoint, and an instance this device never recorded, both
 * answer with ONE block carrying the requested checkpoint and no entries. Never zero
 * blocks and never a checkpoint of core's own invention: 0 collides with both the
 * end-of-chain sentinel and "give me the current block".
 *
 * Call it SYNCHRONOUSLY from the event callback. ctx->senderId points into the message
 * reader's buffer and is freed when the handler returns.
 *
 * Blocks: yes -- waits up to about a second for Record's lock, reads storage and sends the
 * reply. Safe from a timer callback: no. Task: the contract's history-request handler, before it
 * returns.
 *
 * @return IOT_OK, IOT_ERR_INVALID_ARG for a null ctx, IOT_ERR_TIMEOUT if the Record
 *         table could not be locked, or core's error from building the reply.
 */
iot_err_t IOT_ContractRecordServeGet(uint16_t eid, uint16_t elemId, uint16_t attrId,
                             uint64_t checkpoint, const IOT_RequestContext_t *ctx);

#ifdef __cplusplus
}
#endif

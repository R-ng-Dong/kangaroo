#pragma once

/*
 * The holding table: reports made before the device is provisioned. Until the device has a root
 * EID there is no element to store the value under, so the report is kept here, one slot per
 * (element, attribute) with only the latest value, and sent when the first full state report
 * goes out.
 *
 * Internal to the contract.
 */

#include <stdbool.h>
#include <stdint.h>
#include "IOT_ErrorManager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IOT_CONTRACT_PENDING_MAX 32       /* (element, attribute) slots */
#define IOT_CONTRACT_PENDING_VALUE_MAX 16 /* bytes = 8 fields */

/**
 * @brief Keep the latest value of one (element, attribute) until the device is provisioned.
 *
 * A key already held is overwritten. Safe from any task once IOT_ContractPendingInit() has run
 * (IOT_ContractInit() does); a report made earlier creates the lock itself, which is safe only
 * because no other task can touch the table before IOT_ContractStart().
 *
 * After IOT_ContractPendingFlush() nothing is held any more: the first full state report has
 * gone out, so the caller must send the value itself (the device is provisioned by then).
 * @return IOT_OK. IOT_ERR_INVALID_ARG for a NULL value or a zero len, IOT_ERR_INVALID_SIZE when
 *         len exceeds IOT_CONTRACT_PENDING_VALUE_MAX, IOT_ERR_NO_MEM when the table is full and
 *         the key is new (logged as a warning) or when the table's lock could not be created,
 *         IOT_ERR_INVALID_STATE when the table has already been flushed (nothing held). Waits
 *         for a flush in progress to finish, however long it takes.
 */
iot_err_t IOT_ContractPendingHold(uint16_t elemId, uint16_t attrId, const uint8_t *value, uint16_t len);

/**
 * Create the table's lock if it does not exist yet. Called from IOT_ContractInit(). Changes
 * nothing else: a value held by a report made before IOT_ContractStart() is kept.
 */
void IOT_ContractPendingInit(void);

/**
 * Write every held value into the Device store of @p rootEid as the element's starting state,
 * then empty the table. A value is stored, not reported: no Record entry, no state report, no
 * Smart evaluation, no event. A value the store refuses is logged and dropped (a value the model
 * does not hold will never fit).
 *
 * Marks the table flushed (until IOT_ContractPendingReset()) and writes every slot under the one
 * lock that guards the table. A concurrent IOT_ContractPendingHold() either landed before the drain
 * (and is written) or sees the mark and is refused, so a report that raced the flush is not stranded; and a
 * concurrent IOT_ContractPendingDiscard() either removes the slot before it is written or runs
 * after, so a held value is never stored over a value reported later. Calling it again is harmless.
 */
void IOT_ContractPendingFlush(uint16_t rootEid);

/**
 * Forget the held value of one (element, attribute), if any. IOT_ContractReportFields() calls it
 * before every local change it sends, so a value held earlier is never stored over one reported
 * later. Waits for a flush in progress to finish, however long it takes.
 */
void IOT_ContractPendingDiscard(uint16_t elemId, uint16_t attrId);

/** Number of held (element, attribute) slots. */
uint8_t IOT_ContractPendingCount(void);

/** True once IOT_ContractPendingFlush() has run (and IOT_ContractPendingReset() has not since). */
bool IOT_ContractPendingFlushed(void);

/**
 * Drop every held value and clear the flushed mark, as at a fresh boot: reports are held again
 * until the next flush. IOT_ContractFactoryReset() calls it (a root removal runs that reset), so a
 * device that loses its root without restarting holds its reports for the next provisioning.
 */
void IOT_ContractPendingReset(void);

#ifdef __cplusplus
}
#endif

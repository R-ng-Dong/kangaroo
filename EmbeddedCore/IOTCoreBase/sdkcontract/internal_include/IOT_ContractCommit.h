#pragma once

/*
 * The contract's commit: what happens to a change the hardware (or a sub-device's transport) has
 * taken. Every path that changes an element's state runs the same steps in the same order:
 *
 *     store  ->  Record  ->  state report  ->  Smart (with the request's sender)
 *
 * The steps are split in two because a state command that addresses several devices sends ONE
 * report for all of them: the caller stores and records each device (IOT_ContractCommitStore()),
 * sends the report, and only then announces each device (IOT_ContractCommitAnnounce()). A path
 * that changes one device runs the same two calls around its own single-device report.
 *
 * Internal to the contract.
 */

#include <stdint.h>
#include "IOT_Contract.h" /* IOT_ContractCause_t */
#include "IOT_ErrorManager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One committed change: one attribute value on one or more elements of one device. */
typedef struct
{
    uint16_t eid;            /**< Device EID: the root device's, or a sub-device's own. */
    const uint16_t *elemIds; /**< The cloud's element ids that changed (the Device store's ids). */
    uint16_t numElems;       /**< Number of entries in elemIds; at least 1. */
    uint16_t attrId;         /**< The attribute that changed. */
    const uint8_t *value;    /**< The new value bytes. */
    uint16_t valueLen;       /**< Number of bytes in value. */
    uint8_t senderFrom;      /**< IOT_SENDER_* of the request that caused the change. */
} IOT_ContractCommit_t;

/**
 * @brief Steps 1 and 2: write the new value to the Device store, then append it to Record.
 *
 * Record runs only when the store write succeeded: a change that is not stored is not a change.
 *
 * @return IOT_OK, IOT_ERR_INVALID_ARG for a NULL commit or no elements, IOT_ERR_NO_MEM, or the
 *         Device store's error (nothing recorded).
 */
iot_err_t IOT_ContractCommitStore(const IOT_ContractCommit_t *c);

/**
 * @brief The step after the report: feed Smart with the request's sender (its loop guard skips
 *        Smart's own output), then raise IOT_CONTRACT_EVT_STATE_COMMITTED with @p cause (the
 *        path that committed the change).
 */
void IOT_ContractCommitAnnounce(const IOT_ContractCommit_t *c, IOT_ContractCause_t cause);

#ifdef __cplusplus
}
#endif

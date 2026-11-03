#pragma once

/*
 * Raising the contract's events (IOT_Contract.h, IOT_ContractSubscribe()). Internal to the
 * contract.
 *
 * Each call is made AFTER the action it reports has finished, on the task that did it. Element
 * ids are passed in the cloud's numbering (the stores' ids) and translated here, once, to the
 * device's own numbering; an element with no device counterpart is left out, and an event left
 * with no element is not raised. With no subscriber, every call returns at once (no store read).
 */

#include <stdbool.h>
#include <stdint.h>
#include "IOT_Contract.h" /* IOT_ContractCause_t */

#ifdef __cplusplus
extern "C" {
#endif

/** A device's change was committed (store, Record, report, Smart) by the path @p cause. */
void IOT_ContractEventStateCommitted(uint16_t eid, const uint16_t *cloudElemIds, uint16_t numElems, uint16_t attrId,
                                     const uint8_t *value, uint16_t valueLen, uint8_t senderFrom,
                                     IOT_ContractCause_t cause);

/** This device ran automation @p smid's actions. */
void IOT_ContractEventAutomationFired(uint16_t smid);

/** A history block of (eid, element, attribute) was closed. */
void IOT_ContractEventRecordBlockClosed(uint16_t eid, uint16_t cloudElemId, uint16_t attrId);

/** @p blockCount closed history blocks of (eid, element, attribute) were sent. */
void IOT_ContractEventRecordPushed(uint16_t eid, uint16_t cloudElemId, uint16_t attrId, uint16_t blockCount);

/** A sub-device finished joining. */
void IOT_ContractEventDeviceAdded(uint16_t eid);

/** A device left every contract store (@p root: this device, all stores wiped). */
void IOT_ContractEventDeviceRemoved(uint16_t eid, bool root);

/** A mirrored on/off command for @p eid was refused at element @p cloudElemId. */
void IOT_ContractEventMirrorRefused(uint16_t eid, uint16_t cloudElemId);

#ifdef __cplusplus
}
#endif

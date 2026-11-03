#pragma once

/*
 * Echo recognition for sub-devices.
 *
 * When the contract applies a state change to a sub-device, the sub-device's radio reports the new
 * state back a moment later. That report is the change confirming itself, not a new change: it
 * must not be recorded, reported or fed to Smart a second time (and must not lose the original
 * request's sender to "device"). The state command notes each sub-device change it committed;
 * IOT_ContractSubDeviceReport() consumes a matching note.
 *
 * Internal to the contract.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How long after the change a matching report still counts as its echo. */
#define IOT_CONTRACT_ECHO_WINDOW_MS 3000
/** How many applied changes are remembered; the oldest is overwritten. */
#define IOT_CONTRACT_ECHO_RING 8
/** Longest value remembered. A longer value is not noted, so its echo is treated as a change. */
#define IOT_CONTRACT_ECHO_MAX_VALUE 16

/**
 * @brief Remember that the contract applied and committed this change to a sub-device.
 *
 * One entry per element. Safe from any task (a short critical section, no blocking, no logging
 * inside it).
 */
void IOT_ContractEchoNote(uint16_t eid, const uint16_t *elemIds, uint16_t numElems, uint16_t attrId,
                          const uint8_t *value, uint16_t len, uint8_t senderFrom);

/**
 * @brief Is this report the echo of a change the contract applied?
 *
 * True when an entry with the same (eid, elemId, attrId) and the same value bytes was noted no more
 * than IOT_CONTRACT_ECHO_WINDOW_MS ago. A match consumes its entry, so one change confirms at most
 * once. @p outSenderFrom (may be NULL) receives the original request's sender.
 */
bool IOT_ContractEchoConsume(uint16_t eid, uint16_t elemId, uint16_t attrId, const uint8_t *value, uint16_t len,
                             uint8_t *outSenderFrom);

#ifdef __cplusplus
}
#endif

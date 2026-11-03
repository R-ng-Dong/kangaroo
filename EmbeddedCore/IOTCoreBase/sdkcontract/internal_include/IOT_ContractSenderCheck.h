#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Remember that Smart has just dispatched a STATE_SET for this element and attribute.
 *
 * Keeps the last four dispatches. Safe from any task or a timer callback (a short critical
 * section, no blocking).
 */
void IOT_ContractSenderCheckNoteDispatch(uint16_t eid, uint16_t elemId, uint16_t attrId);

/**
 * @brief Is this state change an automation's own output reported with a manual sender?
 *
 * True when @p senderFrom is below the automation range (IOT_SENDER_SCENARIO_UUID_V4) and Smart
 * dispatched a STATE_SET for the same (eid, elemId, attrId) within the last 1000 ms. A match
 * consumes its entry, so one dispatch warns at most once, and logs the warning. A change with an
 * automation sender also consumes its entry, without a warning: the automation's output was
 * reported correctly, and a manual change shortly after it is a real one.
 */
bool IOT_ContractSenderCheckIsSuspect(uint16_t eid, uint16_t elemId, uint16_t attrId, uint8_t senderFrom);

#ifdef __cplusplus
}
#endif

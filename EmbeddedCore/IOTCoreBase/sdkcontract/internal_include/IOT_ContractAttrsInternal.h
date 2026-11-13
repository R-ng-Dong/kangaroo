#pragma once

/**
 * @file IOT_ContractAttrsInternal.h
 * @brief The attribute table's lookups, as the contract's own code calls them.
 *
 * Partners read the table through IOT_ContractAttrSize() and IOT_ContractAttrIsKnown()
 * (IOT_ContractAttrs.h). These are the table's working functions. Each one answers for the
 * standard attributes first, then for the ones registered with IOT_ContractRegisterAttrs().
 */

#include <stdbool.h>
#include <stdint.h>

#include "IOT_ContractAttrs.h"
#include "IOT_ErrorManager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Size in bytes of an attribute's value. Unknown ids are one field (2 bytes). */
uint16_t IOT_AttrGetSize(uint16_t attrId);

/** True for a standard or registered attribute id; false for IOT_ATTR_NONE and unknown ids. */
bool IOT_AttrIsKnown(uint16_t attrId);

/**
 * @brief 16-bit fields one Record entry keeps for this attribute: 1 or 2, never 0.
 *
 * Derived from the attribute's size, with one exception: IOT_ATTR_OPEN_CLOSE_CTL carries command
 * + position, and a record keeps only the command. Wider attributes keep their first two fields.
 */
uint8_t IOT_AttrRecordValueCount(uint16_t attrId);

/** Write the attribute's default value (IOT_AttrGetSize() bytes) into @p buffer. */
iot_err_t IOT_AttrFillDefault(uint16_t attrId, uint8_t *buffer);

/** True when this value of this attribute is worth a push notification (smoke, removed from its
 *  mount, low battery). @p attrValue is big-endian, @p attrSize bytes. */
bool IOT_AttrIsNotifyWorthy(uint16_t attrId, uint8_t *attrValue, uint16_t attrSize);

/**
 * @brief Rewrite a sync-on-off carrier to plain IOT_ATTR_ONOFF.
 *
 * IOT_ATTR_SYNC_ONOFF_NORMAL and IOT_ATTR_SYNC_ONOFF_STAIR_SWITCH are not stored attributes: they
 * mean "apply this ON/OFF value without evaluating this device's own triggers for it". The caller
 * suppresses that evaluation; without it two linked switches would mirror each other forever.
 * IOT_ATTR_SYNC_ONOFF_REVERSING is not a carrier.
 *
 * @return true if @p attrId was a carrier and is now IOT_ATTR_ONOFF.
 */
bool IOT_AttrResolveSyncOnOffCarrier(uint16_t *attrId);

/** True for a control the user sets, which a boot restore replays through apply: the standard
 *  controls, and a registered attribute declared with restore = true. Never a lock, a motor
 *  command, an event, a reading or a carrier. */
bool IOT_AttrIsRestorable(uint16_t attrId);

/** Close registration: IOT_ContractStart() calls this before core starts, so the registry never
 *  changes while another task reads it. */
void IOT_ContractAttrsFreeze(void);

/** The caller has already warned about this unknown id (with more context): the size lookup
 *  does not warn about it again. */
void IOT_ContractAttrMarkWarned(uint16_t attrId);

#ifdef __cplusplus
}
#endif

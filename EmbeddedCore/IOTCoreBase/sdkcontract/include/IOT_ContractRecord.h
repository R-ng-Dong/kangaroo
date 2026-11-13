#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "IOT_ErrorManager.h"

/* One 16-bit field -- the width every loggable attribute needs unless the rule
 * asks for more. See IOT_ContractRecordLogValueWidth(). */
#define IOT_CONTRACT_RECORD_DEFAULT_LOG_VALUE_BYTES 2

/* Widest log value a rule may ask for: four 16-bit fields. Bounds Record's fixed-size
 * buffers. The widest loggable attribute needs two fields -- a paired
 * temperature+humidity reading. */
#define IOT_CONTRACT_RECORD_MAX_LOG_VALUE_BYTES 8

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Record Events and Rules
// ============================================================================

/**
 * @brief State-change context evaluated by the Record subsystem.
 *
 * Record builds one of these per element of every change the contract commits. Your rule filter
 * and your custom store (IOT_ContractRecordUseCustomStore()) receive it; you do not build it
 * yourself.
 */
typedef struct
{
    uint16_t eid;          ///< Network-wide entity ID. Used by exact element overrides.
    uint16_t deviceType;   ///< Product/device class (IOT_DEVTYPE_*); 0 if unknown.
    uint16_t elemId;       ///< Channel/logical element inside the device.
    uint16_t elemType;     ///< Element's type (an IOT_DEVTYPE_* value), from the Device store; 0 if unknown.
    uint16_t attrId;       ///< Property ID (IOT_ATTR_*).
    const uint8_t *value;  ///< Attribute value bytes to record.
    uint16_t valueLen;     ///< Number of bytes in value.
} IOT_ContractRecordEvent_t;

// ============================================================================
// Record policy constants
// ============================================================================
//
// Every value a rule can hold has a name. No bare 0 and no bare 0xFFFF at a call site:
// in a two-knob policy the numbers are indistinguishable from each other by eye, and a
// heartbeat read as a rate limit leaves no heartbeat at all.

/** Either timing field: take the attribute's default.
 *
 *  This IS 0, deliberately. Rules are written with designated initialisers, so a field
 *  nobody mentions is already 0 -- which means the obvious rule inherits the attribute's
 *  defaults without the app-writer having to know they exist. */
#define IOT_CONTRACT_RECORD_USE_DEFAULT  0u

/** minIntervalSec: never hold a change. Use it for events, where holding one risks
 *  dropping a transition -- a door that opened and closed inside a window would read as
 *  never opened. */
#define IOT_CONTRACT_RECORD_EVERY_CHANGE 0xFFFFu

/** heartbeatSec: never re-record an unchanged value. */
#define IOT_CONTRACT_RECORD_NO_HEARTBEAT 0xFFFFu

/**
 * @brief Optional value-level filter for a Record rule.
 *
 * Return true to record the event, false to skip it. Use this for noisy events
 * such as smoke or motion where only the "detected" value should be retained.
 */
typedef bool (*IOT_ContractRecordFilterFn)(const IOT_ContractRecordEvent_t *event, void *userCtx);

/**
 * @brief App-writer rule declaring which element attribute can be recorded.
 *
 * Matching priority is:
 *   1. Exact element override from IOT_ContractRecordSetElementEnabled()
 *   2. deviceType + elemType + attrId rule
 *   3. elemType + attrId rule where deviceType is 0
 *   4. No record
 *
 * Set deviceType to 0 to make the rule apply to any device type with the given
 * elemType and attrId. Set valueSize to 0 to accept any incoming value length.
 *
 * elemType is the element's TYPE, not its id (elemId). Element types use the same
 * IOT_DEVTYPE_* values as device types, and each element's type comes from the cloud's
 * model at provisioning. A rule therefore covers every element of that type: one rule
 * {IOT_DEVTYPE_SWITCH, IOT_ATTR_ONOFF} records ON/OFF on all three gangs of a 3-gang
 * switch, each as its own series. To record, or skip, one particular element, use
 * IOT_ContractRecordSetElementEnabled().
 */
typedef struct
{
    uint16_t deviceType; /* 0 means any device type */
    uint16_t elemType;   ///< Element type to match (an IOT_DEVTYPE_* value), NOT an elemId.
    uint16_t attrId;     ///< Required attribute ID (IOT_ATTR_*).
    uint16_t valueSize;  /* 0 accepts the incoming value length */
    /**
     * @brief Bytes of the attribute value a custom store keeps per entry
     *        (IOT_ContractRecordUseCustomStore(); read with IOT_ContractRecordLogValueWidth()).
     *
     * Record itself ignores this field: how many values an entry holds follows the
     * attribute id (the attribute table: one value, or two for a two-field attribute), because
     * the app sizes entries from the id.
     *
     * For an override, 0 means the default of 2 -- the leading 16-bit field of the
     * value, which is what every loggable attribute needs except a paired reading such
     * as temperature+humidity, where 4 keeps both fields. Trailing fields are dropped,
     * never the leading one. A value shorter than this width is stored whole rather than
     * over-read.
     *
     * Must be even (whole 16-bit fields) and at most IOT_CONTRACT_RECORD_MAX_LOG_VALUE_BYTES;
     * IOT_ContractRecordRegisterRule() rejects any other value, whether or not the override is
     * installed.
     */
    uint8_t logValueBytes;

    /**
     * @brief Shortest gap between two recorded CHANGES, in seconds.
     *
     * Within this long of a recorded change, further changes are HELD -- not dropped --
     * and the settled value is written when the window closes, stamped with when it
     * actually arrived. This knob is local to the device, and it exists because a
     * thermometer with +/-0.1 C jitter produces a changed value on every sample and
     * would fill its whole history in under half an hour.
     *
     * Rate-limit MEASUREMENTS, never events. Events are inherently sparse, and the one
     * thing history must not do is lose a transition.
     *
     * IOT_CONTRACT_RECORD_USE_DEFAULT (0, also what an omitted field holds) takes the
     * attribute's default -- it does NOT mean "no limit";
     * IOT_CONTRACT_RECORD_EVERY_CHANGE never holds a change.
     */
    uint16_t minIntervalSec;

    /**
     * @brief How long before an UNCHANGED value is recorded again, in seconds.
     *
     * A different knob from minIntervalSec. It is evaluated only
     * when a report arrives -- it costs no timer, and a sensor that goes silent simply
     * stops producing entries, which is honest. It fires only for devices that re-report
     * an unchanged value, typically a hub's sub-devices.
     *
     * IOT_CONTRACT_RECORD_USE_DEFAULT (0) takes the attribute's default;
     * IOT_CONTRACT_RECORD_NO_HEARTBEAT never repeats a value.
     *
     * Make it longer than minIntervalSec. Closing a window does not check the heartbeat, so one
     * no longer than the window repeats at the first report after it, not on time. Such a rule
     * is accepted and warned about once, at registration.
     *
     * Values below the configured minimum heartbeat are clamped at registration and
     * warned about. The floor is arithmetic, not taste: history holds a few hundred
     * entries per series, so one day of it needs a heartbeat of minutes, not seconds. A
     * heartbeat of seconds -- 1 s for a door -- would produce 86,400 entries a day and burn
     * the whole series within minutes for a sensor that never changed.
     */
    uint16_t heartbeatSec;

    IOT_ContractRecordFilterFn filter; ///< Optional value-level filter.
    void *userCtx;             ///< Opaque app context passed to filter.
} IOT_ContractRecordRule_t;

// ============================================================================
// Record API
// ============================================================================

/**
 * @brief Register or replace a semantic Record rule.
 *
 * If a rule with the same deviceType, elemType, and attrId already exists, it is
 * replaced. Rules are stored in RAM and should be registered at boot before or
 * during app initialization.
 *
 * Registration succeeding does NOT mean values will be recorded. With a custom store installed
 * (IOT_ContractRecordUseCustomStore()), Record does not store the value itself: it hands it to
 * your store. With Record turned off in the build (IOT_RECORD_ENABLED), valid rules are accepted
 * and ignored.
 *
 * Register before or after IOT_ContractStart(); the start keeps registered rules.
 *
 * Blocks: no. Safe from a timer callback: yes. Task: any, but the rule table is not locked:
 * register at boot, before core starts delivering events.
 *
 * @param rule Rule to register.
 * @return IOT_OK on success; IOT_ERR_INVALID_ARG when rule is NULL, elemType or attrId is 0,
 *         or logValueBytes is odd or larger than IOT_CONTRACT_RECORD_MAX_LOG_VALUE_BYTES;
 *         IOT_ERR_NO_MEM when the fixed rule table is full. The same arguments are rejected
 *         with Record turned off in the build, so a rule set that registers cleanly with
 *         Record off also registers with it on.
 */
iot_err_t IOT_ContractRecordRegisterRule(const IOT_ContractRecordRule_t *rule);

/**
 * @brief Signature of a replacement append routine.
 *
 * Install it with IOT_ContractRecordUseCustomStore(). Record calls it for each change
 * it accepts, after the event has been matched to a rule (or to an exact element override, in
 * which case @p rule may be NULL), on the task that reported the change. The event and rule
 * are only valid for the duration of the call.
 *
 * With Record turned off in the build (IOT_RECORD_ENABLED), an installed override is never
 * called: nothing reaches it.
 */
typedef iot_err_t (*IOT_ContractRecordAppendOverrideFn)(const IOT_ContractRecordEvent_t *event, const IOT_ContractRecordRule_t *rule);

/**
 * @brief Replace Record's history storage with your own. Optional; call before IOT_ContractStart().
 *
 * With @p fn installed, Record still matches every change against the rules, but hands each one
 * it accepts to @p fn instead of storing it, and keeps no history of its own: the history the
 * contract would have served is empty. The older day-keyed history request is then not answered
 * by the contract but passed to your onCoreEvent (IOT_EVENT_TYPE_STATE_SENSOR_LOG), so the store
 * that keeps the history answers it. Not called, or called with NULL: Record keeps its own
 * history and answers the day-keyed request with an empty log.
 *
 * A later call replaces the earlier one and takes effect at the next IOT_ContractStart().
 * Ignored when Record is turned off in the build (IOT_RECORD_ENABLED): nothing reaches @p fn.
 *
 * Blocks: no. Safe from a timer callback: yes. Task: the boot task (app_main), before
 * IOT_ContractStart().
 *
 * @param fn Your store's append routine, or NULL for Record's own history.
 * @return IOT_OK.
 */
iot_err_t IOT_ContractRecordUseCustomStore(IOT_ContractRecordAppendOverrideFn fn);

/**
 * @brief Clear all registered Record rules and exact element overrides.
 *
 * Mainly useful for tests or app reconfiguration flows. Normal app startup can
 * call this before re-registering its full rule set. Open history blocks are flushed and
 * every series starts a fresh block afterwards.
 *
 * Blocks: yes -- waits up to about a second for Record's lock and queues storage writes. Safe
 * from a timer callback: no. Task: any, while no state change is being reported (the rule
 * table itself is not locked).
 *
 * @return IOT_OK on success.
 */
iot_err_t IOT_ContractRecordClearRules(void);

/**
 * @brief Enable or disable recording for one exact physical element attribute.
 *
 * Exact overrides have the highest priority. A disabled override suppresses a
 * matching semantic rule. An enabled override records even if no semantic rule
 * matches, which is useful for runtime/user-configured recording.
 *
 * Element numbering: for the device's OWN eid, pass your own element id, as declared in
 * IOT_ContractConfig_t.elementIds: the contract translates it the same way it translates apply
 * and IOT_ContractLocalChange(). With no elements declared, ids are used as given. For a
 * sub-device eid, always pass the id its hub assigned -- it is not translated. Before
 * provisioning the id is stored unchanged.
 *
 * Blocks: no, apart from reading the device's own EID from core, and with declared elements the
 * device's element list from the Device store.
 * Safe from a timer callback: no. Task: any, but the override table is not locked: change
 * overrides from the task that reports your state changes, or at boot.
 *
 * @param eid Device EID.
 * @param elemId Element ID: see "Element numbering" above.
 * @param attrId Attribute ID.
 * @param enabled true to force record, false to suppress record.
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG when eid, elemId or attrId is 0 (also with
 *         Record turned off in the build), IOT_ERR_NO_MEM when the fixed override table is full.
 */
iot_err_t IOT_ContractRecordSetElementEnabled(uint16_t eid, uint16_t elemId, uint16_t attrId, bool enabled);

/**
 * @brief Bytes of an attribute value that this element's log entries carry.
 *
 * For an append override's read path, which needs the same width its append path used.
 * Returns the matching rule's logValueBytes, or 2 -- one 16-bit field -- when no rule matches.
 *
 * Blocks: no. Safe from a timer callback: yes. Task: any.
 *
 * @param deviceType Product class, 0 if unknown.
 * @param elemType   Semantic element class, 0 if unknown.
 * @param attrId     Attribute ID.
 * @return Log value width in bytes; never 0.
 */
uint8_t IOT_ContractRecordLogValueWidth(uint16_t deviceType, uint16_t elemType, uint16_t attrId);

#ifdef __cplusplus
}
#endif

#pragma once

/*
 * The contract's element map: the cloud's element ids <-> the device's own element ids.
 *
 * The cloud's element ids are opaque labels assigned by its model record (158, 159, ... and
 * often non-contiguous); a product numbers its own elements. The two are joined by POSITION:
 * the n-th element in the cloud's list (the Device store) is the n-th element the product
 * declared in IOT_ContractConfig_t.elementIds. Sub-devices keep the ids their hub assigned and
 * are never translated.
 *
 * Internal to the contract. Not thread-safe against IOT_ContractElementsDeclare(): declare at
 * start, before core delivers events.
 */

#include <stdbool.h>
#include <stdint.h>
#include "IOT_Contract.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Keep a copy of the product's element ids (clamped to IOT_CONTRACT_MAX_ELEMENTS).
 *
 * NULL / 0 clears the map, so ids pass through untranslated. With at least one element, Record's
 * element-id resolver is replaced by the contract's own map (device -> cloud for the root eid).
 */
void IOT_ContractElementsDeclare(const uint16_t *ids, uint8_t count);

/** @brief The declared ids (the contract's copy) and their count. */
const uint16_t *IOT_ContractElementsDeclared(uint8_t *outCount);

/**
 * @brief Positional join: the id at @p id's position in @p fromIds, taken from @p toIds.
 *
 * @param outId Always written: the joined id, or @p id unchanged when there is none.
 * @return true when joined; false for an id not in @p fromIds, a position past @p toCount, or
 *         an empty / NULL list.
 */
bool IOT_ContractElemJoin(uint16_t id, const uint16_t *fromIds, uint8_t fromCount, const uint16_t *toIds,
                          uint8_t toCount, uint16_t *outId);

/**
 * @brief Cloud element id -> the device's own element id, for a request the contract applies.
 *
 * Passes the id through (returns true) for a sub-device, with no elements declared, or when the
 * root device's element list cannot be read (unprovisioned). Returns false -- SKIP the element,
 * after one warning naming eid and element -- when the cloud's element has no declared device
 * element at its position (the cloud's model has more elements than the product declared) or
 * is not in the cloud's list at all.
 *
 * @param outDeviceElemId Always written: the device's id, or @p cloudElemId unchanged.
 */
bool IOT_ContractElemCloudToDevice(uint16_t eid, uint16_t cloudElemId, uint16_t *outDeviceElemId);

/**
 * @brief The device's own element id -> the cloud's element id, for a change the product reports.
 *
 * Same pass-through cases as IOT_ContractElemCloudToDevice(). Returns false (with a warning)
 * when the device element has no cloud element at its position; @p outCloudElemId is then
 * @p deviceElemId unchanged.
 */
bool IOT_ContractElemDeviceToCloud(uint16_t eid, uint16_t deviceElemId, uint16_t *outCloudElemId);

/**
 * @brief One device's cloud element list, read once so a command can translate each of its
 *        elements without reading the Device store again.
 */
typedef struct
{
    bool translate;     /**< false: ids pass through (sub-device, nothing declared, no cloud list). */
    uint8_t cloudCount; /**< Entries in cloudIds. */
    uint16_t cloudIds[IOT_CONTRACT_MAX_ELEMENTS];
} IOT_ContractElemMap_t;

/** @brief Read @p eid's cloud element list into @p map (one Device store read, or none). */
void IOT_ContractElemMapLoad(uint16_t eid, IOT_ContractElemMap_t *map);

/**
 * @brief IOT_ContractElemCloudToDevice() against a loaded map: same answers and the same warning,
 *        without a store read.
 */
bool IOT_ContractElemMapToDevice(const IOT_ContractElemMap_t *map, uint16_t eid, uint16_t cloudElemId,
                                 uint16_t *outDeviceElemId);

/** @brief The request's sender (IOT_SENDER_*) as the source handed to apply. */
IOT_ContractSource_t IOT_ContractSourceFromSender(uint8_t senderFrom);

#ifdef __cplusplus
}
#endif

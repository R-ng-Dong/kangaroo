#pragma once
#include "IOT_ErrorManager.h"
#include <inttypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Record V2 store — persisted layout (partners must not change it)
// ============================================================================
//
// A ring of N blocks per {eid, elemId, attrId}. The key is a ring SLOT, not a time:
//
//   ns    "R%04x"                 eid
//   key   "%04x%04x%02x"          elemId, attrId, slot      -- 10 chars, inside the 15-char key limit
//   blob  [checkpoint 8B][sequence 2B][fields ... host-order uint16]
//
// Keying on the checkpoint instead would need a same-second collision guard, a
// wraparound caveat and a sort that breaks after a backward clock step. A slot index
// removes all three.
//
// Element ids need four hex digits: they are opaque 16-bit values, not small indexes.
//
// Stored blocks are host order and private; the one exception is the push bookmark, a
// two-byte big-endian sequence (see IOT_DaoRecordPutPushed()). The message format is core's, and a
// change to it does not invalidate a single stored block.

/** checkpoint(8) + sequence(2). */
#define IOT_DAO_RECORD_BLOB_HEADER 10

/** Namespace prefix. Distinct from every other store's prefix, so nothing collides. */
#define IOT_DAO_RECORD_NS_PREFIX "R"

/** One stored block. `fields` is heap; free it with SAFE_FREE(). */
typedef struct
{
    uint64_t checkpoint;
    uint16_t sequence;
    uint16_t *fields;
    uint16_t fieldCount;
} IOT_DaoRecordBlock_t;

/** What a boot scan needs to rebuild a ring, without holding every blob resident. */
typedef struct
{
    uint16_t elemId;
    uint16_t attrId;
    uint8_t slot;
    uint16_t sequence;
    uint64_t checkpoint;
    uint16_t fieldCount;
} IOT_DaoRecordSlotInfo_t;

/**
 * @brief Write one block into a ring slot, replacing whatever was there.
 *
 * @param async true queues the write (IOT_DaoStorageSetAsync copies first, so this is
 *              safe from a timer callback); false blocks.
 * @return IOT_OK, IOT_ERR_INVALID_ARG, or the storage layer's error. On an async queue
 *         failure the caller must keep its dirty flag and retry -- DAO_QUEUE_SIZE is 10
 *         and several instances flushing on one tick can fill it.
 */
iot_err_t IOT_DaoRecordPut(uint16_t eid, uint16_t elemId, uint16_t attrId, uint8_t slot,
                           const IOT_DaoRecordBlock_t *block, bool async);

/**
 * @brief Read one block back.
 *
 * entryCount is not stored: it is (len - 10) / (2 * (valueCount + 1)). A length that is
 * not a whole multiple is a DETECTABLE CORRUPTION -- realistically a build whose
 * valueCount for this attrId differs from the writer's. The slot is deleted and
 * IOT_ERR_INVALID_SIZE returned, because truncating to the last whole entry would keep
 * misreading the same block on every boot.
 *
 * @param valueCount 1 or 2, from IOT_AttrRecordValueCount().
 * @param outBlock   Filled on success. Caller frees outBlock->fields with SAFE_FREE().
 * @return IOT_OK, IOT_ERR_NOT_FOUND, IOT_ERR_INVALID_SIZE, IOT_ERR_NO_MEM.
 */
iot_err_t IOT_DaoRecordGet(uint16_t eid, uint16_t elemId, uint16_t attrId, uint8_t slot,
                           uint8_t valueCount, IOT_DaoRecordBlock_t *outBlock);

/**
 * @brief Remember the newest reel pushed for one series.
 *
 * Key "p%04x%04x" (elemId, attrId) in the device's "R%04x" namespace; value is the reel's
 * sequence stored as two big-endian bytes (unlike the host-order blocks). Living in the device namespace, it is dropped by
 * IOT_DaoRecordRemoveDevice(), and IOT_DaoRecordScanDevice() skips it without a WARN.
 *
 * @param async true to queue the write (safe from a timer callback); a queue failure means
 *              the bookmark was not stored.
 */
iot_err_t IOT_DaoRecordPutPushed(uint16_t eid, uint16_t elemId, uint16_t attrId, uint16_t sequence, bool async);

/**
 * @brief Read the bookmark written by IOT_DaoRecordPutPushed().
 *
 * @return IOT_OK, IOT_ERR_NOT_FOUND when never set, IOT_ERR_INVALID_SIZE (and the key is
 *         deleted) when the stored value is not 2 bytes, IOT_ERR_INVALID_ARG on NULL.
 */
iot_err_t IOT_DaoRecordGetPushed(uint16_t eid, uint16_t elemId, uint16_t attrId, uint16_t *outSequence);

/** @brief Free one ring slot. IOT_ERR_NOT_FOUND if it was already empty. */
iot_err_t IOT_DaoRecordRemoveSlot(uint16_t eid, uint16_t elemId, uint16_t attrId, uint8_t slot);

/** @brief Drop every record this device has. Called when a device is removed. */
iot_err_t IOT_DaoRecordRemoveDevice(uint16_t eid);

/**
 * @brief Delete this device's Record V1 namespace.
 *
 * V1 stored one "L%04x" namespace per device and nothing on a V2 build deletes it, so
 * every V1->V2 upgrade would strand one per child forever -- invisible to every API, and
 * still there after a factory reset. Call this for each known device at boot.
 *
 * Does NOT reach namespaces for EIDs the device DAO no longer knows, such as a previous
 * provisioning's root EID: that needs namespace enumeration, which the abstract storage
 * interface does not expose.
 *
 * @return IOT_OK whether or not anything was there -- absence is the normal case.
 */
iot_err_t IOT_DaoRecordReapV1(uint16_t eid);

/**
 * @brief List every stored slot for one device, header fields only.
 *
 * One IOT_DaoStorageGetNamespaceData() call: the namespace is per-EID, so this returns
 * every block of every attribute in one read rather than one read per record. Blobs are
 * freed as their headers are taken, so the peak is the storage layer's, not ours.
 *
 * The caller sorts by `sequence`, NEVER by checkpoint. A backward clock step legitimately
 * produces a block whose checkpoint precedes its predecessor's; sorting by checkpoint
 * would call the newest block oldest, set its prev to 0, and reopen a block sitting in
 * the future of the corrected clock -- a negative delta on every append, permanently.
 *
 * @param outCount Slots written. Capped at outCap; the excess is dropped with a WARN.
 * @return IOT_OK, or IOT_ERR_NOT_FOUND when the device has no records.
 */
iot_err_t IOT_DaoRecordScanDevice(uint16_t eid, IOT_DaoRecordSlotInfo_t *out, uint16_t outCap,
                                  uint16_t *outCount);

#ifdef __cplusplus
}
#endif

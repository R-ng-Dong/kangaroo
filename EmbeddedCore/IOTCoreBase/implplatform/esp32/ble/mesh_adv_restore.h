/**
 * @file mesh_adv_restore.h
 * @brief What to put back on air after ESP-BLE-MESH borrows the BLE 4.x advertiser.
 *
 * On BLE 4.x there is one legacy advertising set and mesh takes it for every packet it
 * sends. With CONFIG_IOT_MESH_ADV_RESTORE the NimBLE driver wraps mesh's
 * bt_le_adv_stop() and re-issues our advert from this record. Split out of
 * IOT_BleMgmtNimble.c as pure logic so it can be tested on the host without NimBLE.
 * The caller provides the locking.
 */

#ifndef MESH_ADV_RESTORE_H
#define MESH_ADV_RESTORE_H

#include <stdbool.h>
#include <stdint.h>

/** Legacy advertising and scan-response payloads are at most 31 bytes. */
#define MESH_ADV_RESTORE_MAX_LEN 31u

typedef struct
{
    bool wanted;         /**< an advert of ours should be on air */
    uint8_t instance;    /**< IOT_BLE_ADV_INST_CONN or IOT_BLE_ADV_INST_BEACON */
    bool connectable;
    uint8_t ownAddrType;
    uint8_t adv[MESH_ADV_RESTORE_MAX_LEN];
    uint8_t advLen;
    uint8_t rsp[MESH_ADV_RESTORE_MAX_LEN];
    uint8_t rspLen;
} MeshAdvRestore_t;

typedef enum
{
    MESH_ADV_RESTORE_OK = 0,
    MESH_ADV_RESTORE_SLOT_TAKEN, /**< a DIFFERENT instance is recorded: one slot, one advert */
    MESH_ADV_RESTORE_BAD_ARG,    /**< NULL payload with a length, or a length over 31 */
} MeshAdvRestoreResult_t;

/**
 * @brief Record the advert being started.
 *
 * The same instance replaces its own record. A different instance is refused, so a
 * beacon can never silently replace the change-WiFi recovery advert. On any non-OK
 * result the record is left untouched.
 */
MeshAdvRestoreResult_t meshAdvRestoreRecord(MeshAdvRestore_t *s, uint8_t instance, bool connectable,
                                            uint8_t ownAddrType, const uint8_t *adv, uint8_t advLen,
                                            const uint8_t *rsp, uint8_t rspLen);

/**
 * @brief Forget the advert being stopped.
 * @return true if `instance` was the one on record, so the caller must stop the radio;
 *         false for a stop of an instance that is not up, which must NOT stop the one
 *         that is.
 */
bool meshAdvRestoreForget(MeshAdvRestore_t *s, uint8_t instance);

/** @brief A connection consumed the advert; the application re-advertises if it wants to. */
void meshAdvRestoreOnConnect(MeshAdvRestore_t *s);

/**
 * @brief After mesh's stop: should our advert be put back?
 *
 * Only when one is wanted, nothing is advertising (a no-op stop may have left ours up,
 * and a second start would fail), and no connection is being established. A connection
 * that already completed cleared the record through meshAdvRestoreOnConnect().
 */
bool meshAdvRestoreShouldApply(const MeshAdvRestore_t *snap, bool advActive, bool connectInProgress);

#endif /* MESH_ADV_RESTORE_H */

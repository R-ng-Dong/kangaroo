/**
 * @file ble_ext_adv_policy.h
 * @brief Advertising-set ids and legacy-equivalent shapes for NimBLE extended advertising.
 *
 * Split out from IOT_BleMgmtNimble.c as pure data + logic so it can be tested on the
 * host without NimBLE.
 */

#ifndef BLE_EXT_ADV_POLICY_H
#define BLE_EXT_ADV_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Advertising instance ids owned by this driver.
 *
 * BLE Mesh takes instance 0 (`CONFIG_BLE_MESH_ADV_INST_ID`) for its own packets and,
 * when `CONFIG_BLE_MESH_SUPPORT_MULTI_ADV` is on, instance 1
 * (`CONFIG_BLE_MESH_PROXY_ADV_INST_ID`) for proxy/provisioning. Reusing either id
 * hands mesh the power to reconfigure our set behind our back — which is exactly the
 * single-legacy-advertiser collision that extended advertising exists to end.
 *
 * `IOT_BleMgmtNimble.c` static-asserts these against the mesh Kconfig values.
 */
#define IOT_BLE_ADV_INST_CONN 2u   /**< connectable: provisioning / change-WiFi recovery */
#define IOT_BLE_ADV_INST_BEACON 3u /**< non-connectable beacon */

/** @brief The three ble_gap_ext_adv_params bits that decide the on-air PDU type. */
typedef struct
{
    uint8_t legacyPdu;   /**< 1 = emit a BLE 4.x PDU, so existing scanners are unaffected */
    uint8_t connectable; /**< 1 = accept connection requests */
    uint8_t scannable;   /**< 1 = answer SCAN_REQ with the scan-response data */
} IOT_BleExtAdvShape_t;

/**
 * @brief Extended-advertising shape equivalent to the legacy call this driver used to make.
 *
 * Legacy `BLE_GAP_CONN_MODE_UND` is ADV_IND, which is inherently scannable;
 * `BLE_GAP_CONN_MODE_NON` is ADV_NONCONN_IND, which is not.
 *
 * @param connectable true for the provisioning advert, false for the beacon.
 * @param out         receives the shape; every field is written.
 */
void IOT_BleExtAdvShapeFromConnectable(bool connectable, IOT_BleExtAdvShape_t *out);

/**
 * @brief Is this one of the legacy-PDU combinations NimBLE will configure?
 *
 * BT 5.0 Vol 6 keeps only IND / SCAN / NONCONN among undirected legacy PDUs, so
 * "connectable but not scannable" has no encoding and `ble_gap_ext_adv_configure()`
 * rejects it with `BLE_HS_EINVAL`.
 */
bool IOT_BleExtAdvShapeIsValid(const IOT_BleExtAdvShape_t *shape);

/**
 * @brief Highest advertising instance id this module tracks.
 *
 * NimBLE's own limit is CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES, which is itself a
 * highest-id and not a count; 3 is its maximum.
 */
#define IOT_BLE_ADV_INST_MAX 3u

/**
 * @brief Remember that an advertising instance has been configured.
 *
 * The legacy and extended APIs disagree about stopping an advert that was never
 * started: `ble_gap_adv_stop()` said `BLE_HS_EALREADY` ("was not advertising"),
 * which callers treat as success, while `ble_gap_ext_adv_stop()` says
 * `BLE_HS_EINVAL` for an instance that was never configured, which they treat as
 * a failure. The driver tracks what it configured so it can keep the old contract.
 *
 * Out-of-range ids are ignored rather than shifting past the width of the mask.
 */
void IOT_BleExtAdvMarkConfigured(uint8_t instance);

/** @brief Has this driver configured @p instance since the last IOT_BleExtAdvForgetAll()? */
bool IOT_BleExtAdvIsConfigured(uint8_t instance);

/**
 * @brief Forget every instance. Call from BLE deinit.
 *
 * A stack brought back up is a new session with nothing configured on it. A latch
 * that outlives the stack it describes makes the driver skip the configure step and
 * advertise nothing.
 */
void IOT_BleExtAdvForgetAll(void);

#endif /* BLE_EXT_ADV_POLICY_H */

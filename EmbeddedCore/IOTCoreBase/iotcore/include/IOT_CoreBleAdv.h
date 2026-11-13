#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "IOT_ErrorManager.h"

/*
 * Your own fields in core's BLE adverts.
 *
 * Core advertises so the app can find the device. Register a customizer and core asks it,
 * each time an advert starts, for the extra AD fields (length, type, data) you want beside
 * its own: a 32- or 128-bit service UUID list (0x04-0x07), service data (0x16, 0x20, 0x21),
 * a different local name. Manufacturer data is not among them: core's advert already carries it
 * (see the refusals below).
 *
 * The callback runs on the task that starts the advert: the task that called IOT_CoreInit(),
 * core's task, the BLE stack's own task after a phone disconnects, or a task of yours that
 * calls IOT_CoreStartProvision() or IOT_CoreStartChangeWifiBle(). Keep it short, never
 * block, use little stack, and read shared values safely (for example a single volatile
 * byte). Call nothing of core's except IOT_CoreBleAdvAddField() and IOT_CoreBleAdvOmitName().
 * Field bytes are copied, so your buffers need not outlive the call.
 *
 * Some apps identify the product by core's default name. If you replace or omit it, those
 * apps no longer list the device. Keep core's name if your users set up with such an app.
 *
 * Space is small (a legacy advert is 31 bytes) and core's own fields come first. Read the
 * free space from the context rather than assuming it; it differs by purpose and by model
 * ID. A field that does not fit is refused whole, and fields you already added are kept.
 *
 * Core's own field types cannot be added: Flags, every 16-bit service UUID list (0x02, 0x03;
 * your own 16-bit UUIDs too) and manufacturer data (0xFF, whatever the company ID) are refused.
 * A phone merges the advert and the scan response and keeps one value per type (Apple reports one
 * manufacturer-data value per device), so a second field of a type you already added, in either
 * packet, is refused too. Service data (0x16, 0x20, 0x21) is the exception: it is reported per
 * UUID, so fields with different UUIDs are fine.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Which advert is starting. */
typedef enum
{
    IOT_CORE_BLE_ADV_PROVISION,  /**< The device is waiting to be set up. */
    IOT_CORE_BLE_ADV_CHANGE_WIFI /**< A provisioned device is advertising to accept new WiFi settings. */
} IOT_CoreBleAdvPurpose_t;

/** Which of the two payloads of an advert a field goes in. */
typedef enum
{
    IOT_CORE_BLE_ADV_PACKET, /**< The advertising packet. */
    IOT_CORE_BLE_SCAN_RSP    /**< The scan response. */
} IOT_CoreBleAdvWhere_t;

/**
 * What the callback is told about this advert. A snapshot taken when the callback is
 * called: set or omit the name first, because these numbers do not change afterwards.
 */
typedef struct
{
    IOT_CoreBleAdvPurpose_t purpose; /**< Which advert is starting. */
    uint8_t advFree;                 /**< Bytes still free in the advertising packet. */
    uint8_t rspFree;                 /**< Bytes free in the scan response, beside core's name. */
    bool nameRequired;               /**< true when core's name cannot be dropped or replaced. */
} IOT_CoreBleAdvContext_t;

/** The fields being collected for one advert. Opaque; valid only inside the callback. */
typedef struct IOT_CoreBleAdvFields IOT_CoreBleAdvFields_t;

/**
 * @brief Add one AD field to the advert being built. Call only from the customizer.
 *
 * The field is encoded as length, type, data and the bytes are copied.
 *
 * A complete (0x09) or shortened (0x08) local name replaces core's name and frees its
 * space at once. A name may be set once per advert. If your device uses a non-hex model
 * ID (product ID), you cannot drop or replace the name: it is the only place the model ID
 * is advertised, and the device could not be found.
 *
 * Some apps identify the product by core's default name. If you replace it, those apps no
 * longer list the device. Keep core's name if your users set up with such an app.
 *
 * @param f      The fields handed to the callback.
 * @param where  Packet or scan response.
 * @param adType AD type of the field.
 * @param data   The field's data; not NULL.
 * @param len    Length of data, at least 1.
 * @return IOT_OK; IOT_ERR_INVALID_ARG for a NULL pointer, zero length or unknown
 *         `where`; IOT_ERR_INVALID_STATE outside the callback; IOT_ERR_NOT_ALLOWED for
 *         a type core owns (Flags, 16-bit UUID lists, manufacturer data) or a name that is
 *         required; IOT_ERR_ALREADY_EXISTS for a second name, a second field of a type already
 *         added in either packet, or service data repeating a UUID; IOT_ERR_INVALID_SIZE when
 *         it does not fit. A refused field writes nothing.
 */
iot_err_t IOT_CoreBleAdvAddField(IOT_CoreBleAdvFields_t *f, IOT_CoreBleAdvWhere_t where, uint8_t adType,
                                 const uint8_t *data, uint8_t len);

/**
 * @brief Drop core's local name from the scan response, freeing its space.
 *
 * Call it before adding fields that need the room. Calling it twice is fine.
 *
 * Some apps identify the product by core's default name. If you omit it, those apps no
 * longer list the device. Keep core's name if your users set up with such an app.
 *
 * @return IOT_OK; IOT_ERR_INVALID_ARG for NULL; IOT_ERR_INVALID_STATE outside the callback
 *         or after you set your own name; IOT_ERR_NOT_ALLOWED when the name is required
 *         (see IOT_CoreBleAdvAddField()).
 */
iot_err_t IOT_CoreBleAdvOmitName(IOT_CoreBleAdvFields_t *f);

/**
 * @brief Callback that adds your fields to the advert that is starting.
 *
 * Runs on the task that starts the advert (see the top of this header): never assume core's
 * task. Keep it short, never block, use little stack, and read shared values safely.
 *
 * @param ctx    What this advert is and how much room it has.
 * @param fields Pass to IOT_CoreBleAdvAddField() and IOT_CoreBleAdvOmitName().
 * @param arg    The pointer given to IOT_CoreBleAdvSetCustomizer().
 */
typedef void (*IOT_CoreBleAdvCustomizeCb_t)(const IOT_CoreBleAdvContext_t *ctx, IOT_CoreBleAdvFields_t *fields,
                                            void *arg);

/**
 * @brief Register (or, with NULL, remove) the customizer. A second call replaces the first.
 *
 * Call it before IOT_CoreInit() so the first advert carries your fields. A later call may
 * race an advert start on another task, so for at most one advert the callback and its arg
 * can be mismatched. Prefer one customizer for the device's life: change what it adds
 * through your own state, then call IOT_CoreBleAdvRefresh().
 *
 * @return IOT_OK.
 */
iot_err_t IOT_CoreBleAdvSetCustomizer(IOT_CoreBleAdvCustomizeCb_t cb, void *arg);

/**
 * @brief Re-run the customizer now, for fields that changed while the device is advertising.
 *
 * Returns at once; the work runs on core's task and puts the new bytes on air. When the
 * device is not advertising this succeeds and does nothing: the next start runs the
 * customizer anyway. If restarting the change-WiFi advert fails, it stays down until
 * core's change-WiFi recovery starts it again.
 *
 * Calling it from inside the customizer is refused: every advert start it caused would
 * queue another refresh.
 *
 * @return IOT_OK; IOT_ERR_INVALID_STATE while the customizer is running; otherwise the
 *         error of scheduling the work.
 */
iot_err_t IOT_CoreBleAdvRefresh(void);

#ifdef __cplusplus
}
#endif

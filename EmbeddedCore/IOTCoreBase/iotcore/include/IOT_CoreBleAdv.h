#pragma once

#include <stdint.h>

#include "IOT_ErrorManager.h"

/*
 * Your own name, fields and vendor data in core's setup advert.
 *
 * Core advertises so the app can find a device that is not set up yet (the "setup advert").
 * These setters store what you want beside core's own fields; core puts it into every setup
 * advert it starts. Set them once at boot, before or after IOT_CoreInit(): nothing is kept across
 * a reboot. Call them from any task. A call while the setup advert is on air takes effect at
 * once, with no further call from you.
 *
 * Only the setup advert carries your settings. The advert a set-up device sends to accept new
 * WiFi settings is core's alone, with core's name.
 *
 * Vendor data: up to IOT_CORE_BLE_ADV_VENDOR_MAX bytes appended inside core's manufacturer-data
 * field, right after the 8-byte model ID: company 0xFFFF, then 0x72 0x67, then the model ID, then
 * your bytes. Your scanner finds them at byte 12 of the manufacturer data that follows the company
 * ID. A device whose model ID is not hex cannot carry vendor data.
 *
 * Space is small: a legacy advert is 31 bytes per packet. With a hex model ID, core leaves 10
 * bytes in the advertising packet, shared by vendor data and your packet fields, and 13 bytes in
 * the scan response beside core's name (31 if you drop it, 29 for your own name with nothing
 * else). A field costs 2 bytes plus its data. Every setter checks the room at the call: when the
 * result would not fit it is refused and nothing changes.
 *
 * Some apps identify the product by core's default name. If you replace or omit it, those apps no
 * longer list the device. Keep core's name if your users set up with such an app.
 *
 * Each refusal returns its reason and logs one warning; nothing is stored. IOT_OK means stored.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Largest vendor data, in bytes. */
#define IOT_CORE_BLE_ADV_VENDOR_MAX 10

/** Longest name, in bytes (a 31-byte scan response less the field's 2-byte header). */
#define IOT_CORE_BLE_ADV_NAME_MAX 29

/** Which of the two payloads of the setup advert a field goes in. */
typedef enum
{
    IOT_CORE_BLE_ADV_PACKET, /**< The advertising packet. */
    IOT_CORE_BLE_SCAN_RSP    /**< The scan response. */
} IOT_CoreBleAdvWhere_t;

/**
 * @brief Set the vendor data carried in core's manufacturer-data field, after the model ID.
 *
 * A second call replaces the first. The bytes are copied.
 *
 * @param data Your bytes; may be NULL only when len is 0.
 * @param len  1 to IOT_CORE_BLE_ADV_VENDOR_MAX; 0 removes the vendor data.
 * @return IOT_OK; IOT_ERR_INVALID_ARG for NULL data with a length; IOT_ERR_INVALID_SIZE when
 *         longer than IOT_CORE_BLE_ADV_VENDOR_MAX or when it does not fit beside your packet
 *         fields (the earlier vendor data stays); IOT_ERR_NOT_ALLOWED when the model ID is not hex.
 */
iot_err_t IOT_CoreBleAdvSetVendorData(const uint8_t *data, uint8_t len);

/**
 * @brief Replace core's name in the setup advert's scan response with your own.
 *
 * The name is sent as a Complete Local Name. Its bytes are copied as given (no terminator needed).
 *
 * @param name Your name; may be NULL only when len is 0.
 * @param len  1 to IOT_CORE_BLE_ADV_NAME_MAX; 0 puts core's name back.
 * @return IOT_OK; IOT_ERR_INVALID_ARG for NULL with a length or a name longer than
 *         IOT_CORE_BLE_ADV_NAME_MAX; IOT_ERR_INVALID_SIZE when it (or core's name, for len 0)
 *         does not fit beside your scan-response fields (the earlier name stays);
 *         IOT_ERR_NOT_ALLOWED when the model ID is not hex: the name then carries it.
 */
iot_err_t IOT_CoreBleAdvSetName(const char *name, uint8_t len);

/**
 * @brief Send the setup advert without a name, freeing the scan response for your fields.
 *
 * Calling it twice is fine. IOT_CoreBleAdvSetName() sets a name again.
 *
 * @return IOT_OK; IOT_ERR_NOT_ALLOWED when the model ID is not hex.
 */
iot_err_t IOT_CoreBleAdvOmitName(void);

/**
 * @brief Add one AD field (length, type, data) to the setup advert.
 *
 * Allowed: 32- and 128-bit service UUID lists (0x04-0x07), service data (0x16, 0x20, 0x21) and
 * any other type core does not send. A field of a type you already added replaces it, in either
 * packet; service data replaces only the one with the same UUID. Fields go on air in the order
 * you first added them. The bytes are copied.
 *
 * @param where  Packet or scan response.
 * @param adType AD type of the field.
 * @param data   The field's data; for service data it starts with the UUID (2, 4 or 16 bytes).
 * @param len    Length of data, at least 1.
 * @return IOT_OK; IOT_ERR_INVALID_ARG for NULL data, length 0, an unknown `where` or service data
 *         shorter than its UUID; IOT_ERR_NOT_ALLOWED for a type core owns (Flags 0x01, 16-bit UUID
 *         lists 0x02/0x03, a name 0x08/0x09 -- use IOT_CoreBleAdvSetName() -- and manufacturer
 *         data 0xFF -- use IOT_CoreBleAdvSetVendorData()); IOT_ERR_INVALID_SIZE when it does not
 *         fit (a field it would replace stays).
 */
iot_err_t IOT_CoreBleAdvAddField(IOT_CoreBleAdvWhere_t where, uint8_t adType, const uint8_t *data, uint8_t len);

/**
 * @brief Remove a field added with IOT_CoreBleAdvAddField().
 *
 * @param adType  The field's type.
 * @param uuid    For service data, the UUID of the field to remove; NULL otherwise.
 * @param uuidLen Length of uuid (2, 4 or 16), 0 when uuid is NULL.
 * @return IOT_OK; IOT_ERR_NOT_FOUND when no such field is set; IOT_ERR_NOT_ALLOWED for a type core
 *         owns.
 */
iot_err_t IOT_CoreBleAdvRemoveField(uint8_t adType, const uint8_t *uuid, uint8_t uuidLen);

/**
 * @brief Back to core's own setup advert: no vendor data, core's name, none of your fields.
 *
 * @return IOT_OK.
 */
iot_err_t IOT_CoreBleAdvClear(void);

#ifdef __cplusplus
}
#endif

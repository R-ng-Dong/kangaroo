/**
 * @file wifi_reconnect.c
 * @brief Backoff schedule for WiFi auto-reconnect.
 *
 * Split out as a pure function so the schedule can be tested on the host without
 * esp_wifi.
 *
 * Why this exists: WIFI_EVENT_STA_DISCONNECTED used to call esp_wifi_connect()
 * immediately and unconditionally. When the AP is genuinely gone (reason 201,
 * NO_AP_FOUND) that is a full scan roughly every 4.8 s, forever.
 *
 * On a single-radio part that starves BLE. Measured on an ESP32-C3:
 * the change-WiFi recovery advert still reached the air between scans, so a phone
 * could SEE the device — but a GATT connection never completed and the app sat on
 * "connecting to device" indefinitely (`wifi:Coexist: Wi-Fi connect fail, apply
 * reconnect coex policy`). The BLE recovery channel is the only way to fix a device's
 * WiFi, so degrading it while WiFi is down is exactly backwards.
 */

#include <stdint.h>

/* First retry is immediate: a brief drop should recover without a penalty. */
#define WIFI_RECONNECT_BASE_MS 1000u
/* Capped, never abandoned — an AP that returns hours later must still be rejoined
 * without a reboot. 30 s leaves the radio overwhelmingly free for BLE while still
 * reconnecting promptly once the AP is back. */
#define WIFI_RECONNECT_MAX_MS 30000u
/* Beyond this the shift would overflow; clamp on the COUNT before shifting. */
#define WIFI_RECONNECT_MAX_SHIFT 5u

/**
 * @brief Delay before the next reconnect attempt.
 *
 * @param consecutiveFailures failures since the last successful association;
 *                            0 means this is the first retry.
 * @return delay in milliseconds: 0, then 1s, 2s, 4s, 8s, 16s, then 30s forever.
 */
uint32_t iotWifiReconnectDelayMs(uint32_t consecutiveFailures)
{
    if (consecutiveFailures == 0u)
    {
        return 0u;
    }

    /* Clamp the SHIFT COUNT first. Shifting by >= 32 is undefined behaviour, and a
     * naive `1000u << failures` wraps to 0 for large counts — turning a long outage
     * straight back into the scan storm this function exists to stop. */
    uint32_t shift = consecutiveFailures - 1u;
    if (shift > WIFI_RECONNECT_MAX_SHIFT)
    {
        shift = WIFI_RECONNECT_MAX_SHIFT;
    }

    uint32_t delayMs = WIFI_RECONNECT_BASE_MS << shift;
    if (delayMs > WIFI_RECONNECT_MAX_MS)
    {
        delayMs = WIFI_RECONNECT_MAX_MS;
    }
    return delayMs;
}

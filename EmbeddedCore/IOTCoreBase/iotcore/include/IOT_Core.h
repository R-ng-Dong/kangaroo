#pragma once

#include <inttypes.h>
#include <stddef.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"
/* OTA moved to its own header on 2026-10-06; included here so IOT_Core.h callers keep
 * compiling unchanged. Prefer including IOT_CoreOta.h directly in new code. It sits with the
 * other includes, above the C-linkage block, rather than mid-file where it used to be. */
#include "IOT_CoreOta.h"
#include <stdbool.h>

/**
 * @brief Initialise the protocol engine
 *
 * Runs the normal boot sequence, UNLESS core finds a pending OTA request left by
 * a previous boot (IOT_CoreOtaIsPending()) -- in which case it runs an OTA-only
 * boot instead: BLE services are skipped (and BLE released via
 * IOT_CoreBleDeinit() if the caller's platform init already brought it up), and
 * the device goes straight to LAN/MQTT rather than entering provisioning. WiFi
 * and MQTT still come up on this path, so the download's progress and any
 * failure still reach the cloud. Once the network is up, core dispatches
 * OS_EVENT_OTA_BOOT_STARTED (IOT_CoreEventHandler.h) so an SDK that owns the
 * download knows to stop its own init and start it. This is NOT safe to leave
 * unclaimed: it is not enough to have registered an IOT_EVENT_OS callback (e.g.
 * one that only handles OS_EVENT_REBOOT, as every partner since the 08_29 sync
 * already does) -- the callback must set `data.otaBootStarted.handled = true`
 * (IOT_CoreEventHandler.h) to claim the download. No handler at all
 * (IOT_DevCoreDispatchEvent() returns IOT_ERR_NOT_FOUND) and a handler that
 * did not set the flag are treated identically: core discards the pending
 * request (IOT_CoreOtaDiscardPending()) and reboots itself -- see the full
 * explanation on OS_EVENT_OTA_BOOT_STARTED. Register the callback AND set the
 * flag when you claim the download.
 *
 * A NORMAL boot (no pending record) behaves exactly as before: this call takes no
 * new parameter and a caller that never sees an OTA request notices no difference.
 *
 * @param modelId 16-character model ID, NUL-terminated, assigned by Rogo device
 *                management. Read during init only.
 *
 * @return IOT_ERR_INVALID_STATE on the OTA path if the device is not provisioned
 *         (an OTA request cannot reach an unprovisioned device, so this should not
 *         happen in practice; there is deliberately no BLE fallback to advertise
 *         with here).
 *
 * @return IOT_ERR_OTA_BOOT_DEGRADED when the OTA request was unclaimed AND core
 *         could not even discard the pending record (e.g. an NVS erase
 *         failure) -- rebooting into that same unresolved state would repeat
 *         this exact failure forever, so core instead tries to bring BLE back
 *         up in place (IOT_BleInit() + IOT_CoreStartChangeWifiBle()) so the
 *         device is not left both stuck on the OTA record and unreachable over
 *         BLE. That recovery is itself best-effort; check the logs, not this
 *         return value, to know whether BLE actually came back.
 *
 * On a storage-bootstrap or network-start failure (the OTA path's not-provisioned
 * case included), core is left partially initialised: the work queue/task and
 * encryption are already up, and core already counts as initialised, so a
 * subsequent call to IOT_CoreInit() will itself fail with IOT_ERR_INVALID_STATE
 * instead of retrying from a clean state. A caller that wants to retry after such
 * a failure must reboot the device or call IOT_CoreDeinit() first. This does NOT
 * apply to a failure in the earlier work-queue or crypto-init steps: those return
 * before core counts as initialised, leaving core in a clean, uninitialised state
 * that a retry can start from directly.
 */

#ifdef __cplusplus
extern "C" {
#endif
iot_err_t IOT_CoreInit(const char *modelId);

iot_err_t IOT_CoreDeinit(void);

/**
 * @brief Provisioning mode selection.
 *
 * Controls how the device enters provisioning when not yet provisioned.
 * Default is IOT_PROVISION_BLE_LAN (BLE config + LAN discovery).
 * Call IOT_CoreSetProvisionMode() before IOT_CoreStartProvision() to override.
 *
 * Lived in IOT_ProvisionTypes.h until 2026-10-14; that header now forwards here.
 */
typedef enum
{
    IOT_PROVISION_BLE_LAN = 0, ///< BLE config + LAN discovery (default)
    IOT_PROVISION_LAN_ONLY,    ///< LAN config only, no BLE (not yet implemented)
    IOT_PROVISION_AP_MODE,     ///< Device as WiFi AP (not yet implemented)
} IOT_ProvisionMode_t;

/**
 * @brief Set the provisioning mode.
 *
 * Call before IOT_CoreStartProvision() to select a non-default mode.
 * Currently only IOT_PROVISION_BLE_LAN is implemented; other modes
 * return IOT_ERR_NOT_SUPPORTED.
 *
 * @param mode  The provisioning mode to use
 * @return IOT_OK on success, IOT_ERR_NOT_SUPPORTED for unimplemented modes,
 *         IOT_ERR_INVALID_ARG for invalid enum values
 */
iot_err_t IOT_CoreSetProvisionMode(IOT_ProvisionMode_t mode);

/**
 * @brief Set and persist the device timezone.
 *
 * Applies the POSIX TZ string to the platform timer layer, then stores it in
 * the core config DAO so reconnects and reboots can restore the same local
 * time behavior. UTC timestamp APIs remain UTC; timezone is only for local
 * calendar/schedule logic.
 *
 * @param timeZone POSIX TZ string, e.g. "ICT-7"
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG for invalid input, or a DAO/timer error
 */
iot_err_t IOT_CoreSetTimeZone(const char *timeZone);

/**
 * @brief Read the persisted device timezone from core config.
 *
 * @param outTimeZone Caller buffer for the null-terminated TZ string
 * @param outSize Size of outTimeZone in bytes
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if unset,
 *         IOT_ERR_INVALID_ARG/IOT_ERR_INVALID_SIZE for invalid output args
 */
iot_err_t IOT_CoreGetTimeZone(char *outTimeZone, size_t outSize);

/**
 * @brief Factory reset the device.
 *
 * Resets core-owned config (provision state, WiFi, MQTT, certs, etc.) and
 * dispatches DEV_EVENT_DEVICE_REMOVED (isRoot=true) so the SDK can reset
 * its own DAOs. The SDK event handler is expected to restart the chip.
 *
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if not provisioned
 */
iot_err_t IOT_CoreFactoryReset(void);

/**
 * @brief Run @p handler on the core service task.
 *
 * Driver callbacks — a BLE-mesh or Zigbee stack task, a timer — run on stacks too
 * small for NVS writes or an MQTT publish, and must not be blocked. Copy what you
 * need, hand it here, and return immediately.
 *
 * Public because an SDK implementing a hub transport needs exactly this and should
 * not have to stand up a worker task of its own to get it.
 *
 * @return IOT_ERR_NO_MEM if the queue is full — the caller owns @p arg and must
 *         free it rather than leak it.
 */
iot_err_t IOT_CoreDeferWork(void (*handler)(void *), void *arg);

/**
 * @brief Counters for the core work queue behind IOT_CoreDeferWork().
 *
 * Every inbound BLE/LAN/MQTT message and every deferred job runs on one core task,
 * fed by a fixed-size queue that drops work when full. These counters show whether
 * that is happening and why: how deep bursts go, how long each item takes, and
 * which handler was slowest.
 *
 * Reading them costs nothing on the device; log them next to your heap numbers
 * when diagnosing dropped commands.
 */
typedef struct
{
    uint32_t enqueued;                    /**< items accepted since boot */
    uint32_t dropped;                     /**< items rejected because the queue was full, since boot */
    uint32_t handled;                     /**< items that finished running */
    uint32_t totalHandlerMs;              /**< summed run time; divide by `handled` for the average */
    uint32_t maxHandlerMs;                /**< longest single item */
    void (*slowestHandler)(void *);       /**< the handler behind `maxHandlerMs` (resolve with addr2line) */
    uint16_t queueLen;                    /**< queue capacity */
    uint16_t pending;                     /**< items queued plus the one running, right now */
    uint16_t peakPending;                 /**< highest `pending` seen since boot */
    uint16_t burstDrops;                  /**< drops since the queue was last empty */
    uint32_t publishCount;                /**< MQTT publishes since boot, from any task */
    uint32_t publishTotalMs;              /**< summed publish time: lock wait plus TLS write */
    uint32_t publishMaxMs;                /**< longest single publish */
} IOT_CoreWorkStats_t;

/**
 * @brief Snapshot the core work queue counters.
 *
 * Safe to call from any task.
 *
 * @return IOT_ERR_INVALID_ARG if @p out is NULL, IOT_ERR_INVALID_STATE before
 *         IOT_CoreInit() has created the queue.
 */
iot_err_t IOT_CoreGetWorkStats(IOT_CoreWorkStats_t *out);

/**
 * @brief Root device mesh unicast address, as assigned at provisioning.
 */
iot_err_t IOT_CoreGetRootNwkAddr(uint16_t *addr);

/**
 * @brief Root device group address, as assigned at provisioning.
 */
iot_err_t IOT_CoreGetRootGroup(uint16_t *group);

/**
 * @brief Root device ID (12 bytes as provisioned).
 *
 * Copies at most @p outSize bytes and reports how many landed in @p outLen
 * (may be NULL). Truncates rather than failing on a short buffer.
 */
iot_err_t IOT_CoreGetRootDeviceId(uint8_t *outDevId, uint16_t outSize, uint16_t *outLen);

/**
 * @brief Start provisioning mode (BLE advertising + LAN config)
 *
 * Call this to (re-)enter provisioning mode after IOT_CoreInit() has been called.
 * IOT_CoreInit() calls this automatically when the device is not yet provisioned.
 * Safe to call when already provisioning (returns IOT_OK as no-op).
 *
 * @return IOT_OK on success, IOT_ERR_INVALID_STATE if core is not initialized
 */
iot_err_t IOT_CoreStartProvision(void);

/**
 * @brief Stop provisioning mode (stop BLE advertising, tear down config listeners)
 *
 * Safe to call when not provisioning (no-op).
 *
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreStopProvision(void);

/**
 * @brief Check if device is provisioned (non-blocking, cached in RAM).
 * @return true if provisioned, false otherwise
 */
bool IOT_CoreIsProvisioned(void);

/**
 * @brief Get root device Element ID.
 * @param eid Pointer to receive the root EID
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if not provisioned
 */
iot_err_t IOT_CoreGetRootEid(uint16_t *eid);

/**
 * @brief Check whether the cloud channel is genuinely usable right now.
 *
 * "Connected" here means the MQTT session is up AND every standard topic
 * subscription has been confirmed — not the earlier, weaker "TCP session open" moment. Anything published before this point (a
 * status NOTIFY, for example) has no subscriber yet and is silently lost.
 *
 * Non-blocking; reflects a flag maintained by the MQTT event callbacks, so it
 * is cheap to poll from a boot-time wait loop.
 *
 * @return true if MQTT is connected and all topic subscriptions are confirmed,
 *         false otherwise (including before the first connect and after any
 *         disconnect).
 */
bool IOT_CoreIsCloudConnected(void);

/**
 * @brief Set the device's WiFi credentials and (re)connect.
 *
 * Routes through the change-WiFi service, which tears the cloud session down before
 * switching networks rather than leaving it to fail — so this is safe to call on a
 * device that is currently online.
 *
 * Intended for callers outside the protocol path, such as a service console. The
 * protocol path reaches the same service directly from the app's set-credentials command and does
 * not use this. No message-layer acknowledgement is sent, because there is no
 * originating message to acknowledge.
 *
 * @param ssid  network name, NUL-terminated
 * @param pwd   passphrase, NUL-terminated
 * @return IOT_OK if the change was accepted; the connection result arrives later as a
 *         device event, not as this return value.
 */
iot_err_t IOT_CoreSetWifiCredentials(const char *ssid, const char *pwd);

/**
 * @brief Connect the cloud (MQTT) session using stored configuration.
 *
 * Pairs with IOT_CoreIsCloudConnected(), which reports when the session is up AND every
 * topic subscription is confirmed. This returns as soon as the attempt is started.
 *
 * @return IOT_OK if the attempt was started, IOT_ERR_FAIL if there is no MQTT client.
 */
iot_err_t IOT_CoreCloudConnect(void);

/**
 * @brief Disconnect the cloud (MQTT) session, leaving the client allocated.
 *
 * Disconnecting when nothing is connected is a harmless no-op.
 *
 * @return IOT_OK on success.
 */
iot_err_t IOT_CoreCloudDisconnect(void);


/**
 * @brief Raise a core event to whatever handlers are registered for its category.
 *
 * The supported way for SDK-side services to emit events that application handlers see,
 * without reaching into core's internal dispatch. `IOT_CoreEvent_t` is declared in
 * IOT_CoreEventHandler.h.
 *
 * @param event  the event to dispatch; must not be NULL
 * @return IOT_OK if the event was accepted for dispatch.
 */
iot_err_t IOT_CoreDispatchEvent(const IOT_CoreEvent_t *event);

/**
 * @brief Connect WiFi using the credentials already in storage.
 *
 * For a boot that skips the normal LAN bring-up but still needs the network.
 * Returns once the association has been requested; use the network events to
 * learn when it is up.
 *
 * NOT used by the OTA path IOT_CoreInit() takes when a request is pending: that
 * path takes the full LAN bring-up path itself (mDNS / TCP server / MQTT
 * included, which is what associates), so calling this too would race a second
 * association against a link already coming up. There is currently no
 * production caller — this stays public API for a future boot mode that wants
 * connectivity without the rest of LAN bring-up.
 */
iot_err_t IOT_CoreConnectStoredWifi(void);

/**
 * @brief Start BLE advertisement in "change WiFi" mode (provisioned device only).
 *
 * Advertises the provisioning service with the device's own obfuscated deviceId in
 * manufacturer data, so a paired app can reconnect, scan, and send new WiFi
 * credentials without a factory reset.
 *
 * Only valid when the device is provisioned.
 *
 * @return IOT_OK on success, IOT_ERR_INVALID_STATE if not provisioned or core not init
 */
iot_err_t IOT_CoreStartChangeWifiBle(void);

/**
 * @brief Stop the change-WiFi BLE advertisement started by IOT_CoreStartChangeWifiBle.
 *
 * Safe to call even if change-WiFi BLE is not currently running (returns IOT_OK).
 * Call this when cloud connectivity is restored so the BLE advertisement is
 * cleaned up and the BLE stack can be released.
 *
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreStopChangeWifiBle(void);

/**
 * @brief Configuration for the change-WiFi service.
 *
 * Pass to IOT_CoreConfigureChangeWifi() BEFORE calling IOT_CoreInit().
 * If not called, the defaults below apply.
 */
typedef struct {
    bool     bleFallbackEnabled;  /**< Auto-start BLE when WiFi is down. Default: true  */
    /** Extra delay before the fallback advert starts, once an outage is confirmed.
     *
     * NOT the debounce. Transient drops are filtered by counting consecutive failed
     * reconnect attempts (the platform re-fires its disconnect event on every one),
     * so by the time this delay is applied the AP is already known to be gone.
     * Leave at the default unless you want the advert held back further.
     *
     * Default: 0 */
    uint32_t bleFallbackDelayMs;
} IOT_CoreChangeWifiConfig_t;

/**
 * @brief The defaults that apply when IOT_CoreConfigureChangeWifi() is never called.
 *
 * Defined here, beside the documentation that describes it, and used as the
 * initializer inside core — one definition, so the documented default and the
 * shipped default cannot drift apart. They had: the header said 30000 while the
 * initializer said 0, for as long as both existed.
 */
#define IOT_CORE_CHANGE_WIFI_CONFIG_DEFAULT \
    { .bleFallbackEnabled = true, .bleFallbackDelayMs = 0u }

/**
 * @brief Configure the change-WiFi service (optional).
 *
 * Must be called before IOT_CoreInit(). If not called, defaults apply
 * (BLE fallback enabled, no extra delay once an outage is confirmed).
 *
 * @param config Pointer to config struct. Must not be NULL.
 * @return IOT_OK, IOT_ERR_INVALID_ARG if config is NULL,
 *         IOT_ERR_INVALID_STATE if called after IOT_CoreInit()
 */
iot_err_t IOT_CoreConfigureChangeWifi(const IOT_CoreChangeWifiConfig_t *config);

/**
 * @brief Get boot counter value (BLOCKING).
 * @param count Output: current boot counter value
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreGetBootCounter(uint8_t *count);

/**
 * @brief Set boot counter value (BLOCKING).
 * @param count Boot counter value to store
 * @return IOT_OK on success
 */
iot_err_t IOT_CoreSetBootCounter(uint8_t count);

/**
 * @brief Set boot counter value asynchronously (NON-BLOCKING).
 * Safe to call from timer callbacks.
 * @param count Boot counter value to store
 * @return IOT_OK if queued successfully
 */
iot_err_t IOT_CoreSetBootCounterAsync(uint8_t count);

/**
 * @brief Pause/resume the change-WiFi BLE fallback around a download.
 *
 * The fallback would otherwise try to advertise on a BLE stack the SDK has torn
 * down to free heap for the download, which faults. Suspend before releasing
 * BLE; resume when the download ends without a reboot.
 */
iot_err_t IOT_CoreChangeWifiSuspendBle(void);
iot_err_t IOT_CoreChangeWifiResumeBle(void);

/**
 * @brief Tear the BLE stack down and reclaim its heap.
 *
 * Stops advertising first, then deinitialises the stack, so iotcore's own BLE
 * bookkeeping stays consistent — deinitialising the platform stack directly
 * would leave core believing BLE is still up.
 *
 * The usual reason to call this is to free contiguous heap before an OTA
 * download: BLE typically holds tens of KB, and a TLS download needs a single
 * ~16.7 KB contiguous block that a fragmented heap often cannot produce.
 * Suspend the change-WiFi fallback first (IOT_CoreChangeWifiSuspendBle) or a
 * WiFi drop mid-download will try to advertise on the stack you just removed.
 */
iot_err_t IOT_CoreBleDeinit(void);

/* ============================================================================
 *  Config diagnostics — a safe subset for tooling (e.g. the service console)
 * ==========================================================================*/

/**
 * @brief Callback invoked once per safe config key by IOT_CoreConfigDumpSafe().
 *
 * @param name Human-readable key name (stable across releases).
 * @param data Raw stored bytes, or NULL when the key is unset.
 * @param len  Length of @p data in bytes (0 when unset).
 * @param user The same pointer passed to IOT_CoreConfigDumpSafe().
 *
 * Do not retain @p data past the call — it is only valid for its duration.
 */
typedef void (*IOT_CoreConfigDumpCb_t)(const char *name, const void *data, uint16_t len, void *user);

/**
 * @brief Dump the config keys core considers safe to display.
 *
 * "Safe" means non-secret: core owns the list, and no credential, URL, or
 * certificate is ever handed to the callback — deciding which config values
 * are safe to expose is a security judgement that belongs here, with the code
 * that knows which keys are encrypted, not with whatever tool is doing the
 * printing. The callback runs once per safe key, in an unspecified-but-stable
 * order, whether or not the key currently has a value; the caller owns
 * formatting only.
 *
 * @param cb   Called once per safe key. Must not be NULL.
 * @param user Opaque pointer passed through to every @p cb call.
 * @return IOT_OK, or IOT_ERR_INVALID_ARG when @p cb is NULL.
 */
iot_err_t IOT_CoreConfigDumpSafe(IOT_CoreConfigDumpCb_t cb, void *user);
#ifdef __cplusplus
}
#endif

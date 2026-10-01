#pragma once


#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stddef.h>
#include "IOT_ErrorManager.h"
#include "IOT_ProvisionTypes.h"
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
 * encryption are already up, and `s_coreInitialized` is already true, so a
 * subsequent call to IOT_CoreInit() will itself fail with IOT_ERR_INVALID_STATE
 * instead of retrying from a clean state. A caller that wants to retry after such
 * a failure must reboot the device or call IOT_CoreDeinit() first. This does NOT
 * apply to a failure in the earlier work-queue or crypto-init steps: those return
 * before `s_coreInitialized` is set, leaving core in a clean, uninitialised state
 * that a retry can start from directly.
 */
iot_err_t IOT_CoreInit(const char *modelId);

iot_err_t IOT_CoreDeinit(void);

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
 * @brief Load mesh credentials from config DAO and join the mesh network.
 *
 * Loads network key and app key from config DAO, decrypts them, builds
 * the root device key (from devId + EID), and calls IOT_MeshJoinNetwork().
 *
 * Called by SDK during init if device is already provisioned, or internally
 * by the MESH handler after the last app key is received from cloud.
 *
 * @return IOT_OK on success, IOT_ERR_DATA_NOT_FOUND if keys not yet stored
 */
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
 * subscription has been confirmed — the same bar
 * IOT_LAN_EVENT_MQTT_ALL_SUBSCRIBED already gates elsewhere, not the earlier,
 * weaker "TCP session open" moment. Anything published before this point (a
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
 * @brief Start BLE advertisement in "change WiFi" mode (provisioned device only).
 *
 * Advertises with WILEDIRECT PROV UUID (00007269) and the device's own obfuscated
 * deviceId in manufacturer data, so a paired app can reconnect and update WiFi
 * credentials via SETTING_WIFI_SCAN / SETTING_WIFI_SSID_PWD without a factory reset.
 *
 * Only valid when the device is provisioned.
 *
 * @return IOT_OK on success, IOT_ERR_INVALID_STATE if not provisioned or core not init
 */
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
    uint32_t bleFallbackDelayMs;  /**< ms to wait after WiFi drop before BLE. Default: 30000 */
} IOT_CoreChangeWifiConfig_t;

/**
 * @brief Configure the change-WiFi service (optional).
 *
 * Must be called before IOT_CoreInit(). If not called, defaults apply
 * (BLE fallback enabled, 30 s delay).
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

/* ============================================================================
 *  OTA — the SDK owns the download, iotcore owns the protocol
 * ==========================================================================*/

/**
 * @brief An OTA request iotcore has accepted and is holding for the SDK.
 *
 * Retrieved with IOT_CoreOtaGetPending(), which is the way to act on a request
 * AFTER the dispatch that announced it — typically on the next boot, when the
 * SDK has deliberately rebooted into a stripped-down mode to free contiguous
 * heap for the TLS download. Release with IOT_CoreOtaFreePending().
 *
 * `cert` is re-read from storage on every call, so the SDK never has to persist
 * it (and could not: it lives in an internal config namespace).
 */
typedef struct
{
    char *url;             /**< Full URL including scheme. */
    char *authToken;       /**< Bearer token, or NULL. May have expired if the
                                request sat across a long reboot — a 401 should be
                                reported as a failure so the cloud reissues. */
    uint16_t authTokenLen;
    char *cert;            /**< CA cert PEM, or NULL when none is provisioned. */
    uint16_t certLen;
    char targetVersion[32];/**< Empty until the download reaches the writing stage. */
    uint8_t autoUpdate;
} IOT_CoreOtaRequest_t;

/**
 * @brief True only while a download is still owed — never merely "core has an
 *        OTA record to report".
 *
 * Reads only the pending record — not the encrypted URL — so it is safe to call
 * before IOT_EncryptInit(), i.e. before core has been initialised. Use it to
 * decide whether this boot is an OTA boot before committing to a boot mode.
 *
 * Bootstraps the config DAO itself if it is not already up (the bootstrap call
 * is idempotent, so this is safe to call unconditionally, every time). This
 * makes the predicate self-sufficient: a caller does not need to know or
 * replicate core's own init order just to ask this question before it runs.
 *
 * True only for IOT_OTA_PENDING_HANDED_OFF. APPLIED (a successful download,
 * verified on the very next boot — the one running the new image), FAILED and
 * DECLINED (both awaiting only a status report on a normal boot with
 * connectivity) all return false: none of them means this boot exists to
 * download.
 */
bool IOT_CoreOtaIsPending(void);

/**
 * @brief Fetch the OTA request iotcore is holding.
 * @param out Filled on success. Free with IOT_CoreOtaFreePending().
 * @return IOT_OK, IOT_ERR_NOT_FOUND when no request is pending,
 *         IOT_ERR_INVALID_ARG when out is NULL.
 */
iot_err_t IOT_CoreOtaGetPending(IOT_CoreOtaRequest_t *out);

/**
 * @brief Release the buffers in a request returned by IOT_CoreOtaGetPending().
 */
void IOT_CoreOtaFreePending(IOT_CoreOtaRequest_t *req);

/**
 * @brief Decline the pending OTA and drop it.
 *
 * Also tells the cloud the device will not update, so the request is answered
 * rather than left hanging. Call this when the SDK decides not to update at all; do
 * NOT call it merely to defer, or the request is lost.
 *
 * When the request is actually dropped, this also resets the SDK's OTA-boot
 * attempt counter (the retry budget your SDK enforces across reboots) — the
 * counter belongs to the transaction this call just closed,
 * and must not carry into whatever OTA request comes next. If the decline
 * notice cannot be sent (no connectivity), the request is deferred rather
 * than dropped — the record and the counter are BOTH left in place, to be
 * retried together on a later boot with connectivity.
 *
 * @return IOT_OK when the request was genuinely resolved -- either dropped
 *         (both keys erased) or deferred (rewritten IOT_OTA_PENDING_DECLINED
 *         and persisted); IOT_ERR_NOT_FOUND when nothing was pending; otherwise
 *         the underlying erase or persist failure, propagated. On anything
 *         other than IOT_OK the on-flash record is UNCHANGED -- still
 *         IOT_OTA_PENDING_HANDED_OFF with its counter untouched. A caller that
 *         means to reboot afterward MUST check this for IOT_OK specifically: an
 *         SDK's give-up path calls this and must not reboot on anything else, or
 *         it repeats the same give-up forever on a record that never actually
 *         changed.
 */
iot_err_t IOT_CoreOtaClearPending(void);

/**
 * @brief Discard the pending OTA record and its attempt counter, without ever
 *        trying to notify the cloud.
 *
 * This is NOT a quieter IOT_CoreOtaClearPending(): that one always tries to
 * answer the cloud first and, if the send fails, DEFERS instead of erasing —
 * it rewrites the record IOT_OTA_PENDING_DECLINED and keeps both the record
 * and the counter for a retry on a later boot with connectivity.
 * IOT_CoreOtaDiscardPending() never attempts a send at all, so it always
 * erases both keys unconditionally, whether or not the network is up.
 *
 * Call this ONLY when a notify attempt is known to be pointless — e.g. when
 * core's own init failed early enough that the network can never be up on
 * this boot. Using IOT_CoreOtaClearPending() there would always fail its send
 * and leave the record deferred rather than gone, re-entering the same
 * failure on every subsequent boot.
 *
 * @return IOT_OK only if the record existed AND both underlying erases
 *         (the record and the attempt counter) reported success;
 *         IOT_ERR_NOT_FOUND when nothing was pending; otherwise the first
 *         erase failure, propagated. A caller that means to reboot afterward
 *         MUST check this for IOT_OK specifically -- on anything else the
 *         record may still be HANDED_OFF with its counter untouched, and
 *         rebooting anyway re-enters whatever put it there. IOT_CoreInit()'s
 *         own OTA path does exactly this check before its own reboot.
 */
iot_err_t IOT_CoreOtaDiscardPending(void);

/**
 * @brief Lean-boot attempts already spent on the current pending OTA request.
 *
 * Core only stores this counter — it persists it and hands back what is
 * stored. The retry BUDGET it is compared against is a caller policy core has
 * no opinion on — your SDK owns it.
 *
 * @return 0 when no counter is stored yet (a fresh transaction), else the
 *         stored count.
 */
uint8_t IOT_CoreOtaGetAttempts(void);

/**
 * @brief Increment and persist the lean-boot attempt counter.
 *
 * Call this before starting a download attempt, not after: a crash mid-
 * download must still leave the count advanced, or a device that dies on
 * every attempt would re-enter OTA mode forever instead of ever hitting the
 * caller's give-up budget.
 *
 * @return IOT_OK, or an error if the counter could not be persisted.
 */
iot_err_t IOT_CoreOtaRecordAttempt(void);

/**
 * @brief Progress callback that emits every OTA protocol message for you.
 *
 * Hand this straight to the platform downloader and the SDK has no message
 * bookkeeping at all — iotcore maps each platform status to the right
 * notification, records the target version, marks the request applied so the
 * next boot verifies it, and drops the request on failure:
 *
 *     IOT_AppManagerStartUpdateProcess(req.url, req.cert, req.certLen,
 *                                      req.authToken, req.authTokenLen,
 *                                      NULL, IOT_CoreOtaPlatformProgressCb);
 *
 * Sending is best-effort: with MQTT down (an OTA-only boot, say) the durable
 * state is still updated and the outcome is reported after the next boot.
 * There is no "success" call — success is proven by the running version after
 * the reboot, not claimed by the downloader.
 */
void IOT_CoreOtaPlatformProgressCb(uint8_t status, uint8_t progress, void *user_data);

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
#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventTypes.h"

/*
 * OS events: reboot requested, OTA requested, OTA boot started.
 *
 * Split out of IOT_CoreEventHandler.h on 2026-10-14. That header remains the umbrella
 * (category enum, IOT_CoreEvent_t, callback registration) and includes this file, so
 * existing includes keep working. Prefer this header in new code that only needs it.
 */

/* ============================
 *  OS EVENT
 * ============================ */

#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    /** The cloud asked this device to restart. A REQUEST, not a notice: core does not restart
     *  the chip. Claim it by writing `data.rebootRequested.result = IOT_OK` once you have
     *  arranged the restart (see IOT_CoreOsEventRebootRequestedData_t), and restart shortly
     *  after returning so the reply can still leave the device. Unclaimed, the cloud is told
     *  the request is not supported and nothing restarts. */
    OS_EVENT_REBOOT_REQUESTED,
    /** The cloud asked for a firmware update. A REQUEST you claim, not a notice. Core has
     *  parsed and saved it (IOT_CoreOtaGetPending() can read it back, even from inside your
     *  callback) but does NOT download and has not answered yet. Claim it by writing
     *  `data.otaRequested.claim` (see IOT_CoreOtaClaim_t) before returning: core answers the
     *  cloud from that claim once your callback returns, so do NOT restart inside the callback;
     *  arrange the restart (a short timer) and return. Unclaimed, core erases the saved request
     *  and tells the cloud the update is not supported. */
    OS_EVENT_OTA_REQUESTED,
    /** This boot exists only to run a previously-requested OTA download, one you claimed
     *  with IOT_CORE_OTA_CLAIM_AFTER_REBOOT (no other claim leads here).
     *  IOT_CoreInit() found a pending OTA record on entry, so it ran its OTA boot
     *  path itself instead of the normal one: BLE was released and WiFi/MQTT are
     *  already up by the time this dispatches. (BLE release itself can be refused
     *  -- IOT_CoreBleDeinit() returns IOT_ERR_INVALID_STATE while a mesh hub role
     *  owns the radio -- and core logs and continues in OTA mode rather than
     *  failing the boot over it; but nothing sets that role before this point in a
     *  boot, so in practice BLE is always down here. The check stays for whatever
     *  future boot ordering might change that.)
     *
     *  THIS EVENT MUST BE CLAIMED, not merely received. An SDK that acts on it
     *  sets `data.otaBootStarted.handled = true` (see below) before returning
     *  from its callback, then skips the rest of its own initialisation and
     *  starts the download. A registered IOT_EVENT_OS callback that does
     *  something else with this event type -- including a partner's existing
     *  handler that only knows OS_EVENT_REBOOT_REQUESTED and falls through a `default`
     *  case without touching `handled` -- leaves the flag false. Core checks it:
     *  IOT_DevCoreDispatchEvent() returning IOT_OK is NOT by itself proof anyone
     *  is running this download, because that same IOT_OK is what an
     *  IOT_EVENT_OS handler registered for a completely different reason (e.g.
     *  OS_EVENT_REBOOT_REQUESTED, which every partner since the 08_29 sync already
     *  handles) also returns while ignoring this one. Core therefore requires
     *  BOTH the dispatch to return IOT_OK AND `handled` to be true; anything
     *  else -- no handler registered at all (IOT_ERR_NOT_FOUND), or a handler
     *  registered that did not set the flag -- is treated identically: core
     *  marks the update failed (the normal boot that follows tells the cloud)
     *  and reboots itself, rather than let the SDK fall through into a boot with
     *  no BLE and no OTA either. This is the one case where core restarts the
     *  device on its own. Falling through un-rebooted would be worse: BLE
     *  stays released with nothing to bring it back, so every later boot finds
     *  the same record still pending, takes the OTA path again, and never has
     *  BLE again -- permanently losing provisioning and the change-WiFi
     *  fallback. The self-reboot trades that for "this update failed", which
     *  is recoverable. (If even marking the record failed does not land --
     *  e.g. an NVS write failure -- core does not reboot into that same unresolved
     *  state either: it tries to bring BLE back up in place instead, so the
     *  device is not left both OTA-stuck and BLE-less.) Register the handler AND
     *  set the flag when you claim the download -- do not rely on receiving this
     *  event, or a nonzero dispatch result, as proof of anything. */
    OS_EVENT_OTA_BOOT_STARTED,
} IOT_CoreOsEventType_t;

/**
 * @brief How the SDK takes an update request (OS_EVENT_OTA_REQUESTED's `claim`).
 *
 * Core presets IOT_CORE_OTA_CLAIM_NONE and reads the value back when your callback returns. The
 * dispatch's own result never counts as a claim.
 */
typedef enum
{
    /** Not taken (the preset). Core erases the saved request and tells the cloud the update is
     *  not supported. Also what any value other than the two below means. */
    IOT_CORE_OTA_CLAIM_NONE = 0,
    /** You download in this boot (you started it, or will, from this boot). Core tells the cloud
     *  the update is accepted. If the device restarts before the image is written, the next boot
     *  is a normal boot and core tells the cloud the update failed; the cloud or the user retries. */
    IOT_CORE_OTA_CLAIM_NOW = 1,
    /** You restart into an update boot and download there. Core tells the cloud the update is
     *  accepted; restart a moment after your callback returns (a short timer), never inside it.
     *  The next boot is an update boot: OS_EVENT_OTA_BOOT_STARTED, which you must claim too. */
    IOT_CORE_OTA_CLAIM_AFTER_REBOOT = 2,
} IOT_CoreOtaClaim_t;

/**
 * @brief OTA request handed to the SDK.
 *
 * Pointers are BORROWED — valid only for the duration of the synchronous
 * dispatch, same convention as IOT_CoreIrEvent_t. To act on the request later
 * (e.g. after a reboot), call IOT_CoreOtaGetPending() instead of stashing these.
 *
 * `claim` is your answer (IOT_CoreOtaClaim_t). To download in this boot, pass the fields
 * straight to the platform downloader together with core's own progress callback, which emits
 * every protocol message for you, and claim IOT_CORE_OTA_CLAIM_NOW if it started:
 *
 *     if (IOT_AppManagerStartUpdateProcess(d->url, d->cert, d->certLen,
 *                                          d->authToken, d->authTokenLen,
 *                                          NULL, IOT_CoreOtaPlatformProgressCb) == IOT_OK)
 *         d->claim = IOT_CORE_OTA_CLAIM_NOW;
 */
typedef struct
{
    const char *url;        /**< Full URL including scheme. Never NULL. */
    const char *authToken;  /**< Bearer token, or NULL when the server needs none. */
    uint16_t authTokenLen;  /**< 0 when authToken is NULL. */
    const char *cert;       /**< CA cert PEM provisioned for HTTPS, or NULL if unset. */
    uint16_t certLen;       /**< 0 when cert is NULL. */
    uint8_t autoUpdate;     /**< Cloud's auto-update preference, already persisted by core. */
    IOT_CoreOtaClaim_t claim; /**< Your answer. Preset to IOT_CORE_OTA_CLAIM_NONE. */
} IOT_CoreOsEventOtaRequestedData_t;

/**
 * @brief Acknowledgement for OS_EVENT_OTA_BOOT_STARTED.
 *
 * Core sets `handled = false` before dispatching and reads it back (dispatch is
 * synchronous) immediately after IOT_DevCoreDispatchEvent() returns. Set it to
 * true from your IOT_EVENT_OS callback to claim this download -- this is how
 * core tells "an SDK is genuinely running the download" apart from "an SDK
 * happened to be registered for a different OS event and returned IOT_OK while
 * ignoring this one" (see OS_EVENT_OTA_BOOT_STARTED above). Leaving it false
 * (not registering a handler at all, or registering one that does not set it)
 * makes core mark the update failed and reboot to a normal boot.
 */
typedef struct
{
    bool handled; /**< Set true to claim the download. Read by core after dispatch returns. */
} IOT_CoreOsEventOtaBootStartedData_t;

/**
 * @brief Answer for OS_EVENT_REBOOT_REQUESTED.
 *
 * Core presets `result` to IOT_ERR_NOT_SUPPORTED, dispatches synchronously and reads it back
 * when your callback returns; core's own restart never happens. Write IOT_OK once the restart
 * is arranged (typically a short timer, so the reply to the cloud can leave first), or an
 * error code if you could not arrange it. The cloud is told success for IOT_OK and failure
 * with that code otherwise. Leaving it untouched, or registering no IOT_EVENT_OS handler,
 * tells the cloud the request is not supported.
 */
typedef struct
{
    iot_err_t result; /**< Your answer. Preset to IOT_ERR_NOT_SUPPORTED. */
} IOT_CoreOsEventRebootRequestedData_t;

typedef struct
{
    IOT_CoreOsEventType_t type;
    union
    {
        IOT_CoreOsEventRebootRequestedData_t rebootRequested;   /**< Valid for OS_EVENT_REBOOT_REQUESTED */
        IOT_CoreOsEventOtaRequestedData_t otaRequested;         /**< Valid for OS_EVENT_OTA_REQUESTED */
        IOT_CoreOsEventOtaBootStartedData_t otaBootStarted;     /**< Valid for OS_EVENT_OTA_BOOT_STARTED */
    } data;
} IOT_CoreOsEvent_t;
#ifdef __cplusplus
}
#endif

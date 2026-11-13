#pragma once

#include <inttypes.h>
#include <stddef.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"
#include <stdbool.h>

/*
 * OTA: the SDK owns the download, iotcore owns the protocol.
 *
 * Split out of IOT_Core.h on 2026-10-06. That header still includes this one, so existing
 * includes keep working.
 */

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

#ifdef __cplusplus
extern "C" {
#endif
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
 * before core has been initialised. Use it to
 * decide whether this boot is an OTA boot before committing to a boot mode.
 *
 * Bootstraps the config DAO itself if it is not already up (the bootstrap call
 * is idempotent, so this is safe to call unconditionally, every time). This
 * makes the predicate self-sufficient: a caller does not need to know or
 * replicate core's own init order just to ask this question before it runs.
 *
 * True only for a request claimed IOT_CORE_OTA_CLAIM_AFTER_REBOOT whose
 * download has not written its image yet. A request claimed
 * IOT_CORE_OTA_CLAIM_NOW is never pending: it belongs to the boot that claimed
 * it, and if that boot ended before the image was written, the next boot is a
 * normal one that reports the update failed. A successful download (verified
 * on the very next boot — the one running the new image) and a failed or
 * declined one (awaiting only a status report on a normal boot with
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
 *         rebooting anyway re-enters whatever put it there.
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
#ifdef __cplusplus
}
#endif

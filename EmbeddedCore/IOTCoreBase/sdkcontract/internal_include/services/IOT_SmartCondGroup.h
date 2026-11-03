#pragma once
/**
 * Distributed cond-group (AND/OR, K-of-N) scoreboard.
 *
 * Each participating device stores ONE condition of a K-of-N group (its own
 * bit) in the trigger DAO and keeps a small RAM scoreboard of the group's
 * latched bitmask. When a device's own condition flips it broadcasts COND_STATE;
 * whichever device's own flip carries the group from < K to >= K true bits
 * fires the existing ACTIVE_BY_TRIGGER. Peer-driven transitions are latched
 * silently (the flipping peer fires for itself).
 *
 * MARKER: a trigger is a cond-group iff its stored seat count is >= 2. subSmartType
 * is NOT the marker — every automation's trigger bind has the same shape
 * (subSmartType 0 plus a condition-group description) and the seat count decides. A single seat is
 * the degenerate case: same code path, no peers, no broadcasts.
 */
#include <stdbool.h>
#include <stddef.h> // NULL — used by the inline predicate below
#include <stdint.h>
#include "dao/IOT_DaoSmartTriggerMgmt.h"
#include "services/IOT_ServiceSmart.h" // SMART_SUB_TYPE_SYNC_ON_OFF
#include "IOT_CoreReporter.h"      // IOT_CoreSmartReportCondState_t
#include "IOT_CoreEventHandler.h"  // IOT_CoreSmartCondStatus_t

/* Minimum seats for a distributed group. N <= 1 is an ordinary single-seat trigger. */

#ifdef __cplusplus
extern "C" {
#endif
#define SMART_COND_GROUP_MIN_N 2

/* Maximum seats in one group. This is the width of the scoreboard bitmask (one byte) AND
 * the number of per-seat keys the trigger DAO stores, so the two MUST agree — keep it here,
 * the one place both sides include. */
#define SMART_COND_GROUP_MAX_N 8

/**
 * @brief Does this stored trigger engage the distributed peer machinery?
 *
 * The ONLY group discriminator. Sync-on-off (subSmartType 71) is excluded: it has its
 * own dispatch arm and must never occupy a scoreboard seat.
 */
static inline bool SmartTriggerIsCondGroup(const IOT_SmartTrigger_t *t)
{
    return t != NULL &&
           t->subSmartType != SMART_SUB_TYPE_SYNC_ON_OFF &&
           t->condCount >= SMART_COND_GROUP_MIN_N;
}

/**
 * @brief Initialize the cond-group scoreboard (creates its lock). Call ONCE from
 * the single-threaded SDK-init path, before any cond-group callback can run on
 * the timer task or core's event task. Idempotent.
 */
void SmartService_CondGroupInit(void);

/**
 * @brief On our way out of a group, tell the survivors what shape it now has.
 *
 * Only the REMOVED device receives the unbind, so nobody else learns that the
 * group shrank — the survivors keep the old N/K and wait forever on a seat that no longer
 * exists. The unbind does carry the new shape in its condition-group description, so the departing
 * device relays it to the location before deleting its trigger and the survivors adopt it
 * through the ordinary "newer generation" rule.
 *
 * Seat retirement cannot cover this case: it is gated on the group already looking
 * satisfied, and a group short of a seat can never reach K, so no ask is ever sent.
 *
 * No-op unless (condCount, fireCount) describe a real group and we actually hold a trigger
 * for this SMID — the announcement carries our own departing seat, and a device that holds
 * nothing has no seat to speak from.
 *
 * @param smid       the automation being left
 * @param version    config generation from the unbind (must outrank the survivors' own)
 * @param condCount  N after the removal
 * @param fireCount  K after the removal
 */
void SmartService_CondGroupAnnounceRemoval(uint16_t smid, uint16_t version,
                                           uint8_t condCount, uint8_t fireCount);

/**
 * Initialise the deferred SYNC-answer slot pool (static, mutex-guarded). Must be called
 * once on the single-threaded SDK-init path, before any cond-group GET can be dispatched.
 * Idempotent; also clears any pending answers.
 */
void SmartService_CondAnswerPoolInit(void);

/* Max concurrent cond-group SMIDs tracked per device. Note: this is the group COUNT,
 * not the input count — each group's bitmask is a single byte (<=8 inputs per group). */
#define COND_GROUP_MAX_ENTRIES 16

/**
 * @brief Validate a BIND_TRIGGER's cond-group parameters.
 *
 * A bind with no condition-group description is always valid. A description
 * declaring fewer than 2 seats is also valid — it is an ordinary single-seat trigger.
 * A real group requires N in [2..8], K in [1..N], and condIndex in [0..N-1].
 * The description, not the subtype, is the marker — which is why no subSmartType is taken.
 *
 * @return true if the bind may be stored, false if it must be rejected.
 */
bool SmartCondGroupBindIsValid(uint8_t hasCondGroup,
                               uint8_t condIndex, uint8_t condCount, uint8_t fireCount);

/**
 * @brief Bitmask of the seats this device holds for @p smid (bit i = seat i).
 *
 * Cached in the scoreboard entry and probed from the DAO exactly once, when the entry is
 * created — the trigger-evaluation path runs on every state change and must not probe.
 * Returns 0 if the SMID is not a group this device participates in.
 */
uint8_t SmartService_CondGroupOwnSeats(uint16_t smid);

/**
 * @brief This device's own condition result changed — detect a flip, broadcast
 *        it, and fire if our flip completes the K-of-N threshold.
 *
 * Called from the trigger-evaluation pipeline for subSmartType==2048 triggers.
 * @param trig  the cond-group trigger (provides smid / condIndex / N / K).
 * @param result  current boolean result of this device's condition.
 * @param attrValue,attrSize  value forwarded to the fire path (cloud report).
 */
void SmartService_CondGroupOnLocalResult(const IOT_SmartTrigger_t *trig, bool result,
                                         const uint8_t *attrValue, uint16_t attrSize);

/**
 * @brief Merge a peer's cond-report (REPORT_COND_STATE or a SYNC response) via the
 * version rule. A device is only ever authoritative for its OWN bit:
 *   - higher version → adopt version + N/K, clip our mask to the new N (drops bits of
 *     departed members), then update ONLY the sender's bit (keep our own bit);
 *   - equal version → update ONLY the sender's bit (keep our own bit);
 *   - lower version → ignore.
 * Own echoes dropped.
 *
 * If this merge crosses the group from <K to >=K (i.e. a PEER's bit completed it,
 * not a local flip of ours), and our own bit is set, it elects a rank-ordered, suppressible
 * DEFERRED fire (see SmartService_CondGroupOnActiveByTrigger). This is the only path by
 * which a peer-driven completion fires — own-flip completion still fires immediately in
 * SmartService_CondGroupOnLocalResult.
 *
 * @return true iff the incoming version was HIGHER than ours (a config-generation bump).
 *         The caller should then schedule a jittered self re-announce
 *         (SmartService_CondGroupScheduleReannounce) so every member re-floods its own
 *         bit at the new generation and the full mask converges. Equal/lower/echo → false
 *         (this is what makes the re-announce flood terminate after one round).
 */
bool SmartService_CondGroupOnPeerReport(const IOT_CoreSmartReportCondState_t *rep,
                                        uint16_t senderEid);

/**
 * @brief Notify the scoreboard that an ACTIVE_BY_TRIGGER for `smid` arrived as a location-wide
 * broadcast (some device fired the group). Cancels our own pending deferred elected fire
 * (suppression) and records the time so a near-simultaneous completion within the dedup
 * window won't double-fire. Call for every inbound ACTIVE_BY_TRIGGER.
 */
void SmartService_CondGroupOnActiveByTrigger(uint16_t smid, uint16_t senderEid);

/**
 * @brief Can this group still reach its threshold with the seats we have evidence for?
 *
 * True means a member is gone (or has never been online) and the automation cannot fire until
 * it returns or the group is re-bound. Evidence is "heard from since the entry was built",
 * never passive silence — a SYNC GET forces every live member to re-announce, so a seat quiet
 * across a probe round is genuinely absent rather than merely idle. Suppressed for a settling
 * window after the entry appears, since a fresh boot has heard nobody yet.
 *
 * OBSERVATIONAL ONLY — nothing here may influence firing. A false alarm that changed behaviour
 * would become a false FIRE, which is the failure direction this whole design avoids.
 */
bool SmartService_CondGroupCannotReachThreshold(uint16_t smid);

/**
 * @brief Sweep every group and log unreachability TRANSITIONS.
 * @return how many groups CHANGED state this sweep (0 on a quiet sweep). A broken group is
 *         logged once, not on every sweep — repeating it would bury everything else.
 */
uint16_t SmartService_CondGroupCheckReachability(void);

/**
 * @brief A peer answered our SYNC GET with "I am not a member of this group".
 *
 * Acts only on a SOLICITED reply (an ask is outstanding) and only when exactly ONE seat was
 * stale at ask time — the ex-member cannot name its old seat, so a reply is attributable
 * only when there was no ambiguity. With several dead peers this declines and the group
 * stays at the existing fail-safe behaviour until one of them returns.
 *
 * @param responderEid who answered. A device that was NEVER in the group answers just as
 *        truthfully (one holding only the bind-CMD, say), so the reply retires a seat only
 *        when the responder is the EID last heard announcing that seat.
 */
void SmartService_CondGroupOnNotMember(uint16_t smid, uint16_t responderEid);

/**
 * @brief Record that `seat` of `smid` is POSITIVELY confirmed absent from the group.
 *
 * Call ONLY on an explicit peer answer ("I am not a member of this group"). Never infer it
 * from silence: silence means the peer is offline, and retiring on silence would let a
 * rebooting device weaken a live group.
 *
 * The seat's bit is cleared and it leaves the pool, so the applied threshold becomes
 * min(K, live seats) — an AND group falls back to an AND of its survivors, while a partial
 * threshold (2-of-3) still requires the same number of conditions. Own seats are ignored.
 * Any newer cloud generation wipes this: cloud config outranks local inference.
 */
void SmartService_CondGroupRetireSeat(uint16_t smid, uint8_t seat);

/** @brief Drop the scoreboard entry for a SMID (unbind / remove-announce). */
void SmartService_CondGroupRemove(uint16_t smid);

/** @brief Clear the entire scoreboard (factory reset / tests). */
void SmartService_CondGroupReset(void);

/**
 * @brief Fire the group's action (local bind-cmds + cloud ACTIVE_BY_TRIGGER).
 * Implemented in smart.c (it owns the static cloud-report helper); called by
 * the scoreboard when this device's own flip completes the threshold.
 */
void SmartService_CondGroupFire(const IOT_SmartTrigger_t *trig,
                                const uint8_t *attrValue, uint16_t attrSize);

// ============================================================================
// Boot / cloud-reconnect re-sync
// ============================================================================

/**
 * @brief Refresh this device's own bit from a re-evaluated condition result and
 *        broadcast it — WITHOUT firing (a rebuild is not a fresh edge).
 *
 * Used by the boot re-sync walk and by CHECK_COND_STATE answers. Rebuilds the
 * scoreboard entry if needed, sets our own bit to @p result, updates
 * prevSatisfied from the resulting mask, and emits one COND_STATE broadcast.
 */
void SmartService_CondGroupResyncOwnBit(const IOT_SmartTrigger_t *trig, bool result);

/**
 * @brief Broadcast a SYNC request (GET) for a batch of SMIDs, chunked so no single
 *        message exceeds the per-message cap. Each group carries OUR OWN status too, so
 *        peers can compare and stay silent when nothing differs. No-op on empty input.
 */
/**
 * @param askForFire true ONLY for ask-before-fire, where a pending fire is gated on the
 *        answer. A reboot re-sync passes false: nothing waits on those replies, so peers
 *        that are not members stay silent instead of emitting one unicast per absent SMID.
 */
void SmartService_CondGroupSendCheck(const uint16_t *smids, uint16_t count, bool askForFire);

/**
 * @brief Answer a SYNC request — ONLY where we differ, unicast to the asker.
 *
 * The GET carries the requester's own status per group. We answer a group only when we
 * can correct it: our generation is newer, N/K disagrees at the same generation, or the
 * bit the requester recorded for US is wrong. If nothing differs we send NOTHING (this is
 * the common case and is the point — it keeps the message cost near zero).
 */
void SmartService_CondGroupAnswerSync(const IOT_CoreSmartCondStatus_t *reqGroups, uint16_t count,
                                      uint16_t requesterEid);

/** Max SMIDs per SYNC message / reports per response (keeps each message under the message size limit). */
#define COND_GROUP_CHECK_CHUNK 32

/**
 * @brief Schedule a deferred cond-group re-sync (own-bit re-announce + one
 *        batched CHECK query). Guarded against double-scheduling. Implemented in
 *        smart.c (needs condition + device-state evaluation). Call on cloud
 *        (re)connect.
 */
void SmartService_CondGroupSyncSchedule(void);

/**
 * @brief Handle a trigger enable/disable for cond-group staleness. On RE-ENABLE of a
 * cond-group trigger (whose mask went stale while disabled), schedules a re-sync to refresh
 * it. Disable, a missing trigger, or a non-cond-group smid do nothing.
 * @return true iff a re-sync was scheduled.
 */
bool SmartService_CondGroupOnEnableChange(uint16_t smid, bool enabled);

/**
 * @brief Schedule a jittered self re-announce of THIS device's own bit for one
 *        cond-group SMID. Implemented in smart.c (needs condition + device-state
 *        evaluation + the timer). Call when a config-generation bump is observed —
 *        from a higher-version peer report (OnPeerReport returned true) or from a
 *        BIND_TRIGGER carrying a cond-group. The jitter (0.1–1.5 s) spreads the
 *        flood when many members bump at once. No fire (a re-announce is not an edge).
 */
void SmartService_CondGroupScheduleReannounce(uint16_t smid);

/**
 * @brief Answer a peer's CHECK_COND_STATE query: for each listed SMID this
 *        device owns a cond-group trigger for, re-announce our own bit (jittered
 *        to avoid post-outage bursts). Own echoes are dropped. Implemented in
 *        smart.c.
 */
void SmartService_CondGroupHandleCheckQuery(const IOT_CoreSmartCondStatus_t *reqGroups,
                                            uint16_t count, uint16_t requesterEid);
#ifdef __cplusplus
}
#endif

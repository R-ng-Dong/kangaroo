/**
 * Distributed cond-group (AND/OR, subSmartType 2048) scoreboard.
 * See IOT_SmartCondGroup.h for the mechanism.
 */
#include <string.h>
#include <stdint.h>
#include "services/IOT_SmartCondGroup.h"
#include "dao/IOT_DaoSmartTriggerMgmt.h"
#include "IOT_CoreReporter.h"
#include "IOT_Core.h"      // IOT_CoreGetRootEid
#include "IOT_Osal.h"
#include "IOT_TimerMgmt.h" // deferred elected fire
#include "IOT_Log.h"

static const char *TAG = "SDK_CONDGRP";

// Peer-driven completion fires via a rank-ordered, suppressible deferred fire.
// Own-flip completion still fires immediately (fast path). When a group crosses <K -> >=K
// because a PEER's bit arrived (not our own flip), each participating device (own bit set)
// schedules a fire after BASE + STEP*rank ms, where rank = set bits below our condIndex.
// The lowest-index live device fires first; the rest hear its ACTIVE_BY_TRIGGER and cancel.
// If that device is offline it never fires, so the next rank takes over -> liveness.
#define COND_ELECT_FIRE_BASE_MS 300
#define COND_ELECT_FIRE_STEP_MS 1000
// Ignore a duplicate completion for a smid within this window (covers the convergence race).
#define COND_FIRE_DEDUP_MS 5000

// Ask-before-fire (freshness): a peer bit not re-heard within this window is "stale" and must
// be re-verified (asked) before it may complete a fire. Own bit is always fresh (local).
#define COND_FRESH_TTL_MS 300000u   // 5 minutes
// When a fire is gated by a stale peer bit, we send a SYNC GET and wait this long for fresh
// replies, then decide (fail-safe: still stale -> do NOT fire).
#define COND_ASK_TIMEOUT_MS 3000u

/* Unreachable-group detection (observational; never changes firing).
 * GRACE: after a boot the scoreboard is empty, so every group looks unreachable until peers
 * announce. A SYNC GET makes every live member re-announce, and the boot re-sync runs within
 * seconds — this window is generously longer than that, so a live-but-idle peer is never
 * mistaken for a gone one. SWEEP: cheap (popcounts over <= COND_GROUP_MAX_ENTRIES entries). */
#define COND_UNREACHABLE_GRACE_MS 600000u  /* 10 min */
#define COND_UNREACHABLE_SWEEP_MS 60000u   /* 1 min */

/* Suspicion-targeted probe. Detection is "heard since the entry was built", which cannot see
 * a device that dies AFTER we already heard it. A probe re-establishes the evidence: send a
 * SYNC GET and CLEAR the quiet seats' bitHeard. Live members re-announce within seconds
 * (HandleCheckQuery -> ResyncOwnBit pushes unconditionally); a gone one stays cleared.
 *
 * askForFire is deliberately FALSE on a probe: live members still answer (that is driven by
 * owning the group), while non-members stay silent — otherwise every probe would re-create
 * the reply storm. Only SUSPECT groups probe, at most once per period, so a healthy chatty
 * group never probes at all and the cost scales with suspicion, not with group count. */
#define COND_PROBE_SUSPECT_MS 43200000u   /* 12 h quiet before a seat is worth probing */
#define COND_PROBE_PERIOD_MS  43200000u   /* and at most one probe per group per 12 h */

typedef struct
{
    uint16_t smid;
    uint16_t eid;          // element/device EID this condition belongs to (sent in identity block)
    uint8_t mask;          // latched bits: bit i = last-known state of condIndex i (0..7)
    uint8_t condCount;     // N
    uint8_t fireCount;     // K
    uint8_t ownMask;       // bits this device owns — a device may hold SEVERAL seats
                           // of one group (two conditions on the same multi-sensor)
    uint16_t version;      // config generation this entry reflects (cloud-assigned)
    uint8_t prevSatisfied; // popcount(mask) >= K at last evaluation
    uint8_t inUse;
    uint32_t lastFireTick;    // tick we last fired OR heard a fire for this smid (0 = never)
    uint32_t firePendingTick; // nonzero = a deferred elected fire is armed (tick it was armed)
    uint32_t bitHeard[8];     // ask-before-fire: tick we last heard peer bit i (0 = never); own bit unused
    uint32_t askPendingTick;  // ask-before-fire: nonzero = a fire is waiting for a freshness re-check
    uint16_t seatEid[8];      // EID heard announcing seat i (0 = never heard). Needed to
                              // ATTRIBUTE a "not a member" reply: a device that was never in
                              // the group answers truthfully too, so the reply may only retire
                              // a seat when the responder is that seat's owner.
    uint8_t askSeatMask;      // seats that were stale when we last asked — a "not a member"
                              // reply is only actionable when exactly ONE seat was in question
    uint32_t createdTick;     // when this entry was built — starts the settling grace window
    uint32_t lastProbeTick;   // last suspicion probe (0 = never); also restarts the settling
                              // window, since a probe deliberately clears evidence
    uint8_t unreachableReported; // reported already; report on the TRANSITION, not every sweep
    uint8_t retiredMask;      // seats POSITIVELY confirmed absent (peer answered "not a member").
                              // Local inference only — never from silence — and wiped by any
                              // newer cloud generation, which is authoritative over it.
} cond_group_entry_t;

static cond_group_entry_t s_groups[COND_GROUP_MAX_ENTRIES];
static IOT_OsalMutexHandle_t s_lock;

void SmartService_CondGroupInit(void)
{
    // Create the scoreboard lock ONCE, on the single-threaded SDK-init path, before
    // the timer task and core's event delivery task can dispatch any cond-group callback.
    // Doing this lazily inside lock() would race: OnLocalResult (timer task) and
    // OnPeerState (core's event task) can first-touch concurrently and both create a
    // mutex, leaking one and losing mutual exclusion. Idempotent.
    if (s_lock == NULL)
    {
        s_lock = IOT_OsalMutexCreate();
    }
    if (s_lock == NULL)
    {
        IOT_LOGE(TAG, "failed to create cond-group scoreboard mutex");
    }
}
static void lock(void)
{
    // s_lock is created by SmartService_CondGroupInit() at startup. If it is NULL
    // here (init not run), fail safe: proceed unlocked rather than crash.
    if (s_lock) IOT_OsalMutexLock(s_lock, 1000);
}
static void unlock(void)
{
    if (s_lock) IOT_OsalMutexUnlock(s_lock);
}

static uint8_t popcount8(uint8_t v)
{
    uint8_t c = 0;
    while (v)
    {
        v &= (uint8_t)(v - 1);
        c++;
    }
    return c;
}

// Tick stamp for "a fire happened", never 0 (0 is the never-fired sentinel).
/* Convert a LONG duration to ticks without overflowing.
 *
 * IOT_OsalMsToTicks may multiply by the tick rate BEFORE dividing by 1000 — in 32 bits. At
 * 100 Hz anything beyond ~11.9 h wraps: 12 h (43,200,000 ms) would become ~250 s, so the
 * 12-hour probe would fire every few minutes and each probe would restart the settling
 * window, meaning the unreachable report would never appear at all.
 * Dividing to seconds first keeps the product in range. */
static uint32_t msToTicksLong(uint32_t ms)
{
    return IOT_OsalMsToTicks(1000u) * (ms / 1000u);
}

static uint32_t nowTick(void)
{
    uint32_t n = IOT_OsalGetTickCount();
    return n ? n : 1u;
}

/* Are these SCOREBOARD parameters internally consistent / trackable?
 * N is capped at 8 — the group bitmask is a single byte (up to 8 inputs) — and floored at 2:
 * a single seat is not a distributed group and never occupies a scoreboard entry. Applying
 * the floor here also hardens adoption: a garbled higher-version peer report can no longer
 * shrink a live group down to one seat. */
static bool paramsValid(uint8_t condIndex, uint8_t condCount, uint8_t fireCount)
{
    return condCount >= SMART_COND_GROUP_MIN_N && condCount <= SMART_COND_GROUP_MAX_N &&
           fireCount >= 1 && fireCount <= condCount &&
           condIndex < condCount;
}

/* Bits within [0..N-1] for masking off stale high bits after an N change. */
static uint8_t maskForN(uint8_t n)
{
    return (n >= SMART_COND_GROUP_MAX_N) ? 0xFF : (uint8_t)((1u << n) - 1);
}

/* Threshold actually applied, after seats confirmed absent are taken out of the pool.
 *
 * Retiring shrinks the POOL, never the requirement: K_eff = min(K, live seats). Subtracting
 * instead (K-1 per retired seat) would silently weaken the automation — a 2-of-3 losing one
 * seat would drop to firing on a SINGLE condition, which is not what "at least 2" meant.
 * min() restores an AND group to an AND of its survivors and leaves a partial threshold
 * intact. Floored at 1: a threshold of 0 would fire a group with nothing true. */
static uint8_t effectiveK(const cond_group_entry_t *e)
{
    uint8_t retired = popcount8((uint8_t)(e->retiredMask & maskForN(e->condCount)));
    uint8_t live = (e->condCount > retired) ? (uint8_t)(e->condCount - retired) : 1u;
    uint8_t k = (e->fireCount < live) ? e->fireCount : live;
    return (k >= 1u) ? k : 1u;
}

/* Seats we have positive evidence exist: our own, plus any peer we have heard from at least
 * once since this entry was built. NOT a freshness test — a peer with a stable condition may
 * legitimately stay quiet for days; what makes this sound is that a SYNC GET forces every live
 * member to re-announce, so silence across a probe round means genuinely absent. */
static uint8_t seatsEverHeard(const cond_group_entry_t *e)
{
    uint8_t seen = e->ownMask;
    for (uint8_t i = 0; i < SMART_COND_GROUP_MAX_N; i++)
    {
        if (e->bitHeard[i] != 0)
            seen |= (uint8_t)(1u << i);
    }
    return (uint8_t)(seen & maskForN(e->condCount));
}

static bool entryCannotReachK(const cond_group_entry_t *e)
{
    /* Settling window after the entry appears: bitHeard[] is empty on a fresh boot and the
     * re-sync has not run yet, so judging now would flag every group on every boot. */
    /* Settling window after the entry appears OR after a probe cleared evidence: judging
     * before live members have had a chance to answer would flag healthy groups. */
    uint32_t settledFrom = (e->lastProbeTick != 0 &&
                            (uint32_t)(e->lastProbeTick - e->createdTick) < 0x80000000u)
                               ? e->lastProbeTick
                               : e->createdTick;
    if ((uint32_t)(nowTick() - settledFrom) < msToTicksLong(COND_UNREACHABLE_GRACE_MS))
        return false;
    return popcount8(seatsEverHeard(e)) < effectiveK(e);
}

static void armReachabilitySweep(void);   /* defined with the sweep, below */

static cond_group_entry_t *findEntry(uint16_t smid)
{
    for (int i = 0; i < COND_GROUP_MAX_ENTRIES; i++)
    {
        if (s_groups[i].inUse && s_groups[i].smid == smid)
            return &s_groups[i];
    }
    return NULL;
}

/* @param ownSeats  bitmask of the seats this device holds, probed by the CALLER outside the
 *                   scoreboard lock (a probe is up to N cache/NVS reads, and this lock is
 *                   contended by the timer and core event tasks — never hold it across flash
 *                   I/O). 0 means "unknown": fall back to just the seat being reported. */
static cond_group_entry_t *createEntry(uint16_t smid, uint16_t eid, uint8_t condIndex,
                                       uint8_t condCount, uint8_t fireCount, uint16_t version,
                                       uint8_t ownSeats)
{
    if (!paramsValid(condIndex, condCount, fireCount))
        return NULL;

    for (int i = 0; i < COND_GROUP_MAX_ENTRIES; i++)
    {
        if (!s_groups[i].inUse)
        {
            s_groups[i].inUse = 1;
            s_groups[i].smid = smid;
            s_groups[i].eid = eid;
            s_groups[i].mask = 0;
            s_groups[i].condCount = condCount;
            s_groups[i].fireCount = fireCount;
            /* Seat mask is supplied by the caller, which probed the DAO OUTSIDE this
             * lock — see the note on createEntry(). */
            s_groups[i].ownMask = (ownSeats != 0) ? ownSeats : (uint8_t)(1u << condIndex);
            s_groups[i].version = version;
            s_groups[i].prevSatisfied = 0;
            s_groups[i].createdTick = nowTick();
            s_groups[i].lastProbeTick = 0;
            armReachabilitySweep();
            s_groups[i].unreachableReported = 0;
            return &s_groups[i];
        }
    }
    IOT_LOGW(TAG, "scoreboard full (%d entries), dropping SMID=%04x", COND_GROUP_MAX_ENTRIES, smid);
    return NULL;
}

/* Everything the scoreboard needs to build an entry, read from the DAO. Gathered by
 * seatsAndTriggerFromDao() BEFORE taking the lock so no flash I/O happens under it. */
typedef struct
{
    IOT_SmartTrigger_t trig;
    uint8_t ownSeats;
    bool valid;
    /* No trigger AT ALL for this smid — distinct from valid==false, which is also the case
     * when we DO hold a trigger that simply is not a cond-group (e.g. a stale N=1 first
     * member). Answering "not a member" for that would retire a device that is genuinely
     * participating, so only this flag may drive the reply. */
    bool absent;
    /* `trig` holds a record we actually read. A storage error is neither "absent" nor
     * "valid", and anything that reads `trig` outside the valid case (promotion does) would
     * otherwise be reading whatever the failed DAO call left behind. */
    bool held;
} cond_group_seed_t;

/* MUST be called with the scoreboard lock NOT held. */
static cond_group_seed_t seedFromDao(uint16_t smid)
{
    /* Zeroed, not just field-by-field: a failed Get may leave `trig` untouched, and every
     * reader of a non-valid seed would then be reading stack garbage. */
    cond_group_seed_t seed;
    memset(&seed, 0, sizeof(seed));
    iot_err_t got = IOT_DaoSmartTriggerGet(smid, &seed.trig);
    if (got != IOT_OK)
    {
        /* Only a positive "not found" proves absence. Any other failure (storage error)
         * must stay silent — silence is the safe answer. */
        memset(&seed.trig, 0, sizeof(seed.trig));
        seed.absent = (got == IOT_ERR_NOT_FOUND);
        return seed;
    }
    seed.held = true;
    if (!SmartTriggerIsCondGroup(&seed.trig))
        return seed;
    (void) IOT_DaoSmartTriggerGetSeatMask(smid, &seed.ownSeats);
    seed.valid = true;
    return seed;
}

/* Self-heal for the group we were never re-bound into.
 *
 * The FIRST member's bind may arrive alone, as N=1, and is not re-sent when the group
 * grows. N=1 is not a cond-group, so that device
 * builds no scoreboard entry, drops every peer report, and fires the automation single-handed
 * the moment its own condition is true — a 3-of-3 firing on 1.
 *
 * A peer's cond-report at a NEWER generation naming N>=2 seats is proof this SMID has
 * become a group. That is enough to join: take the seat our own trigger already records,
 * adopt the announced N/K, and PERSIST it — the announce path (condResyncAnnounce) re-reads
 * the DAO and skips anything that is not a cond-group, so a RAM-only promotion would stay
 * mute, and it would be forgotten on the next boot.
 *
 * Refused when we hold no trigger for the SMID (never invent membership from a peer's word),
 * when the report is not newer (a replayed frame must not mute a live single-condition
 * automation), when our recorded seat does not exist in the new group, and for sync-on-off.
 *
 * MUST be called with the scoreboard lock NOT held — it writes the DAO.
 * Returns true when the seed was promoted; `seed` then describes a full member. */
static bool promoteStaleSingleSeat(uint16_t smid, const IOT_CoreSmartReportCondState_t *rep,
                                   cond_group_seed_t *seed)
{
    if (seed == NULL || rep == NULL || seed->valid || seed->absent)
        return false; /* already a member, or none of our business */
    if (!seed->held)
        return false; /* the DAO read failed — `trig` describes nothing, never act on it */
    if (seed->trig.subSmartType == SMART_SUB_TYPE_SYNC_ON_OFF)
        return false;
    if (seed->trig.enabled == 0)
        return false; /* every other path skips disabled triggers; it could never announce */
    if (rep->version <= seed->trig.condVersion)
        return false;
    if (!paramsValid(seed->trig.condIndex, rep->condCount, rep->fireCount))
        return false;

    seed->trig.condCount = rep->condCount;
    seed->trig.fireCount = rep->fireCount;
    seed->trig.condVersion = rep->version;
    if (IOT_DaoSmartTriggerSet(smid, &seed->trig) != IOT_OK)
    {
        /* Could not persist. Joining in RAM only would go mute on the next announce and
         * forget itself on reboot, so stay out and try again on the next report. */
        IOT_LOGW(TAG, "SMID=%04x: cannot persist promotion, staying out of the group", smid);
        return false;
    }

    /* Claim EVERY seat this device holds, not just the one named on the stale record — two
     * of our elements can be conditions of the same automation. Probe AFTER the write, never
     * before: IOT_DaoSmartTriggerGetSeatMask bounds its walk by the STORED condCount, so
     * while the record still said N=1 it could only ever return seat 0.
     *
     * A seat we own but fail to claim is not a cosmetic loss — `ownMask` is what marks a seat
     * as ours and beyond doubt, so an unclaimed one is treated as an absent peer: it makes a
     * healthy group look unable to reach K, and lets a not-member reply retire it and quietly
     * lower the threshold. */
    if (IOT_DaoSmartTriggerGetSeatMask(smid, &seed->ownSeats) != IOT_OK || seed->ownSeats == 0)
        seed->ownSeats = (uint8_t)(1u << seed->trig.condIndex);

    /* The DAO keys a trigger by (smid, condIndex), so each of our seats is a separate record,
     * and N/K/version describe the AUTOMATION rather than any one seat — the same reasoning
     * that makes IOT_DaoSmartTriggerSetEnabled update every seat. Leaving a sibling record at
     * N=1 would let it build a scoreboard entry that fails validation, silently dropping that
     * seat's bit. A failure here is not fatal: the seat we read is already stored, so the
     * device is in the group and the straggler is repaired on the next generation. */
    for (uint8_t seat = 0; seat < SMART_COND_GROUP_MAX_N; seat++)
    {
        if ((seed->ownSeats & (uint8_t)(1u << seat)) == 0 || seat == seed->trig.condIndex)
            continue;
        IOT_SmartTrigger_t sibling;
        if (IOT_DaoSmartTriggerGetSeat(smid, seat, &sibling) != IOT_OK)
            continue;
        sibling.condCount = rep->condCount;
        sibling.fireCount = rep->fireCount;
        sibling.condVersion = rep->version;
        if (IOT_DaoSmartTriggerSet(smid, &sibling) != IOT_OK)
            IOT_LOGW(TAG, "SMID=%04x: seat %u record not updated on promotion", smid, seat);
    }

    seed->valid = true;
    IOT_LOGI(TAG, "SMID=%04x: promoted from a stale single-condition bind — joining as seat "
                  "%u of %u (K=%u, v=%u)",
             smid, seed->trig.condIndex, rep->condCount, rep->fireCount, rep->version);
    return true;
}

/* The other side of a departure: the group has dissolved down to conditions we alone hold.
 *
 * A peer may never shrink a LIVE group to a single seat — the ordinary merge guards that,
 * because a garbled or replayed frame would otherwise make a device fire
 * on its own. The safe discriminator is ownership: when the announced N equals the number of
 * seats WE hold, there is no other member left to be wrong about, so the automation really has
 * become our own plain trigger. That cannot be forged by a peer claiming a small N while other
 * members still exist.
 *
 * Persisting is the point. Retiring the departed seat in RAM would fire correctly until the
 * next reboot and then quietly die again — the worst kind of repair. Writing the smaller shape
 * to every seat we own makes it an ordinary single-condition automation for good, which is
 * exactly the shape the departure notice announced.
 *
 * MUST be called with the scoreboard lock NOT held — it writes the DAO.
 * Returns true when the group was dissolved; the caller then drops the scoreboard entry. */
static bool dissolveOnDepartureNotice(uint16_t smid, const IOT_CoreSmartReportCondState_t *rep,
                                      const cond_group_seed_t *seed)
{
    if (seed == NULL || rep == NULL || !seed->valid)
        return false;
    if (rep->condCount < 1 || rep->condCount >= SMART_COND_GROUP_MIN_N)
        return false; /* still a group — the ordinary merge handles it */
    if (rep->fireCount < 1 || rep->fireCount > rep->condCount)
        return false;
    if (rep->version <= seed->trig.condVersion)
        return false; /* a replayed farewell must not dissolve a live group */
    if (popcount8(seed->ownSeats) != rep->condCount)
        return false; /* seats we do not own remain — this is the dangerous shrink */

    /* Deliberately NOT requiring our seats to fit inside the smaller N. Seats are not
     * renumbered on removal — a survivor may keep condIndex=1 while being told
     * the group is now N=1 — so that check would refuse every case where the last member is
     * not seat 0, i.e. the automation would die precisely when the OTHER device was the one
     * removed. Owning every remaining condition is what makes this safe; where the index sits
     * does not. The record stops being a group, so condIndex is inert for firing, and leaving
     * it alone keeps the stored numbering intact if the group is ever grown again. */

    for (uint8_t seat = 0; seat < SMART_COND_GROUP_MAX_N; seat++)
    {
        if ((seed->ownSeats & (uint8_t)(1u << seat)) == 0)
            continue;
        IOT_SmartTrigger_t own;
        if (IOT_DaoSmartTriggerGetSeat(smid, seat, &own) != IOT_OK)
            continue;
        own.condCount = rep->condCount;
        own.fireCount = rep->fireCount;
        own.condVersion = rep->version;
        if (IOT_DaoSmartTriggerSet(smid, &own) != IOT_OK)
            IOT_LOGW(TAG, "SMID=%04x: seat %u record not updated on dissolution", smid, seat);
    }

    IOT_LOGI(TAG, "SMID=%04x: group dissolved — last condition(s) are ours, now a plain "
                  "trigger (N=%u K=%u v=%u)",
             smid, rep->condCount, rep->fireCount, rep->version);
    return true;
}

/* Persist a new threshold (and generation) to every seat this device owns, leaving N alone.
 * MUST be called with the scoreboard lock NOT held. */
static void persistThreshold(uint16_t smid, uint8_t ownSeats, uint8_t fireCount, uint16_t version)
{
    for (uint8_t seat = 0; seat < SMART_COND_GROUP_MAX_N; seat++)
    {
        if ((ownSeats & (uint8_t)(1u << seat)) == 0)
            continue;
        IOT_SmartTrigger_t own;
        if (IOT_DaoSmartTriggerGetSeat(smid, seat, &own) != IOT_OK)
            continue;
        own.fireCount = fireCount;
        own.condVersion = version;
        if (IOT_DaoSmartTriggerSet(smid, &own) != IOT_OK)
            IOT_LOGW(TAG, "SMID=%04x: seat %u threshold not persisted", smid, seat);
    }
}

/* A member is leaving the group: retire its seat, keep our own numbering.
 *
 * Adopting the announced N cannot work, because seats are NOT renumbered on a removal. Take
 * away the middle seat of a 3-group and the survivors still sit at seats 0 and 2 while the
 * announced N is 2: the seat-2 device would find its own index outside the new N and refuse
 * the whole update, and the seat-0 device would clip seat 2's bit off its mask as if that
 * member had vanished. Only removing the HIGHEST seat keeps N and the indices consistent.
 *
 * Retiring the named seat sidesteps the numbering entirely and works wherever the seat sits:
 * `effectiveK = min(K, live)` lowers the threshold, and every surviving bit keeps its meaning.
 *
 * The threshold is PERSISTED. Retiring in RAM alone fires correctly until the next reboot and
 * then dies, because we re-seed N and K from our own stored record and the device that left is
 * never coming back to re-announce. N is deliberately left as it was — our own seat index must
 * keep fitting inside it.
 *
 * MUST be called with the scoreboard lock NOT held (it writes the DAO). */
static bool retireDepartedSeat(uint16_t smid, const IOT_CoreSmartReportCondState_t *rep,
                               const cond_group_seed_t *seed)
{
    if (seed == NULL || rep == NULL || !seed->valid || !rep->departing)
        return false;
    if (rep->version <= seed->trig.condVersion)
        return false; /* a replayed farewell must not disarm a live seat */
    if (rep->condIndex >= SMART_COND_GROUP_MAX_N)
        return false;

    uint8_t bit = (uint8_t)(1u << rep->condIndex);
    if (seed->ownSeats & bit)
        return false; /* never our own seat — we know for certain we are still a member */

    bool changed = false;
    lock();
    cond_group_entry_t *e = findEntry(smid);
    if (e != NULL && rep->condIndex < e->condCount)
    {
        e->retiredMask |= bit;
        e->mask &= (uint8_t) ~bit;
        e->bitHeard[rep->condIndex] = 0;
        if (rep->fireCount >= 1 && rep->fireCount <= e->condCount)
            e->fireCount = rep->fireCount;
        e->version = rep->version;
        /* Bookkeeping only — never fire from here; the next real evaluation decides. */
        e->prevSatisfied = popcount8(e->mask) >= effectiveK(e) ? 1 : 0;
        changed = true;
    }
    unlock();

    if (!changed)
        return false;

    if (rep->fireCount >= 1 && rep->fireCount <= seed->trig.condCount)
        persistThreshold(smid, seed->ownSeats, rep->fireCount, rep->version);

    IOT_LOGI(TAG, "SMID=%04x: seat %u left the group — retired, threshold now K=%u (v=%u)",
             smid, rep->condIndex, rep->fireCount, rep->version);
    return true;
}

/* Get the entry for smid, lazily building it from a pre-read DAO seed. Call with the lock
 * HELD; the seed must have been gathered without it. */
static cond_group_entry_t *getOrBuild(uint16_t smid, const cond_group_seed_t *seed)
{
    cond_group_entry_t *e = findEntry(smid);
    if (e != NULL)
        return e;
    if (seed == NULL || !seed->valid)
        return NULL;
    return createEntry(smid, seed->trig.eid, seed->trig.condIndex, seed->trig.condCount,
                       seed->trig.fireCount, seed->trig.condVersion, seed->ownSeats);
}

/* Fill an outgoing report from an entry (own bit read from the entry's mask). */
static void fillReportForSeat(const cond_group_entry_t *e, uint8_t seat,
                              IOT_CoreSmartReportCondState_t *r)
{
    r->smid = e->smid;
    r->version = e->version;
    r->condCount = e->condCount;
    r->fireCount = e->fireCount;
    r->condIndex = seat;
    r->mask = e->mask; // our bit for THIS seat is bit[seat]; peers read only that bit
    r->eid = e->eid;
    /* Explicit, not incidental: this is the funnel for ORDINARY announcements, and a stray
     * DEPARTING bit makes every peer retire this seat — silently dropping a live member from
     * the group. The callers' structs are stack locals, so a field left unset here is whatever
     * happened to be in memory. */
    r->departing = 0;
}

uint8_t SmartService_CondGroupOwnSeats(uint16_t smid)
{
    cond_group_seed_t seed = seedFromDao(smid); /* DAO reads BEFORE the lock */
    lock();
    cond_group_entry_t *e = getOrBuild(smid, &seed);
    uint8_t seats = (e != NULL) ? e->ownMask : 0;
    unlock();
    return seats;
}

bool SmartCondGroupBindIsValid(uint8_t hasCondGroup,
                               uint8_t condIndex, uint8_t condCount, uint8_t fireCount)
{
    /* No subSmartType parameter on purpose: it is NOT the group marker. Every automation's
     * bind has the same shape and the condition-group description decides. */

    // No condition-group description: ordinary trigger, nothing to validate.
    if (!hasCondGroup)
        return true;

    // Description present but fewer than 2 seats: not a distributed group. Runs as an ordinary
    // single-seat trigger, where K and condIndex are ignored — so never reject the whole
    // automation over metadata the device does not use (the caller warns on condCount==0).
    if (condCount < SMART_COND_GROUP_MIN_N)
        return true;

    return paramsValid(condIndex, condCount, fireCount);
}

/* Bits whose value we can still vouch for: our OWN seats (evaluated locally, so always
 * verified regardless of age) plus peer seats heard within the freshness TTL. Anything else
 * is unverified membership — a bit latched from a device that may since have been removed
 * from the group, or died. */
static uint8_t verifiedBits(const cond_group_entry_t *e)
{
    uint32_t now = nowTick();
    uint32_t ttl = IOT_OsalMsToTicks(COND_FRESH_TTL_MS);
    uint8_t v = e->ownMask;

    for (uint8_t i = 0; i < SMART_COND_GROUP_MAX_N; i++)
    {
        uint8_t bit = (uint8_t)(1u << i);
        if (e->ownMask & bit)
            continue; /* own seat: already counted, never ages out */
        if (e->bitHeard[i] != 0 && (uint32_t)(now - e->bitHeard[i]) < ttl)
            v |= bit;
    }
    return v;
}

/* Seats that are SET in our mask but no longer verified — exactly the seats whose truth we
 * are asking peers to confirm. Recorded at ask time so a later "not a member" reply can be
 * attributed to a seat without carrying a per-seat EID table. */
static uint8_t staleSetBits(const cond_group_entry_t *e)
{
    return (uint8_t)(e->mask & maskForN(e->condCount) & ~verifiedBits(e));
}

// Lowest seat this device owns. Used where one representative index is needed (fire
// election rank, SYNC-difference check) — a multi-seat device speaks for its lowest seat.
static uint8_t lowestOwnedSeat(const cond_group_entry_t *e)
{
    for (uint8_t i = 0; i < SMART_COND_GROUP_MAX_N; i++)
        if (e->ownMask & (uint8_t)(1u << i))
            return i;
    return 0;
}

// Rank of our own bit among currently-set bits: how many set bits sit below our seat.
// Lower rank -> shorter deferred-fire delay -> we fire earlier among live participants.
static uint8_t rankOf(uint8_t mask, uint8_t ownIndex)
{
    uint8_t below = (ownIndex >= SMART_COND_GROUP_MAX_N) ? 0xFF
                                                        : (uint8_t)((1u << ownIndex) - 1);
    return popcount8((uint8_t)(mask & below));
}

// Ask-before-fire gate: do the FRESH set bits alone reach K? We count set bits that are fresh
// (our own bit is always fresh — evaluated locally; a peer bit is fresh if heard < TTL ago). If
// that count reaches K, the group is satisfied by confirmed-fresh conditions → fire, no ask.
// If it does NOT, some STALE bit is load-bearing → we must ask to verify it. This deliberately
// IGNORES a stale bit that isn't needed: e.g. 2-of-3 where two live devices are fresh fires even
// if the third is dead/stale (its bit isn't required) — no false-negative missed fire.
static bool freshBitsReachThreshold(const cond_group_entry_t *e)
{
    uint32_t now = nowTick();
    uint32_t ttl = IOT_OsalMsToTicks(COND_FRESH_TTL_MS); // IOT_OsalGetTickCount() is in TICKS
    uint8_t fresh = 0;
    for (uint8_t i = 0; i < e->condCount && i < SMART_COND_GROUP_MAX_N; i++)
    {
        if (!((e->mask >> i) & 1u))
            continue; // bit not set
        if (e->ownMask & (uint8_t)(1u << i))
            fresh++; // own bit always fresh
        else if (e->bitHeard[i] != 0 && (uint32_t)(now - e->bitHeard[i]) < ttl)
            fresh++; // fresh peer bit
    }
    return fresh >= effectiveK(e);
}

// Fire a group's action (outside the lock): reconstruct the trigger and fire with its stored
// condition value — ACTIVE_BY_TRIGGER needs a non-empty attr value (the action runs from
// stored bind-cmds regardless).
static void condFireBySmid(uint16_t smid)
{
    IOT_SmartTrigger_t trig;
    if (IOT_DaoSmartTriggerGet(smid, &trig) == IOT_OK)
        SmartService_CondGroupFire(&trig, trig.attrValue, trig.attrSize);
}

// Ask-before-fire timeout: a gated fire has waited COND_ASK_TIMEOUT_MS for fresh replies. Fire
// iff peers are now fresh AND the group is still satisfied; otherwise fail-safe (drop).
static void condAskTimeoutCb(void *userData)
{
    uint16_t smid = (uint16_t)(uintptr_t) userData;
    bool doFire = false;
    lock();
    cond_group_entry_t *e = findEntry(smid);
    if (e != NULL && e->askPendingTick != 0)
    {
        if (freshBitsReachThreshold(e)) /* implies popcount>=K */
        {
            e->lastFireTick = nowTick();
            doFire = true;
        }
        e->askPendingTick = 0; // consume either way (fail-safe if still stale)
    }
    unlock();
    if (doFire)
        condFireBySmid(smid);
}

// Ask-before-fire: send a SYNC GET to refresh stale peer bits, then arm the decision timeout.
static void condAskPeersAndArmTimeout(uint16_t smid)
{
    SmartService_CondGroupSendCheck(&smid, 1, true); /* a fire is gated on the answer */
    IOT_TimerInsertCb(condAskTimeoutCb, (void *)(uintptr_t) smid, COND_ASK_TIMEOUT_MS);
}

// Deferred elected fire. Fires only if the group is still satisfied AND our armed
// state was not cancelled by a heard ACTIVE_BY_TRIGGER (suppression) or an own flip. The
// fire is additionally gated by peer freshness (ask-before-fire): stale → ask, don't fire yet.
static void condElectedFireCb(void *userData)
{
    uint16_t smid = (uint16_t)(uintptr_t) userData;
    bool doFire = false, doAsk = false;

    lock();
    cond_group_entry_t *e = findEntry(smid);
    if (e != NULL && e->firePendingTick != 0)
    {
        e->firePendingTick = 0; // consume the armed state either way
        if (popcount8(e->mask) >= effectiveK(e))
        {
            if (freshBitsReachThreshold(e))
            {
                e->lastFireTick = nowTick();
                e->askPendingTick = 0;
                doFire = true;
            }
            else
            {
                e->askPendingTick = nowTick();
                e->askSeatMask = staleSetBits(e);
                doAsk = true;
            }
        }
    }
    unlock();

    if (doFire)
        condFireBySmid(smid);
    else if (doAsk)
        condAskPeersAndArmTimeout(smid);
}

void SmartService_CondGroupOnLocalResult(const IOT_SmartTrigger_t *trig, bool result,
                                         const uint8_t *attrValue, uint16_t attrSize)
{
    if (trig == NULL)
        return;

    bool fire = false;
    bool ask = false;

    IOT_CoreSmartReportCondState_t report = {0};
    bool haveReport = false;

    /* Seat mask comes from the DAO, so read it BEFORE taking the lock — the scoreboard
     * mutex is contended by the timer and core event tasks and must never span flash I/O. */
    uint8_t ownSeats = 0;
    (void) IOT_DaoSmartTriggerGetSeatMask(trig->smid, &ownSeats);

    lock();
    cond_group_entry_t *e = findEntry(trig->smid);
    if (e == NULL)
        e = createEntry(trig->smid, trig->eid, trig->condIndex, trig->condCount,
                        trig->fireCount, trig->condVersion, ownSeats);

    if (e != NULL && trig->condIndex < e->condCount && trig->condIndex < SMART_COND_GROUP_MAX_N)
    {
        /* The bit belongs to THIS seat, not to "the" seat: a device may hold several
         * conditions of one group, so claim the seat on the entry as we go. */
        uint8_t bit = (uint8_t)(1u << trig->condIndex);
        e->ownMask |= bit;

        uint8_t oldBit = (e->mask & bit) ? 1 : 0;
        uint8_t newBit = result ? 1 : 0;

        if (oldBit != newBit)
        {
            if (newBit)
                e->mask |= bit;
            else
                e->mask &= (uint8_t)~bit;

            /* Announce this seat's bit (plus the full mask + version). */
            fillReportForSeat(e, trig->condIndex, &report);
            haveReport = true;

            bool sat = popcount8(e->mask) >= effectiveK(e);
            if (sat && !e->prevSatisfied)
            {
                e->prevSatisfied = 1;
                e->firePendingTick = 0; // our own flip supersedes any pending elected fire
                // Ask-before-fire gate: fire now only if all contributing PEER bits are fresh;
                // if any is stale, ask (SYNC GET) and decide at the timeout instead.
                if (freshBitsReachThreshold(e))
                {
                    e->lastFireTick = nowTick(); // suppresses peers
                    e->askPendingTick = 0;
                    fire = true;
                }
                else
                {
                    e->askPendingTick = nowTick();
                    e->askSeatMask = staleSetBits(e);
                    ask = true;
                }
            }
            else if (!sat)
            {
                e->prevSatisfied = 0; // re-arm
            }
        }
    }
    unlock();

    /* Send + fire outside the lock (the exec path may re-enter smart services). */
    if (haveReport)
        IOT_CoreRequestSmartReportCondState(&report);
    if (fire)
        SmartService_CondGroupFire(trig, attrValue, attrSize);
    else if (ask)
        condAskPeersAndArmTimeout(trig->smid);
}

/* Take ONLY the sender's own bit, read out of its mask at its condIndex. Every other bit in
 * the sender's mask is ignored — a device is authoritative for its own condition only. */
static void applySenderBit(cond_group_entry_t *e, const IOT_CoreSmartReportCondState_t *rep,
                           uint16_t senderEid)
{
    uint8_t senderIdx = rep->condIndex;
    if (senderIdx >= e->condCount || senderIdx >= SMART_COND_GROUP_MAX_N ||
        (e->ownMask & (uint8_t)(1u << senderIdx)))
        return;
    // Ask-before-fire: hearing from the sender refreshes this bit's freshness.
    e->bitHeard[senderIdx] = nowTick();
    /* Remember the sender's ROOT eid for this seat — that is what a "not a member" reply
     * carries, and what the reply must be matched against. rep->eid is the condition's
     * element eid, which is not the same thing. */
    if (senderEid != 0)
        e->seatEid[senderIdx] = senderEid;
    uint8_t bit = (uint8_t)(1u << senderIdx);
    if ((rep->mask >> senderIdx) & 1u)
        e->mask |= bit;
    else
        e->mask &= (uint8_t)~bit;
}

/* Merge a peer's cond-report via the version rule. Never fires (peer-driven).
 * A device is authoritative ONLY for its own bit — we never adopt a peer's whole mask.
 *   version > mine  -> config newer: adopt version + N/K, clip my mask to the new N,
 *                      then update only the sender's bit. Returns true (a bump).
 *   version == mine -> update only the sender's bit. Returns false.
 *   version < mine  -> ignore. Returns false.
 *
 * Adopting N/K on a higher version + clipping to the new N is what lets a condition add/remove
 * work when the bind reaches only the new/changed device: its higher-version report carries
 * the new N/K, and every member re-floods its own bit (see the true return → schedule a
 * jittered self re-announce), so the full mask converges without every member being re-bound. */
bool SmartService_CondGroupOnPeerReport(const IOT_CoreSmartReportCondState_t *rep, uint16_t senderEid)
{
    if (rep == NULL)
        return false;

    /* Drop our own echo. */
    uint16_t ownEid = 0;
    if (IOT_CoreGetRootEid(&ownEid) == IOT_OK && senderEid != 0 && senderEid == ownEid)
        return false;

    bool versionBumped = false;
    bool schedule = false;
    bool doFire = false;
    uint32_t delay = 0;

    cond_group_seed_t seed = seedFromDao(rep->smid); /* DAO reads BEFORE the lock */
    /* A stale N=1 first member joins here, before the entry is built (writes the DAO, so it
     * must stay outside the lock). Counts as a generation change: the caller re-announces,
     * which is how peers learn the bit of a seat that has been silent until now. */
    bool promoted = promoteStaleSingleSeat(rep->smid, rep, &seed);

    /* The mirror image: the group has shrunk to conditions we alone hold, so it stops being a
     * group at all. Handled before the entry is touched — it writes the DAO, and the entry it
     * would build is one we are about to drop. */
    if (dissolveOnDepartureNotice(rep->smid, rep, &seed))
    {
        SmartService_CondGroupRemove(rep->smid);
        return true; /* a generation change: the caller re-announces (a no-op once dissolved) */
    }

    /* Still a group, but one seat is vacating it. Retire that seat rather than adopting the
     * announced N — seats are not renumbered, so the survivors' indices need not fit inside it. */
    if (retireDepartedSeat(rep->smid, rep, &seed))
        return true;

    lock();
    cond_group_entry_t *e = getOrBuild(rep->smid, &seed);
    if (e != NULL)
    {
        uint8_t wasSat = e->prevSatisfied;
        if (rep->version > e->version)
        {
            /* Newer config generation. Adopt N/K from the report if valid for our own
             * index (handles a bind that reached only the new/changed trigger). */
            e->version = rep->version;
            /* Cloud config supersedes anything we inferred locally. */
            e->retiredMask = 0;
            if (paramsValid(lowestOwnedSeat(e), rep->condCount, rep->fireCount))
            {
                e->condCount = rep->condCount;
                e->fireCount = rep->fireCount;
            }
            /* Clip OUR mask to the new N — drops bits of members that no longer exist
             * (shrink/remove) — AND drop bits we can no longer vouch for. We do NOT adopt
             * the peer's mask: only the sender's own bit is trusted, exactly as in the
             * equal-version case. Our own bits survive both (own seats are < condCount and
             * always verified).
             *
             * The verified filter matters when a new generation keeps N and lowers K — the safe
             * rule when the departed seat is not the highest, since shrinking N below a
             * survivor's own seat is refused by paramsValid below. Clipping alone only
             * removes bits ABOVE N, so a departed member's latched-true bit would survive,
             * inflate popcount, latch prevSatisfied, and eat the next real edge: the group
             * would go silent. A config generation change is exactly the point at which
             * unverified membership must be forgotten. A peer that is merely slow re-announces
             * and sets its bit again within one TTL, so the cost is at worst a delayed fire,
             * never a false one. */
            e->mask &= (uint8_t)(maskForN(e->condCount) & verifiedBits(e));
            applySenderBit(e, rep, senderEid);
            e->prevSatisfied = popcount8(e->mask) >= effectiveK(e) ? 1 : 0;
            versionBumped = true;
        }
        else if (rep->version == e->version)
        {
            /* Same generation: trust only the sender for its OWN bit. */
            applySenderBit(e, rep, senderEid);
            e->prevSatisfied = popcount8(e->mask) >= effectiveK(e) ? 1 : 0;
        }
        /* version < mine: ignore (our view is newer). */

        /* A completion that arrives via a PEER's bit (no local flip of ours) elects a
         * rank-ordered, suppressible deferred fire, so exactly one device still fires when
         * the group is completed remotely (the power-event hole). Only participating devices
         * (our own bit set) are candidates, and not if we just fired (dedup window). */
        bool nowSat = (e->prevSatisfied != 0);
        bool ownBitSet = (e->mask & e->ownMask) != 0;
        uint32_t now = IOT_OsalGetTickCount();
        bool recentlyFired = (e->lastFireTick != 0) &&
                             ((uint32_t)(now - e->lastFireTick) < IOT_OsalMsToTicks(COND_FIRE_DEDUP_MS));

        /* Early-fire: if we are already waiting on an ask (own flip completed on a stale bit)
         * and this answer just made the needed bits all FRESH, fire NOW instead of waiting out
         * COND_ASK_TIMEOUT_MS — the decision cannot change (fresh bits don't decay in 3 s), so
         * the timeout would reach the same verdict later. Clearing askPendingTick makes the
         * armed ask-timeout a no-op, so no double fire. This keeps the ask cheap when peers are
         * alive; the timeout only governs the offline fail-safe. */
        if (e->askPendingTick != 0 && freshBitsReachThreshold(e))
        {
            e->askPendingTick = 0;
            e->lastFireTick = nowTick(); // suppresses peers + dedup window
            doFire = true;
        }
        /* A completion that arrives via a PEER's bit (no local flip of ours) elects a
         * rank-ordered, suppressible deferred fire (only when we are NOT already firing). */
        else if (!wasSat && nowSat && ownBitSet && !recentlyFired)
        {
            e->firePendingTick = nowTick(); // arm (nonzero marker)
            delay = (uint32_t) COND_ELECT_FIRE_BASE_MS +
                    (uint32_t) rankOf(e->mask, lowestOwnedSeat(e)) * COND_ELECT_FIRE_STEP_MS;
            schedule = true;
        }
    }
    unlock();

    // Schedule / fire OUTSIDE the scoreboard lock (avoids scoreboard->timer lock nesting; the
    // timer cb and the fire path take the scoreboard lock, so we must not hold it here).
    if (schedule)
        IOT_TimerInsertCb(condElectedFireCb, (void *)(uintptr_t) rep->smid, delay);
    if (doFire)
        condFireBySmid(rep->smid);

    return versionBumped || promoted;
}

// Suppression + dedup: we heard an ACTIVE_BY_TRIGGER for `smid` as a location-wide
// broadcast (some device fired the group). Cancel our own pending deferred fire and record
// the time, so a near-simultaneous completion within the dedup window won't re-fire.
void SmartService_CondGroupOnActiveByTrigger(uint16_t smid, uint16_t senderEid)
{
    (void) senderEid; // any fire — own echo or a peer's — cancels our pending one
    lock();
    cond_group_entry_t *e = findEntry(smid);
    if (e != NULL)
    {
        e->lastFireTick = nowTick();
        e->firePendingTick = 0; // cancel a pending elected fire
        e->askPendingTick = 0;  // cancel a pending ask-gated fire (someone already fired the group)
    }
    unlock();
}

void SmartService_CondGroupAnnounceRemoval(uint16_t smid, uint16_t version,
                                           uint8_t condCount, uint8_t fireCount)
{
    /* N==1 is announced too, and deliberately: it is the DISSOLUTION notice. Removing the
     * second-to-last condition leaves one member holding a group of one, and it is the only
     * party that will ever be told — only the leaving device receives the unbind. The
     * receiver does not adopt N=1 blindly (a peer may never shrink a live group to one seat);
     * it dissolves only when every remaining seat is its own. Anything not describable as a
     * shape at all stays unsent — peers act on any newer generation, so a bogus one is worse
     * than silence. */
    if (condCount < 1 || condCount > SMART_COND_GROUP_MAX_N ||
        fireCount < 1 || fireCount > condCount)
        return;

    /* The report speaks from our own seat, so we must still hold the trigger — this runs
     * before the unbind deletes it. Deliberately NOT validated against the new N: the seat
     * being removed is usually the one that no longer exists in it, and that is exactly the
     * bit the survivors should clip away when they adopt the smaller group. */
    IOT_SmartTrigger_t trig;
    if (IOT_DaoSmartTriggerGet(smid, &trig) != IOT_OK)
        return;

    IOT_CoreSmartReportCondState_t rep = {
        .smid = smid,
        .version = version,
        .condCount = condCount,
        .fireCount = fireCount,
        .condIndex = trig.condIndex,
        .mask = 0, /* we contribute nothing on the way out */
        .eid = trig.eid,
        .departing = 1, /* "seat condIndex is leaving" — cannot be inferred from a smaller N */
    };

    IOT_LOGI(TAG, "SMID=%04x: leaving the group — announcing the new shape N=%u K=%u v=%u",
             smid, condCount, fireCount, version);
    (void) IOT_CoreRequestSmartReportCondState(&rep);
}

void SmartService_CondGroupRemove(uint16_t smid)
{
    lock();
    cond_group_entry_t *e = findEntry(smid);
    if (e != NULL)
        memset(e, 0, sizeof(*e));
    unlock();
}

void SmartService_CondGroupOnNotMember(uint16_t smid, uint16_t responderEid)
{
    lock();
    cond_group_entry_t *e = findEntry(smid);
    if (e != NULL)
    {
        /* Only act on a SOLICITED reply: an unsolicited or replayed message must never
         * retire a live seat, because that would turn a missed fire into a FALSE one. */
        if (e->askPendingTick != 0 && popcount8(e->askSeatMask) == 1 && responderEid != 0)
        {
            uint8_t seat = 0;
            while (seat < SMART_COND_GROUP_MAX_N && !((e->askSeatMask >> seat) & 1u))
                seat++;

            /* ATTRIBUTION: a device that was never in this group answers "not a member"
             * just as truthfully — a device holding only the bind-CMD, for instance. Acting
             * on that would retire a seat whose owner is merely OFFLINE. Retire only when
             * the responder is the EID we last heard announcing THIS seat. */
            if (seat < SMART_COND_GROUP_MAX_N && e->seatEid[seat] == responderEid)
            {
                unlock();
                SmartService_CondGroupRetireSeat(smid, seat);
                return;
            }
            IOT_LOGD(TAG, "SMID=%04x: not-member from %04x does not own seat %u (owner=%04x)",
                     smid, responderEid, seat, e->seatEid[seat]);
        }
        else
        {
            IOT_LOGD(TAG, "SMID=%04x: not-member reply not actionable (ask=%u seats=0x%02x)",
                     smid, (unsigned) (e->askPendingTick != 0), e->askSeatMask);
        }
    }
    unlock();
}

bool SmartService_CondGroupCannotReachThreshold(uint16_t smid)
{
    bool res = false;
    lock();
    cond_group_entry_t *e = findEntry(smid);
    if (e != NULL)
        res = entryCannotReachK(e);
    unlock();
    return res;
}

static bool s_reachabilityArmed;

static void condReachabilityTickCb(void *userData)
{
    (void) userData;
    (void) SmartService_CondGroupCheckReachability();
    /* Re-arm. Runs on the timer task, the same one the other cond-group callbacks use —
     * NOT a platform timer-service callback, whose small stack cannot take work like
     * this. The sweep itself is popcounts over <= COND_GROUP_MAX_ENTRIES entries; the only
     * sends it makes happen outside the lock and only for suspect groups. */
    IOT_TimerInsertCb(condReachabilityTickCb, NULL, COND_UNREACHABLE_SWEEP_MS);
}

/* Armed on the first group this device joins, so a device with no cond-groups never pays
 * for the sweep at all. Idempotent. */
static void armReachabilitySweep(void)
{
    if (s_reachabilityArmed)
        return;
    if (IOT_TimerInsertCb(condReachabilityTickCb, NULL, COND_UNREACHABLE_SWEEP_MS))
        s_reachabilityArmed = true;
}

/* Seats that have been quiet long enough to be worth probing: not ours, and either never
 * heard or last heard beyond the suspicion window. Returns 0 when nothing is suspect. */
static uint8_t suspectSeats(const cond_group_entry_t *e)
{
    uint32_t now = nowTick();
    uint32_t suspect = msToTicksLong(COND_PROBE_SUSPECT_MS);
    uint8_t out = 0;

    for (uint8_t i = 0; i < e->condCount && i < SMART_COND_GROUP_MAX_N; i++)
    {
        uint8_t bit = (uint8_t)(1u << i);
        if (e->ownMask & bit)
            continue;                       /* our own seat is never in doubt */
        if (e->retiredMask & bit)
            continue;                       /* already known absent; nothing to learn */
        if (e->bitHeard[i] == 0 || (uint32_t)(now - e->bitHeard[i]) >= suspect)
            out |= bit;
    }
    return out;
}

uint16_t SmartService_CondGroupCheckReachability(void)
{
    uint16_t transitions = 0;
    uint16_t probeSmids[COND_GROUP_MAX_ENTRIES];
    uint16_t probeCount = 0;

    /* Log on the TRANSITION only. A group that is broken stays broken, and repeating the
     * warning every sweep would bury everything else — the opposite of the point. */
    lock();
    for (int i = 0; i < COND_GROUP_MAX_ENTRIES; i++)
    {
        cond_group_entry_t *e = &s_groups[i];
        if (!e->inUse)
            continue;

        uint8_t suspect = suspectSeats(e);
        bool probeDue = (suspect != 0) &&
                        (e->lastProbeTick == 0 ||
                         (uint32_t)(nowTick() - e->lastProbeTick) >= msToTicksLong(COND_PROBE_PERIOD_MS));
        if (probeDue)
        {
            /* Clear the evidence we are about to re-establish. A live member answers the
             * probe within seconds and sets it again; one that is gone stays cleared and is
             * reported after the settling window. */
            for (uint8_t seat = 0; seat < SMART_COND_GROUP_MAX_N; seat++)
            {
                if (suspect & (uint8_t)(1u << seat))
                    e->bitHeard[seat] = 0;
            }
            e->lastProbeTick = nowTick();
            e->unreachableReported = 0;     /* re-judge from the fresh evidence */
            probeSmids[probeCount++] = e->smid;
        }

        bool bad = entryCannotReachK(e);
        if (bad && !e->unreachableReported)
        {
            e->unreachableReported = 1;
            transitions++;
            IOT_LOGW(TAG,
                     "SMID=%04x CANNOT REACH K: need %u, only %u of %u seat(s) ever seen "
                     "(seats heard=0x%02x). A member is gone or has never been online — the "
                     "automation will not fire until it returns or the cloud re-binds it.",
                     e->smid, (unsigned) effectiveK(e), (unsigned) popcount8(seatsEverHeard(e)),
                     (unsigned) e->condCount, seatsEverHeard(e));
        }
        else if (!bad && e->unreachableReported)
        {
            e->unreachableReported = 0;
            transitions++;
            IOT_LOGI(TAG, "SMID=%04x recovered: all required seats accounted for", e->smid);
        }
    }
    unlock();

    /* Send OUTSIDE the lock — this lock is contended by the timer and core event tasks and
     * must never be held across a send. askForFire stays false: live members answer because
     * they own the group, while non-members correctly stay silent. */
    for (uint16_t i = 0; i < probeCount; i++)
    {
        IOT_LOGI(TAG, "SMID=%04x: seat(s) quiet for %u h — probing", probeSmids[i],
                 (unsigned) (COND_PROBE_SUSPECT_MS / 3600000u));
        SmartService_CondGroupSendCheck(&probeSmids[i], 1, false);
    }
    return transitions;
}

void SmartService_CondGroupRetireSeat(uint16_t smid, uint8_t seat)
{
    if (seat >= SMART_COND_GROUP_MAX_N)
        return;

    lock();
    cond_group_entry_t *e = findEntry(smid);
    if (e != NULL)
    {
        uint8_t bit = (uint8_t)(1u << seat);
        /* Never retire a seat we own — we know for certain we are a member, so an answer
         * claiming otherwise is wrong (or aimed at someone else) and must not disarm us. */
        if (seat < e->condCount && !(e->ownMask & bit))
        {
            e->retiredMask |= bit;
            e->mask &= (uint8_t)~bit;   /* a retired seat contributes nothing */
            e->bitHeard[seat] = 0;
            /* Recompute against the smaller pool: the group may now be satisfied by the
             * survivors alone, or may drop below threshold. Never fire from here — this is
             * bookkeeping; the next real evaluation decides. */
            e->prevSatisfied = popcount8(e->mask) >= effectiveK(e) ? 1 : 0;
            IOT_LOGI(TAG, "SMID=%04x: seat %u confirmed absent — live=%u K_eff=%u",
                     smid, seat, (unsigned)(e->condCount - popcount8(e->retiredMask)),
                     (unsigned) effectiveK(e));
        }
    }
    unlock();
}

void SmartService_CondGroupReset(void)
{
    lock();
    memset(s_groups, 0, sizeof(s_groups));
    unlock();
    /* The sweep timer keeps running; it simply finds nothing. Leaving it armed avoids
     * re-arming churn if groups come back, and an empty sweep costs a loop over 16 slots. */
}

void SmartService_CondGroupResyncOwnBit(const IOT_SmartTrigger_t *trig, bool result)
{
    if (trig == NULL)
        return;

    IOT_CoreSmartReportCondState_t report = {0};
    bool haveReport = false;

    uint8_t ownSeats = 0;
    (void) IOT_DaoSmartTriggerGetSeatMask(trig->smid, &ownSeats); /* before the lock */

    lock();
    cond_group_entry_t *e = findEntry(trig->smid);
    if (e == NULL)
        e = createEntry(trig->smid, trig->eid, trig->condIndex, trig->condCount,
                        trig->fireCount, trig->condVersion, ownSeats);

    if (e != NULL && trig->condIndex < e->condCount && trig->condIndex < SMART_COND_GROUP_MAX_N)
    {
        uint8_t bit = (uint8_t)(1u << trig->condIndex);
        e->ownMask |= bit;
        if (result)
            e->mask |= bit;
        else
            e->mask &= (uint8_t)~bit;

        /* Rebuild is not a fresh edge — set prevSatisfied from the mask but never
         * fire, then re-announce this seat's report so peers converge. */
        e->prevSatisfied = popcount8(e->mask) >= effectiveK(e) ? 1 : 0;
        fillReportForSeat(e, trig->condIndex, &report);
        haveReport = true;
    }
    unlock();

    /* A rebuild/re-announce is NOT a condition flip, so it goes out as a sync push rather
     * than a condition-state report, which is reserved for genuine flips. */
    if (haveReport)
        IOT_CoreRequestSmartSyncCondPush(&report, 1);
}

/* Send a SYNC GET: for each SMID we own, send OUR OWN status alongside the query, so
 * peers can compare and stay silent when nothing differs. */
void SmartService_CondGroupSendCheck(const uint16_t *smids, uint16_t count, bool askForFire)
{
    if (smids == NULL || count == 0)
        return;

    for (uint16_t off = 0; off < count; off += COND_GROUP_CHECK_CHUNK)
    {
        uint16_t chunk = (uint16_t)(count - off);
        if (chunk > COND_GROUP_CHECK_CHUNK)
            chunk = COND_GROUP_CHECK_CHUNK;

        IOT_CoreSmartReportCondState_t groups[COND_GROUP_CHECK_CHUNK] = {{0}};
        uint16_t n = 0;

        /* Seed ONE group at a time: the seed carries a whole trigger record, so an array
         * of them would put kilobytes on a task stack that does not have it (this runs on
         * the "timer" task). Taking the lock per group instead is cheap — SendCheck runs at
         * boot/reconnect, not on the hot path — and keeps flash I/O outside the lock. */
        for (uint16_t i = 0; i < chunk; i++)
        {
            cond_group_seed_t seed = seedFromDao(smids[off + i]);
            lock();
            cond_group_entry_t *e = getOrBuild(smids[off + i], &seed);
            if (e != NULL)
                fillReportForSeat(e, lowestOwnedSeat(e), &groups[n++]);
            unlock();
        }

        if (n > 0)
        {
            for (uint16_t g = 0; g < n; g++)
            groups[g].askForFire = askForFire ? 1u : 0u;

        IOT_CoreSmartSyncCondRequest_t req = {.groups = groups, .groupCount = n};
            IOT_CoreRequestSmartSyncCondRequest(&req);
        }
    }
}

/* Does the requester's view of THIS group differ from ours in a way only we can correct?
 * We own exactly one bit, so we answer when:
 *   - our config generation is newer (they are stale), or
 *   - their N/K disagrees with ours at the same generation, or
 *   - the bit THEY recorded for US is wrong.
 * Everything matching -> stay silent (saves a message; this is the common case). */
static bool needsCorrection(const cond_group_entry_t *e, const IOT_CoreSmartCondStatus_t *req)
{
    if (e->version > req->version)
        return true;
    if (e->version < req->version)
        return false; /* they are newer — our own re-announce path handles adopting it */
    if (e->condCount != req->condCount || e->fireCount != req->fireCount)
        return true;

    /* Compare every bit we own — a multi-seat device must correct all of them. */
    uint8_t ourBits = (uint8_t)(e->mask & e->ownMask);
    uint8_t theirViewOfUs = (uint8_t)(req->mask & e->ownMask);
    return ourBits != theirViewOfUs;
}

/* Answer a SYNC request — ONLY for groups where we actually differ, and unicast back to
 * the asker (never re-broadcast to the whole location). */
void SmartService_CondGroupAnswerSync(const IOT_CoreSmartCondStatus_t *reqGroups, uint16_t count,
                                      uint16_t requesterEid)
{
    if (reqGroups == NULL || count == 0)
        return;

    IOT_CoreSmartReportCondState_t reps[COND_GROUP_CHECK_CHUNK];
    uint16_t n = 0;

    /* One seed at a time — see the note in SendCheck; an array of them overflows the
     * task stack. */
    uint16_t seedCount = (count < COND_GROUP_CHECK_CHUNK) ? count : COND_GROUP_CHECK_CHUNK;
    for (uint16_t i = 0; i < seedCount && n < COND_GROUP_CHECK_CHUNK; i++)
    {
        cond_group_seed_t seed = seedFromDao(reqGroups[i].smid);
        lock();
        cond_group_entry_t *e = getOrBuild(reqGroups[i].smid, &seed);
        if (e != NULL && needsCorrection(e, &reqGroups[i]))
            fillReportForSeat(e, lowestOwnedSeat(e), &reps[n++]);
        unlock();
    }

    if (n > 0)
        IOT_CoreRequestSmartSyncCondResponse(reps, n, requesterEid);
    else
        IOT_LOGD(TAG, "SYNC request: nothing differs, staying silent");
}

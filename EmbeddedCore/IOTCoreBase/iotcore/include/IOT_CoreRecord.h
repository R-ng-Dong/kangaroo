#pragma once
#include <inttypes.h>

#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_CoreEventHandler.h"

/*
 * Record V2 responses.
 *
 * The SDK hands over facts; core owns the encoding. Nothing here is a serialised
 * form: do not pre-pack entries, do not byte-swap, and do not pad to a block size. Core
 * builds the header, converts byte order, orders the power pair the way the cloud reads
 * it, and stops at the reply budget.
 *
 * attrId crosses this boundary as an opaque number. Core never switches on it -- which is
 * why valueCount is a parameter rather than something core derives.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One block of history, as fields rather than bytes.
 *
 * A block is the unit the app asks for and walks: it carries its own checkpoint, and the
 * checkpoint of the block before it. `prevCheckpoint` 0 means there is nothing older --
 * the app stops there.
 */
typedef struct
{
    /** Epoch MILLISECONDS this block opened. Every entry's diffSeconds is measured from
     *  here, so it is the origin of the block's whole time axis. */
    uint64_t checkpoint;
    /** Checkpoint of the next older block, or 0 for the oldest. 0 ends the chain. */
    uint64_t prevCheckpoint;
    /**
     * Host-order fields, laid out per entry as:
     *   valueCount == 1:  [value][diffSeconds]
     *   valueCount == 2:  [value1][value2][diffSeconds]
     *
     * Natural order, NOT the cloud's order -- core does the reordering for you. Entries run
     * oldest to newest, which is the opposite of the block order.
     *
     * Borrowed for the duration of the call; core copies what it sends, so one scratch
     * buffer may be refilled between blocks.
     */
    const uint16_t *fields;
    /** Total uint16s in `fields`, NOT the entry count. Two numbers describing one array
     *  can contradict each other; this is the one core can check, and it does:
     *  fieldCount must divide by (valueCount + 1). */
    uint16_t fieldCount;
} IOT_CoreRecordBlock_t;

/** How many record blocks one GET_STATE reply may carry.
 *
 * Each costs ~242 bytes of payload (240-byte block plus its framing), and the reply is
 * reallocated to its exact size as each block is added -- so the peak contiguous demand
 * is the final payload size, drawn from the same heap a TLS handshake needs one large
 * contiguous block of. 4 matches the cap a record reply already uses, so neither reply
 * path can outgrow the other's assumptions. An app wanting more walks the chain with a
 * Record V2 history query, which is the path that has always existed. */
#define IOT_CORE_RECORD_STATE_BLOCKS_MAX 4

/**
 * @brief One block plus the identity needed to encode it.
 *
 * IOT_CoreRecordBlock_t deliberately carries no eid/elem/attr: a record REPLY is about
 * one attribute, so the identity lives once on the response rather than once per block.
 * A GET_STATE reply is the other shape -- one block each for SEVERAL attributes -- so it
 * needs the pair. Added 2026-10-22 rather than widening IOT_CoreRecordBlock_t, which
 * would change a struct partners already compile against.
 */
typedef struct
{
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    /** 1, or 2 for a two-field attribute (the power pair); never more. IOT_ATTR_OPEN_CLOSE_CTL keeps 1. */
    uint8_t valueCount;
    IOT_CoreRecordBlock_t block;
} IOT_CoreRecordStateBlock_t;

/**
 * @brief A record reply: the blocks to send, newest first.
 *
 * `blockCount` is never 0. For an unknown or evicted checkpoint the SDK supplies ONE
 * block whose checkpoint is the value that was requested, with prevCheckpoint 0 and no
 * entries. Core never invents a block, so it never has to guess which checkpoint to echo
 * -- and a reply carrying checkpoint 0 would collide with both the end-of-chain sentinel
 * and the "give me the current block" request, sending a conforming app back to the head
 * of the chain.
 */
typedef struct
{
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    /** 1, or 2 for a two-field attribute (the power pair); never more. IOT_ATTR_OPEN_CLOSE_CTL keeps 1. */
    uint8_t valueCount;
    /** Newest first. At least one, always. */
    const IOT_CoreRecordBlock_t *blocks;
    uint16_t blockCount;
    IOT_RequestContext_t ctx;
} IOT_CoreRecordResponse_t;

/**
 * @brief Build and send a record reply.
 *
 * Frames each block separately, in the order given, and stops once the reply
 * reaches its budget rather than failing -- the app walks the rest by asking for the last
 * prevCheckpoint it received.
 *
 * Call this SYNCHRONOUSLY, inside the event callback. ctx.senderId points into the
 * message reader's buffer and is freed when the handler returns, so a reply built on
 * another task routes through freed memory.
 *
 * @param response Blocks plus the request echo. Arrays are borrowed; they need only
 *                 outlive the call.
 * @return IOT_OK, or IOT_ERR_INVALID_ARG for a null response, a zero blockCount, or a
 *         valueCount outside 1..2.
 */
iot_err_t IOT_CoreRequestRecordResponse(const IOT_CoreRecordResponse_t *response);

/** Most blocks one push carries: 4 x 242 B framed fits the 1000-byte record budget. */
#define IOT_CORE_RECORD_PUSH_BLOCKS_MAX 4

typedef struct
{
    uint16_t eid;
    uint16_t elemId;
    uint16_t attrId;
    /** 1, or 2 for a two-field attribute (the power pair); never more. IOT_ATTR_OPEN_CLOSE_CTL keeps 1. */
    uint8_t valueCount;
    /** OLDEST first -- the opposite of IOT_CoreRecordResponse_t. 1..IOT_CORE_RECORD_PUSH_BLOCKS_MAX. */
    const IOT_CoreRecordBlock_t *blocks;
    uint16_t blockCount;
} IOT_CoreRecordPush_t;

/**
 * Send closed reels to the cloud, unsolicited. All or nothing: never trims.
 * The message also carries the device's current time, so the cloud can check the device clock.
 * @return IOT_OK once the message is handed to the MQTT client;
 *         IOT_ERR_INVALID_ARG for null, blockCount 0 or > MAX, valueCount outside 1..2;
 *         an encoder error for a malformed block;
 *         IOT_ERR_INVALID_STATE when the cloud session is not fully up yet (later than the MQTT
 *         connect event), or the device clock has not synced -- retry soon;
 *         any other non-OK = not sent, retry later.
 *
 * Blocking: it waits on the MQTT publish. Call it from a task, never from a timer callback.
 */
iot_err_t IOT_CoreReportRecordBlocks(const IOT_CoreRecordPush_t *push);

#ifdef __cplusplus
}
#endif

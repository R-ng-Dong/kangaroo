# Record History (V2): Writing Your Own Record

As of 2026-10-30. **This guide is for path B**, an SDK that keeps attribute history itself on the
public `iotcore` API. On path A (`sdkcontract/`) the contract's Record does all of this for you;
read [7_Record_Contract.md](7_Record_Contract.md) instead.

**The rules and the API are in [7_Record_Contract.md](7_Record_Contract.md)**, the Record rulebook
for both paths: the ten rules, the block, answering a query, the GET_STATE checkpoint, pushing
closed blocks, the return codes, the pitfalls and a minimal sketch. Where this guide and that one
differ, that one is right. This guide adds what a Record of your own has to get right around those
calls: who owns what, the lifecycle of a series, and the fixed limits core enforces. The shipped
headers `IOT_CoreRecord.h`, `IOT_CoreEventState.h` and `IOT_CoreState.h` are the authority on
every signature.

## Who owns what

| Who | Owns |
| --- | --- |
| Your SDK | What is worth recording (policy), when a block closes, flash storage and retention, the "last pushed" bookmark, when to push |
| Core (`libiotcore.a`) | Parsing the app's history query, the message format, the reply size limit, delivery to the cloud |

Core records nothing on its own. An SDK that never handles the Record event and never calls the
Record functions simply has no history; nothing else breaks.

Record V1 (`IOT_CoreSensorLog.h`, day-keyed) is **not in the shipped library**:
`IOT_CoreRequestSensorLogResponse()` sends nothing and returns `IOT_ERR_NOT_SUPPORTED`. Use V2.

The terms (series, block, `checkpoint`, `prevCheckpoint`, entry, `valueCount`, open and closed
block) are defined in [7_Record_Contract.md](7_Record_Contract.md#how-history-is-organised). Take
`valueCount` from `IOT_AttrRecordValueCount(attrId)` in your attribute table (the shipped
`reference/defines/IOT_AttrDefines.c` has it), never from the attribute's state size.

## Lifecycle

A series goes through five stages, all driven by your SDK; core appears only when a message
arrives or leaves.

1. **Wait for the clock.** Record nothing until `IOT_TimerIsTimeSynced()` is true. Stamp with
   `IOT_TimerGetU64UnixTimeMs()` (epoch), not a monotonic timer. A block stamped in 1970 stays in
   the chain forever.
2. **Record.** On each state change, wherever it came from (a state command, your own hardware, a
   sub-device), decide whether the value is worth an entry (your policy, within rule 4 of the
   rulebook) and append the value and its offset in seconds to the open block in RAM.
3. **Close and store.** When the block is full, the offset would pass 65535, or the clock stepped
   back, write it to flash, evict your oldest block if your ring is full, and open a new one whose
   `prevCheckpoint` is the block just closed. The oldest block you still hold must report
   `prevCheckpoint = 0`.
4. **Serve.** Answer `IOT_EVENT_TYPE_STATE_RECORD` from RAM (open block) and flash (closed
   blocks), and attach open blocks to GET_STATE replies.
5. **Push (optional).** After a block closes and is on flash, push it. On every cloud connect,
   push the backlog oldest first, up to `IOT_CORE_RECORD_PUSH_BLOCKS_MAX` blocks per call, with a
   gap between calls, from a task of your own (never a timer callback).

```
  App / cloud                 Core (libiotcore.a)              Your SDK
      |                              |                              |
  1 · First checkpoint               |                              |
      |-- GET_STATE + time req. ---->|                              |
      |                              |---- STATE_GET event -------->|
      |                              |<--- state + open block ------|
      |                              |     per series               |
      |<-- state + current ----------|                              |
      |    checkpoints               |                              |
  2 · Walk the chain                 |                              |
      |-- history query, C --------->|                              |
      |                              |---- STATE_RECORD event ----->|
      |                              |<--- block C, then older, ----|
      |                              |     newest first             |
      |<-- up to 4 blocks -----------|                              |
      |   (repeat from last prev until it is 0)                     |
  3 · Push (optional)                |                              |
      |                              |<--- ReportRecordBlocks, -----|
      |<-- closed blocks ------------|     oldest first             |
```

Core only translates at each crossing; every block it sends comes from your SDK.

**Power cuts.** The open block lives in your RAM, so a power cut loses whatever you had not
flushed. Flush on a timer you choose (`sdkcontract/`'s Record flushes every 60 s by default), and
write everything out before a restart you plan (rule 6). Store a monotonic sequence number with
each block and order your ring by it on boot, **not** by checkpoint: a backward clock step makes
checkpoints go backwards.

**Push delivery is at-least-once.** The cloud stores blocks by checkpoint and a later copy
replaces an earlier one, so a resend after a reboot is harmless. GET_STATE may already have shown
the cloud a half-filled version of the same checkpoint.

**Before you turn pushing on,** check with Rogo that your tenant's cloud accepts pushed blocks.
Until then, keep pushing off; queries and GET_STATE work without it.

## Configuration and fixed limits

**There is nothing to configure in core.** The shipped `libiotcore.a` always has Record V2 and
never has V1, and it reads no Record option. Ring depth, flush interval, recording policy and
whether to push are all settings of your own SDK.

The fixed limits core enforces:

| Limit | Value | Source |
| --- | --- | --- |
| Entries per block | 54 (one value) or 36 (two values) | fixed |
| Blocks per reply or push | 4 | `IOT_CORE_RECORD_PUSH_BLOCKS_MAX` |
| Blocks in a GET_STATE reply | 4 | `IOT_CORE_RECORD_STATE_BLOCKS_MAX` |
| Entry offset range | 0–65535 s (~18 h) | 16-bit field |
| `valueCount` | 1 or 2 | `IOT_AttrRecordValueCount()` |

## References

- [7_Record_Contract.md](7_Record_Contract.md): the rules, the core Record API, the return codes,
  the pitfalls and a minimal sketch.
- `IOT_CoreRecord.h`, `IOT_CoreEventState.h`, `IOT_CoreState.h`: the shipped headers. Their
  comments are the authority.
- [../core/API_Document.md](../core/API_Document.md): the `RECORD` event in §3, and Record
  Response / Record Push in §4.
- [4_DAO_Guide.md](4_DAO_Guide.md): the storage interface to keep your blocks in.

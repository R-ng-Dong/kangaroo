# Build an IR device

## Declare the capability

```c
/* before IOT_ContractStart(): transmits, learns, recognises protocols */
(void) IOT_ContractIrConfigure(true, true, false);
```

The contract answers the cloud's IR capability question with these values. Not called: the device
declares no IR capability (as if called with all three false). A later call replaces the earlier
values and takes effect at the next question.

| Argument | Meaning |
|---|---|
| `tx` | The device can transmit IR |
| `rx` | The device can receive IR (learn a remote) |
| `detect` | The device can recognise a remote's protocol from what it receives |

## Keysets are stored for you

The contract stores the IR keysets the cloud sends for an IR appliance. Read them back through the
read-only IR store (`include/dao/IOT_DaoIRMgmt.h`):

| Call | Returns |
|---|---|
| `IOT_DaoIRGetProtocolCode(eid, dataName, &code)` | A protocol-based code for one button (`dataName` = button index) |
| `IOT_DaoIRGetRawZipHeader(eid, &header, &len)` | The appliance's shared rawzip header; the caller frees it |
| `IOT_DaoIRGetRawZipData(eid, dataName, &data, &len)` | Compressed raw data for one button; the caller frees it |

`IOT_IR_HEADER_DATANAME` is the reserved `dataName` of the header. Each returns
`IOT_ERR_NOT_FOUND` when nothing is stored. They block on storage; any task, including your core
event callback.

## Sending and learning stay yours

A send (`EXEC_*`) or a learn request (`LEARN_START`) arrives at `onCoreEvent` as `IOT_EVENT_IR`.
Write `evt.ir.result` before returning — `IOT_OK` when the code was sent, or when the learn window
opened (report the capture later) — or the cloud is told the device does not support it. See
[core-events.md](core-events.md#answering-a-request).

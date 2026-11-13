# Actuate in `apply`

`apply` is required. The contract calls it once per element of a state command it accepted, in
order, device by device for a command that addresses a group or the whole location; for a device
attribute set on this device; for an automation's actions; and for the boot restore.

```c
static iot_err_t app_apply(const IOT_ContractApply_t *req)
{
    if (req->attrId != IOT_ATTR_ONOFF || !my_relay_valid(req->elemId))
        return IOT_ERR_NOT_FOUND;              /* not driven here: stored, not an error */
    uint16_t onOff = IOT_OFF;
    if (IOT_ContractApplyField(req, 0, &onOff) != IOT_OK)
        return IOT_ERR_INVALID_ARG;            /* malformed value: nothing stored */
    my_relay_drive(req->elemId, onOff != IOT_OFF);
    return IOT_OK;
}
```

## What `req` holds

| Field | Meaning |
|---|---|
| `eid` | This device's EID, or a sub-device's |
| `elemId` | Your own element number (declared in `elementIds`) for this device; the hub-assigned id for a sub-device. Never the cloud's id |
| `attrId` | The attribute to set (`IOT_ATTR_*` or one you registered) |
| `value`, `valueLen` | The value bytes in the attribute's stored layout. Read fields with `IOT_ContractApplyField(req, index, &out)` (first field is 0) |
| `source` | Who asked: `IOT_CONTRACT_SOURCE_CLOUD`, `_APP_LOCAL`, `_AUTOMATION`, `_SCHEDULE`, `_VOICE`, `_DEVICE`, `_OTHER`, or `_RESTORE` for the boot restore |

`req` and its value are valid only during the call; copy what you keep.

## What to return

| Return | Meaning |
|---|---|
| `IOT_OK` | The hardware took the value (for a sub-device: it was sent) |
| `IOT_ERR_NOT_FOUND` | This element or attribute is not driven by your product; treated as success, still stored |
| anything else | Stops this device's remaining elements. Elements already applied are committed and reported; the failing one is not. Other devices of the command carry on. A device command that moved nothing is answered with this error |

Return `IOT_ERR_NOT_FOUND`, not `IOT_ERR_NOT_SUPPORTED`, for an attribute you do not drive: any
error other than `IOT_ERR_NOT_FOUND` means "not applied", and nothing is stored for it.

## Rules

- **Do not store, record or report the change yourself.** After the last device the contract
  stores what was applied, records it, sends one state report for the whole command, then feeds
  Smart with the command's sender.
- **An element you did not declare** (the cloud's model has it, `elementIds` does not) is skipped
  with a warning; `apply` never sees it.
- **A device attribute set** reaches `apply` only for this device (one call, element id translated
  the same way); applied or `IOT_ERR_NOT_FOUND`, it is stored, recorded, answered, then given to
  Smart, and any other error is the answer with nothing stored. For a sub-device the value is
  stored without driving it: no `apply` call.
- **A mirrored on/off** from a sync-on-off or stair-switch pairing reaches `apply` as plain
  `IOT_ATTR_ONOFF` only when this device holds the pairing; otherwise it is refused before `apply`
  (subscribers see `IOT_CONTRACT_EVT_MIRROR_REFUSED`). See [the Smart rulebook](../smart.md), rule 5.
- **Task.** Always the task that delivers core events — an automation's actions (immediate, delayed
  or reverse) and the boot restore too. Keep it short and non-blocking (the next request waits),
  and guard hardware you also drive from your own tasks.
- **Boot restore.** `req->source == IOT_CONTRACT_SOURCE_RESTORE`: drive the hardware to the value as
  usual; the return is logged and ignored. See [restore-at-boot.md](restore-at-boot.md).

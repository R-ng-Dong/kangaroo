# Handle state commands and queries (path B)

What an SDK written directly on `iotcore` does for `IOT_EVENT_STATE`: resolve a SET command to the
devices it addresses, actuate, store, report and answer; answer a GET from the store; report a
change your hardware made by itself. On path A you write none of this: the contract runs the command
and calls your `apply` ([sdkcontract's apply how-to](../../../sdkcontract/docs/how-to/apply.md)).

The events themselves (when, task, data, answer) are in
[events.md](../events.md#iot_event_type_state_set). The rules every state change must keep for
history and automations are the [Record rulebook](../../../sdkcontract/docs/record.md) and the
[Smart rulebook](../../../sdkcontract/docs/smart.md).

## A SET command

1. **Resolve the target** to a list of device EIDs from `targetType` (below).
2. **Accept or refuse.** A sync-on-off mirror is acted on only from a pairing this device holds
   (Smart rulebook, rule 5); otherwise write `result = IOT_OK` and do nothing more.
3. **For each device**: actuate the hardware (or send to the sub-device), write your Device store,
   record the change, send the report, then feed trigger evaluation unless the sender is Smart's own
   output ([concepts.md](../concepts.md#loop-prevention)).
4. **Answer:** write `setData->result` before returning.

### Resolve the target

`targetType` says what the command addresses. Core has already decided it from the address in the
message; read it, never compare EIDs yourself.

| `targetType` | Target field | Elements in `devSetInfo` | Your job |
|---|---|---|---|
| `IOT_SET_TARGET_DEVICE` | `target.device.eid` | `elemList` / `numOfElems` | Apply to those elements of that device |
| `IOT_SET_TARGET_LOCATION` | none: `targetType` is the whole meaning | Usually none (`elemList` NULL); a voice command for the location carries its element list | Every device in your Device store |
| `IOT_SET_TARGET_GROUP` | `target.group.groupEid` (strictly greater than `0xC000`) | Usually none; a voice command for a group carries its element list | Expand the group from your group store; a member bound with specific elements responds on those only |
| `IOT_SET_TARGET_VOICE` | `target.voice.devId` / `devIdLen` (12-byte device id) | `elemList` / `numOfElems` | Resolve the device id to an EID in your Device store |

- **`0xC000` is the location, not a group.** `IOT_GROUP_EID_BASE` is "all devices"; real groups
  start at `0xC001`. A test of `eid >= 0xC000` treats every room command as a group that does not
  exist.
- **A voice command never names a group as VOICE.** When it carries a group, core delivers it as
  LOCATION or GROUP; by the time VOICE reaches you it always means exactly one device.
  `target.voice.groupEid` is not used.
- **`targetDeviceType`** is non-zero when the cloud limits a group or location command to one
  device type (lights only, for example): skip every device whose type does not match. 0 means
  every type.
- `devSetInfo.eid` is not used in a SET event.

```c
IOT_EventStateSetRequest_t *setData = &stateEvent->data.setRequest;
uint16_t *eidList = NULL;
uint16_t numDevices = 0;
uint16_t voiceEid = 0;   // function scope: eidList may point at it after the switch

switch (setData->targetType)
{
case IOT_SET_TARGET_DEVICE:
    eidList = &setData->target.device.eid;
    numDevices = 1;
    break;
case IOT_SET_TARGET_LOCATION:
    eidList = my_all_device_eids(&numDevices);                       // your Device store
    break;
case IOT_SET_TARGET_GROUP:
    eidList = my_group_members(setData->target.group.groupEid, &numDevices); // your group store
    break;
case IOT_SET_TARGET_VOICE:
    if (my_device_eid_by_dev_id(setData->target.voice.devId, &voiceEid) == IOT_OK)
    {
        eidList = &voiceEid;
        numDevices = 1;
    }
    break;
}
```

### Per device: actuate, store, report

```c
for (uint16_t d = 0; d < numDevices; d++)
{
    uint16_t eid = eidList[d];

    // 1. Actuate the hardware (this device) or send to the sub-device.
    // 2. Write the changed attribute into your Device store.
    my_device_update_elem_state(eid, &setData->devSetInfo);

    // 3. Read back the device's full state and identity for the report.
    my_device_get_full_state(eid, &fullState);     // every element, every attribute
    my_device_get_identity(eid, &identity);        // mac, device id

    // 4. Report: one device, its full state, only the elements the command changed.
    uint16_t changed[MY_MAX_ELEMS];
    for (uint16_t i = 0; i < setData->devSetInfo.numOfElems; i++)
        changed[i] = setData->devSetInfo.elemList[i].elemId;

    IOT_CoreStateChangeDeviceData_t deviceData = {
        .eid = eid,
        .mac = identity.mac,
        .macLen = identity.macLen,
        .devId = identity.devId,
        .devIdLen = identity.devIdLen,
        .fullState = &fullState,               // ALL elements, ALL attributes
        .numChangedElems = setData->devSetInfo.numOfElems,
        .changedElemIds = changed,             // ONLY the elements the command named
        .changedAttrId = setData->devSetInfo.attrId,
        .pushNotify = false,                   // true for safety/alert notifications
    };
    IOT_CoreStateChangeReport_t report = {
        .devices = &deviceData,
        .numDevices = 1,
        .ctx = setData->ctx,                   // pass the request's context through unchanged
    };
    IOT_CoreRequestReportStateChanged(&report);
}
setData->result = IOT_OK;
```

Two rules the report depends on:

- **`fullState` holds every element of the device**, including those that did not change.
- **`changedElemIds` holds only the elements that changed** (those the command named).

Core looks up the location id, builds the message and sends it. `report.numDevices` may be more
than one for a group or location command.

## A GET query

Read the device's full state from your store and answer with `IOT_CoreRequestReportState()` before
you return; there is no `result`:

```c
IOT_CoreStateResponse_t response = {
    .eid = getData->eid,
    .fullState = &elemsStateInfo,
    .ctx = getData->ctx,               // pass-through routing context
    .unixTime = getData->unixTime,     // NULL when the caller asked for no time
};
IOT_CoreRequestReportState(&response);
```

Core adds the time sync (when asked, `IOT_CoreTimeRequested()`) and the connectivity attribute.

## A change your hardware made by itself

A button, a sensor or a sub-device changed state with no command. Store it, record it, report it
with `IOT_CoreRequestReportStateChanged()` exactly as above, with a context you build yourself whose
`senderFrom` is `IOT_SENDER_DEVICE`, and feed trigger evaluation. Call it from a task of your own,
never from a timer callback or an ISR: the report blocks while the message is handed to the cloud
connection.

## History queries

`IOT_EVENT_TYPE_STATE_RECORD` is answered with `IOT_CoreRequestRecordResponse()`, synchronously;
the block format, the checkpoint chain and pushing closed blocks with `IOT_CoreReportRecordBlocks()`
are in the [Record rulebook](../../../sdkcontract/docs/record.md#the-core-record-api). The legacy
`IOT_EVENT_TYPE_STATE_SENSOR_LOG` cannot be answered by the shipped library
([events.md](../events.md#iot_event_type_state_sensor_log)).

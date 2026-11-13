# Run a hub: joins, removals, groups (path B)

What an SDK written directly on `iotcore` does when a sub-device joins or leaves, when the cloud
binds groups or sets a device attribute, and when the product resets itself. On path A the contract
stores all of it and answers the cloud; you get its hooks instead
([sdkcontract's sub-device how-to](../../../sdkcontract/docs/how-to/sub-devices.md) and
[hooks](../../../sdkcontract/docs/how-to/hooks.md)).

Each event's task, data and answer are in [events.md](../events.md#device-events).

## Answering a device command

A join phase, a removal, a group bind or unbind and a device attribute set are requests the cloud
waits on until you have done the store work. Answer each with `IOT_CoreRespondDeviceCmd()` after
the store work, passing the event's `cmdData2` and `ctx` through unchanged:

```c
IOT_CoreDeviceCmdResponse_t rsp = {
    .cmdType = IOT_DEVICE_CMD_BIND_GRP,     // which command this answers
    .cmdData2 = bind->cmdData2,              // echoed, meaning depends on the command
    .result = storeErr,                      // IOT_OK = success, anything else = failure
    .ctx = bind->ctx,
};
IOT_CoreRespondDeviceCmd(&rsp);
```

It sends nothing when `ctx.requiresResponse` is false, so you may call it unconditionally. A command
left unanswered leaves the cloud waiting until it times out.

| Event | `cmdType` |
|---|---|
| `DEV_EVENT_NEW_DEVICE_JOINED`, phases DEVICE_INFO and ELM_INFO | `IOT_DEVICE_CMD_SYNC_DEV_JOINED` |
| `DEV_EVENT_DEVICE_REMOVED` (a sub-device) | `IOT_DEVICE_CMD_SYNC_DEV_REMOVED` |
| `DEV_EVENT_GROUP_BIND` | `IOT_DEVICE_CMD_BIND_GRP`, or `IOT_DEVICE_CMD_BIND_GRP_ELMS` when `elemData` is present |
| `DEV_EVENT_GROUP_UNBIND` | `IOT_DEVICE_CMD_UNBOUND_GRP` |
| `DEV_EVENT_DEVICE_ATTR_SET` | `IOT_DEVICE_CMD_DEVICE_ATTR` |

## A device joins

`DEV_EVENT_NEW_DEVICE_JOINED` comes once per phase, in order.

**Phase 1, `IOT_DEV_JOIN_PHASE_DEVICE_INFO`:** save the device's common and identity info in your
Device store, and add it to its groups.

```c
IOT_DevCommonInfo_t commonInfo = {
    .deviceType = joinData->data.deviceInfo.deviceType,
    .protocol = joinData->data.deviceInfo.protocol,
    .nwkAddr = joinData->data.deviceInfo.nwkAddr,
    .rootEid = joinData->data.deviceInfo.rootEid,
};
my_device_set_common_info(eid, &commonInfo);
my_device_set_identity_info(eid, &identityInfo);
my_group_add_device(groupId, eid);
```

**Phase 2, `IOT_DEV_JOIN_PHASE_ELM_INFO`:** create each element's attributes. The event carries
only the attribute ids; take each size from the attribute table (`IOT_ContractAttrSize()`, see
[attributes.md](attributes.md)) and seed a default value.

```c
for (uint8_t i = 0; i < joinData->data.elmInfo.numOfElms; i++)
{
    const IOT_DevJoinElmInfo_t *elm = &joinData->data.elmInfo.elmList[i];
    for (uint8_t a = 0; a < elm->numOfAttr; a++)
    {
        uint16_t attrId = elm->attrIdList[a];
        uint16_t size = IOT_ContractAttrSize(attrId);
        attrList[a].attrId = attrId;
        attrList[a].attrSize = size;
        attrList[a].attrValue = Mem_SafeMalloc(size, TAG, "attr default");
        my_attr_default(attrId, attrList[a].attrValue);   // your default-value helper
    }
    my_device_add_element(eid, elm->elemId, elm->elemType, attrList, elm->numOfAttr);
}
```

**Phase 3, `IOT_DEV_JOIN_PHASE_SYNC_COMPLETE`:** the join is done. Keep the sub-device's mesh key
if one came (copy it: the pointer is borrowed). There is nothing to answer.

```c
if (joinData->data.syncComplete.meshKey != NULL && joinData->data.syncComplete.meshKeyLen > 0)
{
    my_device_set_mesh_key(joinData->data.syncComplete.eid, joinData->data.syncComplete.meshKey,
                           joinData->data.syncComplete.meshKeyLen);
}
```

## A device is removed

`DEV_EVENT_DEVICE_REMOVED` is the cloud's removal command, never a local reset.

```c
if (removed->isRootDevice)
{
    // This device was removed. Core has already wiped its own identity and answered the cloud.
    my_stores_wipe_all();
    IOT_ApplicationRestartChip();          // come back up unprovisioned
}
else
{
    // A sub-device is gone: forget it in every store (Device, Group, Smart, Record, IR).
    // Anything that needs its record (retiring a mesh node, for example) runs first.
    my_stores_remove_device(removed->eid);
    // then answer with IOT_DEVICE_CMD_SYNC_DEV_REMOVED
}
```

## The product resets itself

Your product calls `IOT_CoreFactoryReset()` (a long press, for example). Core wipes its own identity
and delivers `DEV_EVENT_FACTORY_RESET` on the calling task before the call returns; that handler
wipes every store you own and restarts. It is not `DEV_EVENT_DEVICE_REMOVED`, and no cloud answer is
owed.

If your stores depend on each other (automation commands kept per SMID, found through the trigger
store), collect what you need before you wipe the store that indexes it, or the rest is left
orphaned in storage.

## Groups

`DEV_EVENT_GROUP_BIND` adds a device to a group (`newGroupId`), optionally only some of its elements
(`elemData` / `elemDataLen`, 2-byte element ids, NULL when the bind covers every element);
`oldGroupId` is the group it leaves, 0 when none. `DEV_EVENT_GROUP_UNBIND` removes it from
`groupId`.

Your group store must return every member of a group, each with its optional element list, so a
group command can be expanded ([handle-state.md](handle-state.md#resolve-the-target)):

- no element list: the group command applies to every element of that device;
- an element list: it applies to those elements only;
- removing a device removes it from every group;
- a bind with no `oldGroupId` clears the device's stale membership of other custom groups.

One compact layout is one storage key per group, holding repeated `[eid][elemDataLen][elemData]`
entries.

## A device attribute set

`DEV_EVENT_DEVICE_ATTR_SET` sets one attribute directly, outside a state command. Store the value
(and drive the hardware if it is this device's own), then answer with `IOT_DEVICE_CMD_DEVICE_ATTR`.

## Hub roles

A hub declares the transports it serves with `IOT_CoreSetHubRole()` (`IOT_CoreHubRoleFlags.h`) as
soon as it commits to the role, not once the transport is up: the cloud only sends mesh keys to a
device it believes is a hub. A Zigbee coordinator hub starts core's Zigbee role with
`IOT_CoreHubZigbeeStart()` (`IOT_CoreHubRole.h`). Work coming from a transport's driver callback is
handed to core's task with `IOT_CoreDeferWork()`, which frees its argument after the handler runs.

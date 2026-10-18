# Sub-Device and Hub Management

**SDK Writer Guide concepts:** Core Event Contract, SDK-Owned Responsibilities, SDK Integration Model.

> **Only relevant for hub-role devices (mesh gateways, bridge devices). Single-device products can skip this section.**

Hub-role devices manage a registry of sub-devices and relay cloud commands to them. iotcore dispatches `IOT_EVENT_DEVICE` events to your SDK for all lifecycle operations — device join, removal, and group management. Your SDK owns the device and group DAOs; iotcore never touches them directly.

---

## 1. Device Join: Three Phases

When a sub-device syncs to the hub, iotcore fires `DEV_EVENT_NEW_DEVICE_JOINED` **three times in sequence** — once per phase. Handle each phase and respond as indicated.

| Phase | `IOT_DevJoinPhase_t` value | Data available | SDK action | Respond? |
|-------|---------------------------|----------------|------------|---------|
| 1 | `IOT_DEV_JOIN_PHASE_DEVICE_INFO` | `devId[12]`, `mac[6]`, `macType`, `eid`, `rootEid`, `protocol`, `nwkAddr`, `deviceType`, `manufacturer`, `eleNum`, `groupId` | Save device identity to your device DAO | **Yes** — `IOT_CoreRespondDeviceCmd()` |
| 2 | `IOT_DEV_JOIN_PHASE_ELM_INFO` | `eid`, `numOfElms`, `elmList[]` (each: `elemId`, `elemType`, `numOfAttr`, `attrIdList`) | Save element list; initialize default attribute state in your device DAO for each element | **Yes** — `IOT_CoreRespondDeviceCmd()` |
| 3 | `IOT_DEV_JOIN_PHASE_SYNC_COMPLETE` | `eid`, `meshKey` (NULL if no mesh), `meshKeyLen` | If `meshKey != NULL`: decrypt and persist to your device DAO. `ctx.requiresResponse` is `false` for this phase | **No** |

> **Note on `attrIdList`:** The element info phase provides attribute IDs only. Default sizes and values come from the attribute definitions document shipped with your SDK package. Use those to initialize DAO state for each attribute on each element.

> **Note on `nwkAddr`:** For mesh-enabled hubs, `nwkAddr` is the device's mesh unicast address — store it in your device DAO for use when sending mesh commands.

### Code Example

```c
static void handleNewDeviceJoined(const IOT_CoreDeviceEventNewDeviceJoinedData_t *evt)
{
    switch (evt->phase)
    {
        case IOT_DEV_JOIN_PHASE_DEVICE_INFO:
        {
            // Save identity: devId, mac, macType, eid, rootEid, protocol,
            // deviceType, manufacturer, eleNum, groupId, nwkAddr
            // For mesh-enabled hubs, nwkAddr is the device's mesh unicast address —
            // store it in your device DAO for use when sending mesh commands.
            myDao_SaveDeviceIdentity(evt->data.deviceInfo.eid,
                                     evt->data.deviceInfo.devId,
                                     evt->data.deviceInfo.mac,
                                     evt->data.deviceInfo.macType,
                                     evt->data.deviceInfo.deviceType,
                                     evt->data.deviceInfo.manufacturer,
                                     evt->data.deviceInfo.eleNum,
                                     evt->data.deviceInfo.groupId,
                                     evt->data.deviceInfo.nwkAddr);

            IOT_CoreDeviceCmdResponse_t rsp = {
                .cmdType  = IOT_DEVICE_CMD_SYNC_DEV_JOINED,
                .cmdData2 = evt->cmdData2,
                .result   = IOT_OK,
                .ctx      = evt->ctx,
            };
            IOT_CoreRespondDeviceCmd(&rsp);
            break;
        }

        case IOT_DEV_JOIN_PHASE_ELM_INFO:
        {
            // Save element list and initialize default attribute state
            for (uint8_t i = 0; i < evt->data.elmInfo.numOfElms; i++)
            {
                const IOT_DevJoinElmInfo_t *elm = &evt->data.elmInfo.elmList[i];
                myDao_SaveElement(evt->data.elmInfo.eid, elm->elemId, elm->elemType,
                                  elm->numOfAttr, elm->attrIdList);
                myDao_InitDefaultAttrState(evt->data.elmInfo.eid, elm->elemId,
                                           elm->numOfAttr, elm->attrIdList);
            }

            IOT_CoreDeviceCmdResponse_t rsp = {
                .cmdType  = IOT_DEVICE_CMD_SYNC_DEV_JOINED,
                .cmdData2 = evt->cmdData2,
                .result   = IOT_OK,
                .ctx      = evt->ctx,
            };
            IOT_CoreRespondDeviceCmd(&rsp);
            break;
        }

        case IOT_DEV_JOIN_PHASE_SYNC_COMPLETE:
        {
            // Persist mesh key if present
            if (evt->data.syncComplete.meshKey != NULL && evt->data.syncComplete.meshKeyLen > 0)
            {
                myDao_SaveMeshKey(evt->data.syncComplete.eid,
                                  evt->data.syncComplete.meshKey,
                                  evt->data.syncComplete.meshKeyLen);
            }
            // ctx.requiresResponse is false — do NOT call IOT_CoreRespondDeviceCmd()
            break;
        }
    }
}
```

---

## 2. Device Removed

`DEV_EVENT_DEVICE_REMOVED` is fired when cloud or app commands a device to be removed.

**Two cases based on `isRootDevice`:**

- **`isRootDevice = true`** — The hub itself is being factory-reset. Wipe all SDK-owned DAOs (device registry, groups, smart automations, etc.), then call `IOT_CoreFactoryReset()`. This call does not return — iotcore wipes the core config, fires the lifecycle event, and restarts the device.

- **`isRootDevice = false`** — A specific sub-device is being removed. Remove it from your device DAO, clean up its smart automation entries (`IOT_DaoSmartRemoveDeviceData(eid)`), and remove its group memberships from your group DAO.

**Two entry paths — check `ctx.requiresResponse`:**

- **Cloud-commanded removal** (`ctx.requiresResponse = true`) — Call `IOT_CoreRespondDeviceCmd()` after DAO cleanup to acknowledge the command. For root-device removal, respond first, then call `IOT_CoreFactoryReset()`. For sub-device removal, respond and return.
- **App-initiated reset** (`ctx.requiresResponse = false`) — Your SDK's own factory-reset API triggered this. Do DAO cleanup only. `IOT_CoreFactoryReset()` restarts the device itself; no response is needed.

```c
static void handleDeviceRemoved(const IOT_CoreDeviceEventDeviceRemovedData_t *evt)
{
    if (evt->isRootDevice)
    {
        // Wipe all SDK-owned DAOs
        myDao_WipeAll();

        if (evt->ctx.requiresResponse)
        {
            IOT_CoreDeviceCmdResponse_t rsp = {
                .cmdType  = IOT_DEVICE_CMD_SYNC_DEV_REMOVED,
                .cmdData2 = evt->cmdData2,
                .result   = IOT_OK,
                .ctx      = evt->ctx,
            };
            IOT_CoreRespondDeviceCmd(&rsp);
        }

        IOT_CoreFactoryReset(); // Does not return
    }
    else
    {
        // Remove sub-device from device DAO
        myDao_RemoveDevice(evt->eid);
        // Clean up smart automations referencing this EID.
        // Your smart DAO cleanup should remove: all triggers monitoring this EID
        // (and their index entries), all bind commands targeting this EID, and
        // any schedule entries tied to this EID only.
        your_smart_dao_remove_device(evt->eid);
        // Remove from group DAO
        myGroupDao_RemoveDevice(evt->eid);

        if (evt->ctx.requiresResponse)
        {
            IOT_CoreDeviceCmdResponse_t rsp = {
                .cmdType  = IOT_DEVICE_CMD_SYNC_DEV_REMOVED,
                .cmdData2 = evt->cmdData2,
                .result   = IOT_OK,
                .ctx      = evt->ctx,
            };
            IOT_CoreRespondDeviceCmd(&rsp);
        }
    }
}
```

---

## 3. Group Bind

`DEV_EVENT_GROUP_BIND` is fired when cloud assigns a device to a group (or changes its group membership).

**`IOT_CoreDeviceEventGroupBindData_t` fields:**

| Field | Type | Description |
|-------|------|-------------|
| `eid` | `uint16_t` | Device being bound to the group |
| `newGroupId` | `uint16_t` | Group the device is joining |
| `oldGroupId` | `uint16_t` | Previous group (0 if none — BIND_GRP_ELMS path) |
| `elemData` | `uint8_t *` | Element bindings: array of 2-byte element IDs (NULL = all elements) |
| `elemDataLen` | `uint16_t` | Byte length of `elemData` (0 if all elements) |
| `ctx` | `IOT_RequestContext_t` | Routing context from the original command |
| `cmdData2` | `uint8_t` | Protocol metadata echoed by core |

**SDK actions:**

1. If `oldGroupId != 0`: remove `eid` from the old group in your group DAO first.
2. If `oldGroupId == 0` and `newGroupId > IOT_GROUP_EID_SECONDARY`: clear stale custom-group membership for this
   `eid` before adding the new one. This prevents a device from remaining in an outdated custom group.
3. If `newGroupId != 0`: save `eid` -> `newGroupId` membership with element bindings from `elemData`/`elemDataLen` to
   your group DAO.
4. Call `IOT_CoreRespondDeviceCmd()` after DAO work is complete. Pass `cmdData2` through unchanged.

`IOT_GROUP_EID_BASE` (`0xC000`) is the default group address. `IOT_GROUP_EID_SECONDARY` (`0xC001`) is the secondary
group address used by mesh provisioning. Custom groups are above `IOT_GROUP_EID_SECONDARY`.

The group DAO entry should store both the member EID and optional element binding data:

```text
[eid(2B)][elemDataLen(2B)][elemData(elemDataLen bytes)] ...
```

When `elemDataLen == 0`, group control should apply to all elements for that device. When `elemDataLen > 0`, the data is
an array of 2-byte element IDs and group control should apply only to those elements.

```c
static void handleGroupBind(const IOT_CoreDeviceEventGroupBindData_t *evt)
{
    // Remove from old group if applicable
    if (evt->oldGroupId != 0)
    {
        myGroupDao_RemoveMember(evt->oldGroupId, evt->eid);
    }
    else if (evt->newGroupId > IOT_GROUP_EID_SECONDARY)
    {
        myGroupDao_RemoveDeviceFromCustomGroups(evt->eid);
    }

    // Save to new group with element bindings
    if (evt->newGroupId != 0)
    {
        myGroupDao_AddMember(evt->newGroupId, evt->eid,
                             evt->elemData, evt->elemDataLen);
    }

    IOT_CoreDeviceCmdResponse_t rsp = {
        .cmdType  = (evt->elemDataLen > 0) ? IOT_DEVICE_CMD_BIND_GRP_ELMS : IOT_DEVICE_CMD_BIND_GRP,
        .cmdData2 = evt->cmdData2,
        .result   = IOT_OK,
        .ctx      = evt->ctx,
    };
    IOT_CoreRespondDeviceCmd(&rsp);
}
```

---

## 4. Group Unbind

`DEV_EVENT_GROUP_UNBIND` is fired when cloud removes a device from a group.

**`IOT_CoreDeviceEventGroupUnbindData_t` fields:** `eid`, `groupId`, `ctx`.

**SDK actions:**

1. Remove `eid` from `groupId` in your group DAO.
2. Call `IOT_CoreRespondDeviceCmd()` after DAO work is complete. Pass `cmdData2` through unchanged.

```c
static void handleGroupUnbind(const IOT_CoreDeviceEventGroupUnbindData_t *evt)
{
    myGroupDao_RemoveMember(evt->groupId, evt->eid);

    IOT_CoreDeviceCmdResponse_t rsp = {
        .cmdType  = IOT_DEVICE_CMD_UNBOUND_GRP,
        .cmdData2 = evt->cmdData2,
        .result   = IOT_OK,
        .ctx      = evt->ctx,
    };
    IOT_CoreRespondDeviceCmd(&rsp);
}
```

Your SDK should also remove a sub-device from all groups when that sub-device is removed. If your SDK supports hubs,
make group cleanup part of the device-removal flow so later group control does not try to address a stale EID.

---

## 5. Device Attribute Set

`DEV_EVENT_DEVICE_ATTR_SET` is fired when cloud requests a sub-device attribute update.

**`IOT_CoreDeviceEventAttrSetData_t` fields:** `eid`, `elm`, `attrId`, `attrData`, `attrDataLen`, `ctx`.

**SDK actions:**

1. Persist the attribute value in your device/state DAO.
2. Call `IOT_CoreRespondDeviceCmd()` with `cmdType = IOT_DEVICE_CMD_DEVICE_ATTR` after DAO work is complete.

```c
static void handleDeviceAttrSet(const IOT_CoreDeviceEventAttrSetData_t *evt)
{
    myDeviceDao_SetAttribute(evt->eid, evt->elm, evt->attrId,
                             evt->attrData, evt->attrDataLen);

    IOT_CoreDeviceCmdResponse_t rsp = {
        .cmdType  = IOT_DEVICE_CMD_DEVICE_ATTR,
        .cmdData2 = 0,
        .result   = IOT_OK,
        .ctx      = evt->ctx,
    };
    IOT_CoreRespondDeviceCmd(&rsp);
}
```

---

## 5. Device Status Request

`DEV_EVENT_DEVICE_STATUS_REQUEST` is a GET — cloud is querying the hub's health and capability data.

**SDK actions:**

1. Read current system stats (firmware version, uptime, memory usage, protocol flags).
2. Build and send the report via `IOT_CoreRequestReportDeviceStatus()`.

> **Do NOT call `IOT_CoreRespondDeviceCmd()` for this event.** It is a query, not a command — the status report itself is the response.

```c
static void handleDeviceStatusRequest(void)
{
    IOT_CoreDeviceStatusData_t status = {
        .firmwareVersion    = MY_FIRMWARE_VERSION_STRING,
        .uptimeSeconds      = mySystem_GetUptimeSeconds(),
        .lastActivationTime = myDao_GetLastActivationTime(),
        .ramUsagePercent    = mySystem_GetRamUsagePercent(),
        .nvsUsagePercent    = mySystem_GetNvsUsagePercent(),
        .totalRamKB         = mySystem_GetTotalRamKB(),
        .totalNvsKB         = mySystem_GetTotalNvsKB(),
        // protocolFlags[0] = mode, [1] = mesh, [2] = zigbee, [3] = matter
        .protocolFlags      = { 0x00, CONFIG_IOT_MESH_ENABLED ? 0x01 : 0x00, 0x00, 0x00, 0x00, 0x00 },
    };
    IOT_CoreRequestReportDeviceStatus(&status);
}
```

---

## Putting It Together

Register your device event handler during SDK init:

```c
static void myDeviceEventHandler(void *arg, const IOT_CoreEvent_t *event)
{
    if (event->category != IOT_EVENT_DEVICE) return;

    const IOT_CoreDeviceEvent_t *devEvt = &event->evt.device;

    switch (devEvt->type)
    {
        case DEV_EVENT_NEW_DEVICE_JOINED:
            handleNewDeviceJoined(&devEvt->data.newDeviceJoined);
            break;
        case DEV_EVENT_DEVICE_REMOVED:
            handleDeviceRemoved(&devEvt->data.deviceRemoved);
            break;
        case DEV_EVENT_GROUP_BIND:
            handleGroupBind(&devEvt->data.groupBind);
            break;
        case DEV_EVENT_GROUP_UNBIND:
            handleGroupUnbind(&devEvt->data.groupUnbind);
            break;
        case DEV_EVENT_DEVICE_STATUS_REQUEST:
            handleDeviceStatusRequest();
            break;
        default:
            break;
    }
}

// In your SDK init:
IOT_DevCoreRegisterEventCb(IOT_EVENT_DEVICE, myDeviceEventHandler, NULL);
```

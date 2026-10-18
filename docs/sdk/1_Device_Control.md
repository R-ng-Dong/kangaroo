# Device Control Guide

**SDK Writer Guide concepts:** Core Event Contract, SDK-Owned Responsibilities.

This guide explains how your SDK handles device state commands from the cloud. The core engine parses incoming protocol
messages and dispatches structured events to your SDK via registered callbacks. Your SDK is responsible for resolving
targets, updating your DAO, actuating hardware, and reporting state back to the cloud through the reporter API.

---

## 1. STATE_SET: The Core Control Loop

When the cloud sends a control command, iotcore dispatches an `IOT_CoreStateEvent_t` with `type == IOT_EVENT_TYPE_STATE_SET`. The event data is in `event->evt.state.data.setRequest` (an `IOT_EventStateSetRequest_t`).

### Target Types

The `setRequest.targetType` field tells your SDK how to resolve which device(s) to control:

| `targetType` | Meaning | How to resolve |
|---|---|---|
| `IOT_SET_TARGET_DEVICE` | Single device | Use `setRequest.target.device.eid` directly |
| `IOT_SET_TARGET_GROUP` | Group address (≥ `IOT_GROUP_EID_BASE` = `0xC000`) | Use `setRequest.target.group.groupEid`; look up member EIDs from your group DAO |
| `IOT_SET_TARGET_VOICE` | Voice assistant path (Alexa, Google, etc.) | Use `setRequest.target.voice.devId` (12 bytes); resolve to EID via your device DAO. If `target.voice.groupEid > 0`, a group override applies — expand that group as well |

> **Note:** `setRequest.devSetInfo.eid` is **not used** for SET events. Always resolve targets through `targetType` and the `target` union.

### Location Control Routing vs Target Resolution

Cloud traffic may arrive through either a device command topic or a location command topic. iotcore hides that transport
detail and dispatches the same `IOT_EVENT_TYPE_STATE_SET` event to your SDK. There is no separate "location target"
enum value.

For SDK code, the rule is:

1. Use `setRequest.targetType` and `setRequest.target` to decide which device EID(s) to control.
2. Treat `setRequest.locationId` / `locationIdLen` as routing metadata from core. Do not use it to choose devices.
3. Pass `setRequest.ctx` through unchanged when reporting state or responding.

If a location-routed message contains multiple device/group control blocks, iotcore dispatches them as normal SET events.
Your SDK should handle each event the same way it handles direct device or group control.

### Device Type Filter

`setRequest.targetDeviceType` is a device type filter. If non-zero, apply the command only to devices of that type (as stored in your device DAO). If zero, apply to all resolved target devices.

### Group Control Flow

Your SDK should handle group control by expanding the group before doing normal per-device SET processing:

1. Read `setRequest.target.group.groupEid`.
2. Load the group member list from the SDK-owned group DAO.
3. If the group is unknown or empty, ignore the SET as a no-op for this device.
4. For each group member, resolve which elements should be controlled.
5. Run the same per-device flow used for normal control: update device DAO, actuate local hardware or dispatch to the
   sub-device protocol, build full state, and report changed state.
6. Batch all successfully processed devices into one `IOT_CoreRequestReportStateChanged()` call when possible.

Group element resolution is important. A group member may have element bindings saved from `DEV_EVENT_GROUP_BIND`:

| Group member data | Elements to control |
|---|---|
| `elemDataLen > 0` | Treat `elemData` as `[elemId(2B)]...` and control only those elements |
| `elemDataLen == 0` | No element binding was provided; control all elements registered for that EID in the device DAO |

This means group control does not trust `devSetInfo.elemList` as the final per-device element list. For group SET events,
the SDK expands the group first, then builds a per-device element list from the group DAO and device DAO.

### Per-EID Processing Loop

Once you have a list of resolved EIDs, process each one:

```c
static void handle_state_set(const IOT_EventStateSetRequest_t *setData)
{
    // Resolve EID list from targetType (see section above)
    uint16_t eidList[MAX_DEVICES];
    uint8_t eidCount = 0;
    resolve_target_eids(setData, eidList, &eidCount);

    for (uint8_t i = 0; i < eidCount; i++) {
        uint16_t eid = eidList[i];
        const dev_set_info_t *info = &setData->devSetInfo;

        // 1. Update DAO state — iterate each element in the SET command
        for (int j = 0; j < info->numOfElems; j++) {
            uint16_t elemId = info->elemList[j].elemId;
            iot_err_t daoResult = your_dao_set_attr(eid, elemId, info->attrId,
                                                    info->attrValue, info->attrSize);
            if (daoResult != IOT_OK) {
                // Log warning — state may be stale. Continue to report.
            }
        }

        // 2. Actuate hardware or route to sub-device
        if (is_root_device_eid(eid)) {
            your_hardware_actuate(eid, info);   // GPIO, PWM, etc.
        } else {
            your_protocol_dispatch(eid, info);  // Mesh, IR, etc.
        }

        // 3. Report state change to cloud
        // Collect changed element IDs for the report
        uint16_t changedIds[info->numOfElems];
        for (int j = 0; j < info->numOfElems; j++) {
            changedIds[j] = info->elemList[j].elemId;
        }
        IOT_CoreStateChangeDeviceData_t devData = {
            .eid            = eid,
            .mac            = /* from device DAO */,
            .macLen         = 6,
            .devId          = /* from device DAO */,
            .devIdLen       = /* from device DAO */,
            .fullState      = your_dao_get_full_state(eid),
            .numChangedElems = info->numOfElems,
            .changedElemIds = changedIds,
            .changedAttrId  = info->attrId,
            // Set pushNotify = true only for safety/alert events (smoke detection,
            // tamper, low battery). For normal state changes, leave it false to
            // avoid spamming push notifications.
            .pushNotify     = false,
        };
        IOT_CoreStateChangeReport_t report = {
            .devices    = &devData,
            .numDevices = 1,
            .ctx        = setData->ctx,   // pass through unchanged
        };
        IOT_CoreRequestReportStateChanged(&report);
    }
}
```

**Key rules:**
- Always pass `.ctx = setData->ctx` through to the reporter — never unpack or rebuild it.
- Call `IOT_CoreRequestReportStateChanged()` once per device per SET command. For group commands affecting multiple devices, you can batch them into a single call by filling the `devices` array with all affected devices and setting `numDevices` accordingly.
- For group control, skip members whose elements cannot be resolved, but continue processing the other group members.
- After state reporting, evaluate smart triggers for each affected EID unless the sender is already automation/schedule/voice control. This avoids trigger feedback loops.

---

## 2. STATE_GET

When the cloud queries current device state, iotcore dispatches `IOT_EVENT_TYPE_STATE_GET`. The event data is in `event->evt.state.data.getRequest` (an `IOT_EventStateGetRequest_t`).

```c
static iot_err_t handle_state_get(const IOT_EventStateGetRequest_t *getReq)
{
    devStateElemsInfo_t *fullState = your_dao_get_full_state(getReq->eid);
    if (fullState == NULL) {
        // iotcore will send an error response to cloud — do not call any reporter
        return IOT_ERR_DATA_NOT_FOUND;
    }

    IOT_CoreStateResponse_t response = {
        .eid       = getReq->eid,
        .fullState = fullState,
        .ctx       = getReq->ctx,      // pass through unchanged
        .unixTime  = getReq->unixTime, // pass through — NULL if not requested
    };
    IOT_CoreRequestReportState(&response);
    return IOT_OK;
}
```

No DAO writes and no hardware actuation — this is a read-only operation.

---

## 3. STATE_SENSOR_LOG

When the cloud requests a sensor log for a specific attribute, iotcore dispatches `IOT_EVENT_TYPE_STATE_SENSOR_LOG`. The event data is in `event->evt.state.data.sensorLogRequest` (an `IOT_EventStateSensorLogRequest_t`).

Available fields: `eid`, `elemId`, `attrId`, `year`, `dayOfYear`.

```c
static iot_err_t handle_sensor_log(const IOT_EventStateSensorLogRequest_t *logReq)
{
    uint8_t *logData    = NULL;
    uint16_t logDataLen = 0;

    iot_err_t result = your_dao_read_sensor_log(
        logReq->eid, logReq->elemId, logReq->attrId,
        logReq->year, logReq->dayOfYear,
        &logData, &logDataLen
    );

    if (result != IOT_OK) {
        // iotcore will send an error response to cloud — do not call any reporter
        return IOT_ERR_DATA_NOT_FOUND;
    }

    // Note: if the query is valid but no entries exist for that date, set
    // logDataLen = 0 and still call the reporter below — this is NOT an error,
    // just an empty result. Only return IOT_ERR_DATA_NOT_FOUND above if the
    // DAO lookup itself fails (device not found, invalid parameters).

    IOT_CoreSensorLogResponse_t response = {
        .eid        = logReq->eid,
        .elemId     = logReq->elemId,
        .attrId     = logReq->attrId,
        .year       = logReq->year,
        .dayOfYear  = logReq->dayOfYear,
        .logData    = logData,
        .logDataLen = logDataLen,
        .ctx        = logReq->ctx,     // pass through unchanged
    };
    IOT_CoreRequestSensorLogResponse(&response);
    return IOT_OK;
}
```

---

## 4. Reporter API: Which to Call

| Situation | Reporter call |
|---|---|
| Response to `IOT_EVENT_TYPE_STATE_GET` | `IOT_CoreRequestReportState()` |
| State changed by a SET command | `IOT_CoreRequestReportStateChanged()` |
| Response to `IOT_EVENT_TYPE_STATE_SENSOR_LOG` | `IOT_CoreRequestSensorLogResponse()` |
| Local state change (button press, sensor reading, timer) | Your SDK's local state update function — updates DAO, then calls `IOT_CoreRequestReportStateChanged()` with a local context (see Section 5) |

Every reporter call must pass the original `ctx` from the event as-is (`.ctx = event->ctx`). The context carries transport routing, sender identity, transaction ID, and request correlation info — the SDK must not unpack or reconstruct it.

---

## 5. Handling Local State Changes (Button Presses, Sensors)

When hardware state changes locally — a physical button is pressed, a sensor threshold is crossed, or a timer fires — your SDK must both update the DAO **and** send a state report to the cloud. iotcore does not know about these changes unless you report them.

Implement a local state update function in your SDK. The pattern:

```c
// Called from your button ISR handler, sensor polling task, etc.
void your_sdk_local_state_update(uint16_t eid, uint16_t elemId,
                                 uint16_t attrId, uint8_t *value, uint16_t valueSize)
{
    // 1. Update DAO
    your_dao_set_attr_single(eid, elemId, attrId, value, valueSize);

    // 2. Build local context — senderFrom=0, senderId=NULL for locally-initiated changes
    IOT_RequestContext_t localCtx = { 0 };

    // 3. Report to cloud
    IOT_CoreStateChangeDeviceData_t devData = {
        .eid             = eid,
        .mac             = /* from device DAO */,
        .macLen          = 6,
        .devId           = /* from device DAO */,
        .devIdLen        = /* from device DAO */,
        .fullState       = your_dao_get_full_state(eid),
        .numChangedElems = 1,
        .changedElemIds  = &elemId,
        .changedAttrId   = attrId,
        .pushNotify      = false,
    };
    IOT_CoreStateChangeReport_t report = {
        .devices    = &devData,
        .numDevices = 1,
        .ctx        = localCtx,
    };
    IOT_CoreRequestReportStateChanged(&report);
}
```

> **Important:** Do NOT call your local state update function from inside a SET event handler. The DAO was already updated in the SET handler, and calling it again will double-write the DAO and may trigger a re-dispatch loop. The SET handler calls `IOT_CoreRequestReportStateChanged()` directly with the cloud's `ctx`.

---

## 6. Failure Handling

| Scenario | Action |
|---|---|
| DAO write fails on SET | Log a warning. Continue — still send the state report. The cloud receives the last-known value. Correct when the DAO is available again. |
| Hardware actuation fails | Log a warning. The DAO was already updated, so send the report as-is. If you can read the actual current hardware state, update the DAO with the true value and send a corrective report immediately after. |
| `STATE_GET` — DAO has no data for requested EID | Return `IOT_ERR_DATA_NOT_FOUND` without calling any reporter. iotcore automatically sends an error response to the cloud. |
| Sensor log not found for requested query params | Return `IOT_ERR_DATA_NOT_FOUND` without calling any reporter. iotcore automatically sends an error response to the cloud. |

---

## Event Handler Registration

Register your state event handler during SDK init:

```c
static void sdk_state_event_cb(void *arg, const IOT_CoreEvent_t *event)
{
    if (event->category != IOT_EVENT_STATE) return;

    const IOT_CoreStateEvent_t *stateEvent = &event->evt.state;
    switch (stateEvent->type) {
        case IOT_EVENT_TYPE_STATE_SET:
            handle_state_set(&stateEvent->data.setRequest);
            break;
        case IOT_EVENT_TYPE_STATE_GET:
            handle_state_get(&stateEvent->data.getRequest);
            break;
        case IOT_EVENT_TYPE_STATE_SENSOR_LOG:
            handle_sensor_log(&stateEvent->data.sensorLogRequest);
            break;
    }
}

// In your IOT_SDKInit():
IOT_DevCoreRegisterEventCb(IOT_EVENT_STATE, sdk_state_event_cb, NULL);
```

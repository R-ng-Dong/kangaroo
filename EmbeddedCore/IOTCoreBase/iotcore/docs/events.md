# Event reference

Every event `iotcore` raises, one section each: when it comes, on which task, what it carries and
what core expects back. [concepts.md](concepts.md#the-event-flow) explains the model behind these
answers; [reference.md](reference.md) lists every type and function by header. A check in our
build ties this page to the event headers: an event without its own section, or a section without
all four labels, fails it.

On path A (`sdkcontract`) several of these never reach your `onCoreEvent`, because the contract
takes them; each section says so, and
[sdkcontract's core-events page](../../sdkcontract/docs/how-to/core-events.md) has the whole table.

## How each event is described

- **When:** the moment core raises it, and what does not raise it.
- **Task:** which task your handler runs on. There are three kinds:
  - *Core's task, in order.* Lifecycle events (setup, network, cloud, WiFi recovery, Zigbee, WiFi
    changed) are queued and delivered one at a time on core's own task, in the order core raised
    them. A burst is never reordered; if you stall the handler, later events wait.
  - *Core's task, as part of a request.* A command or question from the cloud or the app (state,
    smart, IR, mesh key, identify, reboot, update, service message, group and device commands) is
    handled and delivered synchronously on core's task. The next request waits for your handler, so
    return quickly and hand slow work to your own task.
  - *The calling task.* An event caused by your own call into core (`IOT_CoreInit()`,
    `IOT_CoreFactoryReset()`, `IOT_CoreDispatchEvent()`) comes on the task that made the call,
    before the call returns. Your handlers therefore run on more than one task: guard state they
    share.
- **Data:** what the event carries. Every pointer in an event (`ctx.senderId`, `ctx.requestId`,
  payload buffers, key bytes, lists) is *borrowed*: it is valid only until your handler returns.
  Copy what you keep, including `ctx.senderId` and `ctx.requestId` if you answer later from
  another task. Arrays inside the event struct itself (the WiFi SSID) are part of the event and
  need no copy, but still last only for the call.
- **You answer:** what core expects back. *Nothing* for a notice. A *request* has a `result` (or a
  `claim`) that core presets and reads when your handler returns; the sender is told success for
  `IOT_OK` and failure with the code otherwise. The preset is "not supported"
  (`IOT_ERR_NOT_SUPPORTED`) for every request except the mesh key, so leaving it alone, or
  registering no handler for the category, tells the sender the request is not supported. Some
  requests are instead answered with a call into core (an `IOT_CoreRespond*()` or report function)
  made before you return. Either way a request is answered once.

---

## STATE Events

### IOT_EVENT_TYPE_STATE_SET

**When:** the cloud, the app, a voice assistant or a local automation sends a SET state command
(e.g., turn on the light).
**Task:** core's task, as part of a request (an automation's own action is dispatched on the task
that runs it).
**Data:** `data.setRequest` (`IOT_EventStateSetRequest_t`): `targetType` and the matching `target`,
`devSetInfo` (elements, attribute id, value), `ctx` and `targetDeviceType`. `devSetInfo.elemList`,
`devSetInfo.attrValue`, `target.voice.devId` and the `ctx` pointers are borrowed: valid only until
you return.
**You answer:** `setData->result`, when the sender asked for an acknowledgement (see
"Your answer: `result`" below); on path A the contract writes it for you.

When a SET state command is received (e.g., turn on light), your SDK must (the code is in
[how-to/handle-state.md](how-to/handle-state.md)):

1. **Resolve target** — determine which device(s) to control based on `targetType`
2. **Accept or refuse** — a sync-on-off mirror is acted on only from a pairing this device holds ([Smart rulebook](../../sdkcontract/docs/smart.md), rule 5); otherwise write `result = IOT_OK` and do nothing more: a refusal is normal, not an error
3. **For each device**: actuate hardware, write your Device store, record the change, send the report, then feed trigger evaluation unless the report could not read the device — all with the cloud's element ids and the request's sender (Record and Smart rulebooks)
4. **Answer** — write `setData->result` before returning (below)

#### Your answer: `result`

A state command the sender acknowledged waits for an answer, and the answer is yours. Core presets
`setData->result` to `IOT_ERR_NOT_SUPPORTED`, dispatches synchronously and reads it when your
handler returns: the sender is told success for `IOT_OK` and failure with the code otherwise.
Write `IOT_OK` once the command is carried out (or deliberately refused), or the error that stopped
it. Leaving it untouched, or registering no `IOT_EVENT_STATE` handler, tells the sender the command
is not supported. An unacknowledged command (`ctx.requiresResponse` false) gets no answer whatever
you write.

With `sdkcontract` (path A) you never see this event: the contract runs the command and writes
`result` from its own handling. A command for one device that moved nothing answers your `apply`'s
error; a group or location command answers success, since its members that are not on this device
are expected to do nothing here.

### IOT_EVENT_TYPE_STATE_GET

**When:** the cloud or the app asks for the state of one device.
**Task:** core's task, as part of a request.
**Data:** `data.getRequest` (`IOT_EventStateGetRequest_t`): `eid`, `ctx`, and `unixTime` (the
caller's time, or NULL when it asked for no time sync). `unixTime` and the `ctx` pointers are
borrowed: valid only until you return.
**You answer:** with a call, before you return: `IOT_CoreRequestReportState()`, as below. There is
no `result` to write.

When a GET state request is received, build a **response** using `IOT_CoreReporter.h`:

```c
// 1. Get device state from DAO
iot_err = IOT_DaoDeviceGetDeviceElemsState(getData->eid, &elemsStateInfo);

// 2. Build response struct and send
IOT_CoreStateResponse_t response = {
    .eid = getData->eid,
    .fullState = &elemsStateInfo,
    .ctx = getData->ctx,             // pass-through routing context
    .unixTime = getData->unixTime,   // NULL if no time sync requested
};

iot_err = IOT_CoreRequestReportState(&response);
```

Core handles: message building, time sync, connectivity attribute, and sending.

### IOT_EVENT_TYPE_STATE_SENSOR_LOG

**When:** the app asks for one day of the legacy sensor history (a year and a day of the year for
one attribute of one element).
**Task:** core's task, as part of a request.
**Data:** `data.sensorLogRequest` (`IOT_EventStateSensorLogRequest_t`): `eid`, `elemId`, `attrId`,
`year`, `dayOfYear` and `ctx`. The `ctx` pointers are borrowed: valid only until you return.
**You answer:** with a call, before you return: `IOT_CoreRequestSensorLogResponse()`
(`IOT_CoreSensorLog.h`). No `result`. The shipped library does not include this legacy history:
the call sends nothing and returns `IOT_ERR_NOT_SUPPORTED`, so the app's query times out. Keep
history with Record (`IOT_EVENT_TYPE_STATE_RECORD`) instead. On path A the contract answers it with an empty log unless
you installed a custom history store, in which case it arrives at your `onCoreEvent`. The
current history is `IOT_EVENT_TYPE_STATE_RECORD` below.

### IOT_EVENT_TYPE_STATE_RECORD

**When:** the app asks for one block of Record history (the [Record rulebook](../../sdkcontract/docs/record.md) describes the
checkpoint chain it walks).
**Task:** core's task, as part of a request.
**Data:** `data.recordRequest` (`IOT_EventStateRecordRequest_t`): `eid`, `elemId`, `attrId`,
`checkpoint` (epoch milliseconds of the block asked for, or 0 for the current one) and `ctx`.
`ctx.senderId` points into the message reader's buffer and is freed when your handler returns.
**You answer:** with a call, before you return: `IOT_CoreRequestRecordResponse()`
(`IOT_CoreRecord.h`), synchronously. No `result`. On path A the contract answers it.

---

## DEVICE Events

### DEV_EVENT_NEW_DEVICE_JOINED

**When:** a new device (a hub's sub-device) joins; the event is raised once per phase, in order.
**Task:** core's task, as part of a request.
**Data:** `data.newDeviceJoined` (`IOT_CoreDeviceEventNewDeviceJoinedData_t`): `phase`, `ctx`,
`cmdData2`, and the phase's data (`deviceInfo`, `elmInfo` or `syncComplete`). `elmInfo.elmList`
(and the `attrIdList` in it) and `syncComplete.meshKey` are borrowed: valid only until you return.
**You answer:** for the DEVICE_INFO and ELM_INFO phases, `IOT_CoreRespondDeviceCmd()` with
`cmdType = IOT_DEVICE_CMD_SYNC_DEV_JOINED` and your result, before you return; `ctx.requiresResponse`
is false for SYNC_COMPLETE, so there is nothing to answer there. On path A the contract does this
and calls `onDeviceJoined` instead.

Triggered in phases when a new device joins the network; what to store in each phase is in
[how-to/hub-and-sub-devices.md](how-to/hub-and-sub-devices.md#a-device-joins).

### DEV_EVENT_PROVISION_STARTED / _CANCELLED / _FAILED / _COMPLETED

**When:** the four moments of a setup, described below. A provisioned device never raises them.
**Task:** core's task, in order.
**Data:** none.
**You answer:** nothing.

A setup starts when a phone connects over BLE to an unprovisioned device (or, for a setup over the
local network with no phone on BLE, when it asks the device to reach for the cloud), and every
`DEV_EVENT_PROVISION_STARTED` ends in exactly one of the other three, so a "being set up"
indication you show from STARTED always has an end. A provisioned device never raises them. All four
are delivered on core's task, carry no data and need no answer.

- **STARTED**: a phone connected to set the device up. Show "being set up" (for example a blink).
  A wrong WiFi password does not end the setup: the phone is told, and the user can retype on the
  same link.
- **CANCELLED**: the phone left before the setup finished: before sending the WiFi details, while
  the device was still joining with them (after a wrong password too), or right after connecting
  without sending anything. Raised at once. A phone that hangs up while the device is waiting for
  the cloud is routine and raises nothing; FAILED or COMPLETED ends that setup.
- **FAILED**: nothing worked within the setup window: 5 minutes after the last WiFi details the
  phone sent (the join never succeeded), or the cloud never came. The device is not set up. Core
  stops the cloud connection, drops the phone's BLE link and puts the setup advert back, so a retry
  is a new setup (a new STARTED). A cloud connect after FAILED never completes it.
- **COMPLETED**: success only. The device is provisioned: `IOT_CoreIsProvisioned()` is already true
  inside it, and it comes before the `DEV_EVENT_CLOUD_CONNECTED` of the session that completed it.
  Element attrs are already initialized during `DEV_EVENT_NEW_DEVICE_JOINED`. No additional DAO
  restructuring needed. Start anything that must wait for setup here (a hub transport, for example).

```c
case DEV_EVENT_PROVISION_STARTED:
    led_show_setup();
    break;
case DEV_EVENT_PROVISION_CANCELLED:
case DEV_EVENT_PROVISION_FAILED:
    led_show_unprovisioned();
    break;
case DEV_EVENT_PROVISION_COMPLETED:
    led_show_provisioned();
    break;
```

On path A the contract's device status follows the same events: `IOT_CONTRACT_STATUS_PAIRING`
from STARTED until the setup ends; after COMPLETED it holds until the first cloud session is up, so
the status goes straight from pairing to online.

### DEV_EVENT_DEVICE_REMOVED

**When:** the cloud removed a device from this one: a sub-device, or this device itself (the root).
Only the cloud's removal command raises it; a local factory reset is `DEV_EVENT_FACTORY_RESET`.
**Task:** core's task, as part of a request.
**Data:** `data.deviceRemoved`: `eid`, `isRootDevice`, `ctx`, `cmdData2`. The `ctx` pointers are
borrowed. For the root device core has already wiped its own identity (provision state, WiFi, MQTT
credentials) and, when the sender asked for an answer, already answered the cloud, so
`ctx.requiresResponse` is false and there is no reply left to send for it.
**You answer:** for a sub-device, `IOT_CoreRespondDeviceCmd()` with
`cmdType = IOT_DEVICE_CMD_SYNC_DEV_REMOVED` when `ctx.requiresResponse` is set. On path A the contract
removes the device from every store, answers, and calls `onDeviceRemoving` / `onDeviceRemoved`.

Clean up device data from storage (path B; on path A the contract does it and calls
`onDeviceRemoving` before and `onDeviceRemoved` after, where you restart for a root removal, or
call `IOT_ContractRemovalConfigure(true)` before `IOT_ContractStart()` and the contract restarts
once `onDeviceRemoved` returns):

```c
if (isRootDevice) {
    // This device was removed by the cloud: wipe every store you own.
    // Provision state was already reset by iotcore.
    my_stores_wipe_all();
    // ...answer the request, then come back up clean:
    IOT_ApplicationRestartChip();
} else {
    // A sub-device is gone: forget it in every store (Device, Group, Smart, Record, IR).
    // Anything that needs its record (e.g. retiring a mesh node) runs first.
    my_stores_remove_device(eid);
}
```

### DEV_EVENT_FACTORY_RESET

**When:** your product called `IOT_CoreFactoryReset()`; never the cloud.
**Task:** the calling task, before `IOT_CoreFactoryReset()` returns.
**Data:** `data.factoryReset`: `wasProvisioned` and `rootEid` (this device's EID before the wipe;
0 only when it had none).
**You answer:** nothing to core. You wipe what you own and restart.

Raised by `IOT_CoreFactoryReset()`, never by the cloud. Core has already wiped its own identity
(provision state, WiFi, MQTT credentials); the event is delivered on the calling task **before
`IOT_CoreFactoryReset()` returns**, so the reset call is where you wipe and restart. It is not
`DEV_EVENT_DEVICE_REMOVED`, which only the cloud's removal command raises, and no cloud response is
owed. `data.factoryReset.wasProvisioned` says whether the device was provisioned when the reset
began (the reset is performed in full either way).

```c
case DEV_EVENT_FACTORY_RESET:
    // Path B: wipe every store you own, then come back up clean.
    // On path A the contract wipes its stores and calls onDeviceRemoving / onDeviceRemoved
    // (root == true) as for a root removal; restart there, or let
    // IOT_ContractRemovalConfigure(true) restart for you.
    my_stores_wipe_all();
    IOT_ApplicationRestartChip();
    break;
```

### DEV_EVENT_GROUP_BIND / DEV_EVENT_GROUP_UNBIND

**When:** the cloud binds a device (or some of its elements) to a group, or takes it out of one.
**Task:** core's task, as part of a request.
**Data:** `data.groupBind` (`eid`, `newGroupId`, `oldGroupId` (0 when there was none), `elemData` /
`elemDataLen` (element bindings, NULL when the bind covers every element), `ctx`, `cmdData2`) or
`data.groupUnbind` (`eid`, `groupId`, `ctx`, `cmdData2`). `elemData` and the `ctx` pointers are
borrowed: valid only until you return.
**You answer:** update your group store, then call `IOT_CoreRespondDeviceCmd()` with
`cmdType = IOT_DEVICE_CMD_BIND_GRP` (or `_BIND_GRP_ELMS` when `elemData` is present) or
`IOT_DEVICE_CMD_UNBOUND_GRP`, passing `cmdData2` and `ctx` through, when `ctx.requiresResponse` is
set. On path A the contract updates its Group store and answers.

### DEV_EVENT_DEVICE_ATTR_SET

**When:** the cloud sets an attribute directly on a device, outside a state command.
**Task:** core's task, as part of a request.
**Data:** `data.attrSet`: `eid`, `elm`, `attrId`, `attrData` / `attrDataLen`, `ctx`. `attrData` and
the `ctx` pointers are borrowed: valid only until you return.
**You answer:** store the value (and actuate if it is this device's own), then
`IOT_CoreRespondDeviceCmd()` with `cmdType = IOT_DEVICE_CMD_DEVICE_ATTR` when `ctx.requiresResponse`
is set. On path A the contract stores it, drives `apply` for the device itself, and answers.

### DEV_EVENT_DEVICE_STATUS_REQUEST

**When:** the cloud or the app asks for this device's status, including a phone over BLE during setup.
**Task:** core's task, as part of a request.
**Data:** `data.deviceStatusRequest`: `ctx` and `result`. The `ctx` pointers are borrowed: valid only
until you return.
**You answer:** `IOT_CoreRespondDeviceStatus(&req->ctx, &status)` before you return, and write its
return into `req->result` (preset to `IOT_ERR_NOT_SUPPORTED`). The reply is the answer; core sends
none of its own on success. See [how-to/device-status.md](how-to/device-status.md).
On path A this arrives at your `onCoreEvent`, untouched.

### DEV_EVENT_ZIGBEE_DEVICE_JOINING / _INTERVIEWED / _UNKNOWN_MODEL / _LEFT

**When:** a hub with a Zigbee coordinator (`CONFIG_IOT_ZIGBEE_ENABLED`) sees a Zigbee device join the
network (JOINING, the interview still pending), finish its interview with a known model
(INTERVIEWED) or an unknown one (UNKNOWN_MODEL), or leave the network (LEFT). The device has no EID
yet at this stage: the cloud assigns it later.
**Task:** core's task, in order.
**Data:** `data.zigbeeJoining` (`ieeeAddr`, `friendlyName`), `data.zigbeeInterviewed` (`ieeeAddr`,
`friendlyName`, `model`, `vendor`; used for UNKNOWN_MODEL too) or `data.zigbeeLeft` (`ieeeAddr`).
All are fixed arrays inside the event, valid for the call.
**You answer:** nothing. Register the device with the cloud through your own hub logic.

### DEV_EVENT_WIFI_RECOVERY_STARTED / _ENDED

**When:** the WiFi recovery advert went on the air (STARTED) and is off the air again (ENDED).
**Task:** core's task, in order.
**Data:** none.
**You answer:** nothing.

When a provisioned device loses its WiFi for good (several reconnect attempts in a row fail, not a
roam or a brief drop), core puts a recovery advert on the air so the installer's app can hand the
device new WiFi credentials. These two events bracket that advert.

- **STARTED** is raised when the advert actually went up. A start that was refused raises nothing.
  It is raised once per outage: a phone that connects and leaves while the outage lasts does not
  raise it again.
- **ENDED** is raised once, when the advert is no longer on the air: the cloud has been stable again
  (after a short grace period), or the advert could not be kept up after a client left. It is raised
  only for an outage STARTED announced.
- Both are delivered on core's task, carry no data, and need no answer. Neither is raised for an
  unprovisioned device (it advertises for provisioning instead) or when you stop the advert yourself.

```c
case DEV_EVENT_WIFI_RECOVERY_STARTED:
    // Show "needs new WiFi", e.g. a distinct LED pattern.
    show_needs_wifi(true);
    break;
case DEV_EVENT_WIFI_RECOVERY_ENDED:
    show_needs_wifi(false);   // back to the normal status display
    break;
```

### DEV_EVENT_CLOUD_CONNECTING / _CONNECTED / _DISCONNECTED / _CONNECT_FAILED

**When:** the four moments of the device's cloud session, in the table below.
**Task:** core's task, in order.
**Data:** none.
**You answer:** nothing.

The device's cloud session. All four carry no data, need no answer, and are delivered on core's
task.

| Event | Means |
|---|---|
| `DEV_EVENT_CLOUD_CONNECTING` | Core started a cloud connect attempt. Sent once per attempt, on every path: after the network comes up, on a retry, on `IOT_CoreCloudConnect()`, during setup, and for each reconnect core makes by itself after a drop or a failed attempt |
| `DEV_EVENT_CLOUD_CONNECTED` | The session is up **and ready**: `IOT_CoreIsCloudConnected()` is already true inside the event, so a report or message sent from it is accepted. Sent once per established session, including each reconnect |
| `DEV_EVENT_CLOUD_DISCONNECTED` | A working session ended (dropped, stopped by `IOT_CoreCloudDisconnect()`, or replaced by a new attempt); always after a CONNECTED |
| `DEV_EVENT_CLOUD_CONNECT_FAILED` | An attempt failed: the session did not come up (or was stopped or replaced first). Ends the attempt a CONNECTING announced |

Each CONNECTING is followed by one CONNECTED or CONNECT_FAILED. An attempt that a newer one replaces
(`IOT_CoreCloudConnect()` called mid-attempt) or that `IOT_CoreCloudDisconnect()` stops is ended with
CONNECT_FAILED first. One exception: an attempt core held until the device clock was set, and which
then could not start, is ended only when the next attempt begins.

While a network is up core keeps trying, and announces each try with its own CONNECTING; a cloud
that stays unreachable therefore sends a CONNECTING / CONNECT_FAILED pair per try, never a
DISCONNECTED. While no network is up, core announces no cloud attempt; the next one comes after
`DEV_EVENT_NETWORK_CONNECTED`. After `IOT_CoreCloudDisconnect()` core does not reconnect by itself.

Schedule your connect-time work on CONNECTED:

```c
case DEV_EVENT_CLOUD_CONNECTED:
{
    // Path B, on every connect: Smart scheduler, condition-group re-sync, Record history push.
    // On path A the contract has done these before onCoreEvent sees the event.
    my_smart_and_record_resume();

    // Schedule device status report (5s delay to avoid congestion)
    scheduleDeviceStatusReport();
    break;
}
case DEV_EVENT_CLOUD_DISCONNECTED:
case DEV_EVENT_CLOUD_CONNECT_FAILED:
    show_cloud(false);   // e.g. the "no cloud" LED pattern
    break;
```

### DEV_EVENT_NETWORK_CONNECTING / _CONNECTED / _DISCONNECTED / _CONNECT_FAILED

**When:** the four moments of the device's network link, in the table below.
**Task:** core's task, in order.
**Data:** `data.networkConnected`, `data.networkDisconnected` or `data.networkConnectFailed` (each
`iface` plus `ip` or `reason`); none for CONNECTING. The data is plain values, nothing to free.
**You answer:** nothing.

The device's network link, on WiFi or Ethernet. Useful for status LEDs or fallback behavior. Every
event but CONNECTING names its interface (`IOT_CoreNetIface_t`: `IOT_CORE_NET_IFACE_WIFI` or
`IOT_CORE_NET_IFACE_ETHERNET`), and all four are delivered on core's task.

| Event | Means | Data |
|---|---|---|
| `DEV_EVENT_NETWORK_CONNECTING` | Core started joining the network (before an address) | none |
| `DEV_EVENT_NETWORK_CONNECTED` | An interface got an address; also sent for a network already up at start | `networkConnected.iface`, `.ip` |
| `DEV_EVENT_NETWORK_DISCONNECTED` | A working link dropped; always after a CONNECTED on that interface | `networkDisconnected.iface`, `.reason` |
| `DEV_EVENT_NETWORK_CONNECT_FAILED` | An attempt failed: the interface was not up and did not come up | `networkConnectFailed.iface`, `.reason` |

A device retrying WiFi (wrong password, access point gone) sends one CONNECT_FAILED per attempt,
never a DISCONNECTED. `reason` is an `IOT_NwkErrorReason_t`; on CONNECT_FAILED it is
`IOT_CORE_NET_REASON_NOT_STARTED` when core could not start the attempt at all (for example, no
stored WiFi credentials).

```c
case DEV_EVENT_NETWORK_CONNECTED:
{
    const IOT_CoreDeviceEventNetworkConnectedData_t *data = &devEvent->data.networkConnected;
    // data->iface, data->ip
    break;
}
case DEV_EVENT_NETWORK_DISCONNECTED:
    // a working link dropped: devEvent->data.networkDisconnected.iface / .reason
    break;
case DEV_EVENT_NETWORK_CONNECT_FAILED:
    // an attempt failed: devEvent->data.networkConnectFailed.iface / .reason
    break;
```

### DEV_EVENT_IDENTIFICATION

**When:** the app or the cloud asked the device to show itself. Only that request raises it.
**Task:** core's task, as part of a request.
**Data:** `data.identification.result` only.
**You answer:** `data.identification.result = IOT_OK` once the device is identifying itself; any
other code is reported as a failure. Preset to `IOT_ERR_NOT_SUPPORTED`.

The app or the cloud asked the device to show itself (blink an LED, beep). Only that request raises
it: a phone connecting for setup is `DEV_EVENT_PROVISION_STARTED`. This is a request you claim: core
presets `data.identification.result` to `IOT_ERR_NOT_SUPPORTED` and answers the asker from what you
write.

```c
case DEV_EVENT_IDENTIFICATION:
    if (startIdentifyBlink() == IOT_OK)
    {
        deviceEvent->data.identification.result = IOT_OK; // claimed
    }
    break;
```

A device with nothing to show leaves it alone, and the asker is told identify is not supported.

### DEV_EVENT_BOOTED

**When:** first thing in every normal boot (not an update boot), before any setup or network event.
**Task:** the calling task: the one that called `IOT_CoreInit()`, before it returns. On path A that
is the task running `IOT_ContractStart()`.
**Data:** none.
**You answer:** nothing.

The first event of every normal boot, before any setup or network event. `IOT_CoreInit()` sends it
once, synchronously, on the task that called it, once core's set-up has succeeded and before core
goes online: before it connects WiFi or takes up a network that is already connected, and before it
starts the setup advert. Never from a start that failed: a failed `IOT_CoreInit()` sends no
`BOOTED`. What goes wrong after `BOOTED` does not fail the init: a network that does not come up is
reported as an event. A firmware-update boot sends `OS_EVENT_OTA_BOOT_STARTED` instead, and no
`BOOTED`: that boot only runs the download and ends in a restart.

Lifecycle events (setup, network, cloud, Zigbee, WiFi changed) come on core's own
task, one at a time, in the order core raised them, and requests from the cloud or the app also run
on core's task. An event caused by your own call into core (`IOT_CoreInit()`, `IOT_CoreFactoryReset()`,
`IOT_CoreDispatchEvent()`) comes on the task that made the call, before it returns. Because `BOOTED`
comes first, `IOT_CoreIsProvisioned()` already holds this boot's answer inside it, and every network
or setup event of the boot follows it. Your handlers still run on more than one task, so guard any
state they share.

On path B, read `IOT_CoreIsProvisioned()` here and start your initial logic. On path A,
`BOOTED` comes while `IOT_ContractStart()` is still running, before the contract has scheduled its
restore. Start your own logic after `IOT_ContractStart()` returns.

**Restore at boot.** On path A the contract puts a provisioned device's controls back to their last
value for you: right after core starts, through `apply`, with `IOT_CONTRACT_SOURCE_RESTORE`
(nothing stored, recorded or reported; never a lock, a motor command, an event or a reading).
Have your hardware ready before `IOT_ContractStart()`. Choose the behaviour with
`IOT_ContractRestoreConfigure()`: `IOT_CONTRACT_RESTORE_LAST` (default), `IOT_CONTRACT_RESTORE_OFF`
(everything with on/off comes back off, for a heater or a pump), `IOT_CONTRACT_RESTORE_NONE` (you
restore yourself). On path B, restore yourself here: read the root's element state from the Device
store and drive the controls. A cloud command normally needs a few seconds of cloud connection
before it can arrive, but that is not guaranteed: if a command can reach your outputs while you
restore, guard them.

---

## SMART Events

Path B only: on path A the contract takes `IOT_EVENT_SMART` and runs every automation itself. How
an SDK of your own stores and runs automations is [how-to/smart-engine.md](how-to/smart-engine.md);
the rules it must keep are the [Smart rulebook](../../sdkcontract/docs/smart.md).

### Smart event reference

Every Smart event is delivered on **core's task, as part of a request**, so each handler runs to
completion before the next request is handled (an event your own automation synthesises and passes
to `IOT_CoreDispatchEvent()` runs on the task that dispatches it). Every pointer in the data
(`attrValue`, `scheduleTimes`, `linkedId`, `attrValueList`, `groups`, `ctx.senderId`) is borrowed:
valid only until you return. The *broadcast* events go to every device in the location and only the
device that holds the SMID acts; the others stay silent. On path A the contract takes all of them.

### SMART_EVENT_ACTIVE_BY_USER

**When:** the cloud or the app runs an automation now (a broadcast).
**Task:** core's task, as part of a request.
**Data:** `data.activeByUser.smid`.
**You answer:** nothing to core. Execute the bind-commands of the SMID if you hold it; send nothing
if you do not.

### SMART_EVENT_ACTIVE_BY_TRIGGER

**When:** the cloud forwards a trigger that fired on another device (a broadcast).
**Task:** core's task, as part of a request.
**Data:** `data.activeByTrigger`: `smid`, `triggerType`, `triggerKey`, `attrValue` (the value that
fired it), `timeConfig`, `activeSection`, `controlCount`. `attrValue.attrValue` is borrowed.
**You answer:** nothing to core. Execute the bind-commands with the trigger value.

### SMART_EVENT_ACTIVE_BY_SCHEDULE

**When:** the cloud runs a schedule slot (a broadcast); your own scheduler's tick reaches the same
code through your own call.
**Task:** core's task, as part of a request.
**Data:** `data.activeBySchedule`: `smid`, `minuteOfWeekScheduleUTC0` (the slot, UTC, 0..10079), and
`uuidv4` / `uuidv4Len`, the identity bytes of the sender that asked for the run (borrowed; they are
not a schedule identifier and not a UUID you need to parse).
**You answer:** nothing to core. Run the slot once, keyed on (SMID, minute of week), so a cloud
request and your own tick for the same minute do not both act ([Smart rulebook](../../sdkcontract/docs/smart.md), rule 8).

### SMART_EVENT_ACTIVE_BY_SYNC_ON_OFF

**When:** a device of a sync-on-off pairing (sub-type 71) broadcasts its new ON/OFF value to the
location.
**Task:** core's task, as part of a request.
**Data:** `data.activeBySyncOnOff`: `smid`, `attrValue` (borrowed) and `senderEid` (the sender's root
EID, 0 from an old sender).
**You answer:** nothing to core. Apply the value to every element of your trigger's element list,
only from a pairing this device holds ([Smart rulebook](../../sdkcontract/docs/smart.md), rule 5).

### SMART_EVENT_REMOVE_ANNOUNCE

**When:** the cloud deletes a whole automation (a broadcast).
**Task:** core's task, as part of a request.
**Data:** `data.removeAnnounce.smid`.
**You answer:** nothing to core. Delete its commands, trigger and schedule if you hold it.

### SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE / SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE

**When:** the cloud sends the schedule times of an automation, or removes its schedule (broadcasts).
**Task:** core's task, as part of a request.
**Data:** `data.scheduleUpdateAnnounce` (`smid`, `scheduleTimes`, `scheduleTimeCount`: minutes of the
week, UTC; the array is freed when you return, so copy it) or `data.scheduleRemoveAnnounce.smid`.
**You answer:** nothing to core. Store or delete the schedule.

### SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE

**When:** the cloud announces a trigger's key so devices can check their copy (a broadcast).
**Task:** core's task, as part of a request.
**Data:** `data.triggerUpdateAnnounce`: `eid`, `rootEid`, `elemId`, `elemType`, `triggerKey`.
**You answer:** nothing to core. Compare with the stored trigger; log a mismatch.

### SMART_EVENT_BIND_TRIGGER / SMART_EVENT_UNBOUND_TRIGGER

**When:** the cloud creates or updates a trigger rule on this device, or removes one.
**Task:** core's task, as part of a request.
**Data:** `data.bindTrigger` (the rule: keys, attribute values, time window, week days, and the
condition-group shape when the bind carries one) or `data.unboundTrigger` (`eid`, `smid`, the group
shape after the removal when supplied). Both carry `ctx`; pointers are borrowed.
**You answer:** `IOT_CoreRespondSmartCmd()` with `cmdType = IOT_SMART_CMD_BIND_TRIGGER` or
`IOT_SMART_CMD_UNBOUND_TRIGGER` and your result, when `ctx.requiresResponse` is set.

### SMART_EVENT_BIND_CMD / SMART_EVENT_UNBOUND_CMD

**When:** the cloud binds the commands an automation runs on this device, or removes this device's
commands from it.
**Task:** core's task, as part of a request.
**Data:** `data.bindCmd` (`smid`, `eid`, `rootEid`, `prtc`, `filter`, `cmdSize`, `linkedId`,
`attrValueList` with each command's value, delay and reverse time, `ctx`) or `data.unboundCmd`
(`smid`, `eid`, `ctx`). Pointers are borrowed: serialise before you return.
**You answer:** `IOT_CoreRespondSmartCmd()` with `cmdType = IOT_SMART_CMD_BIND_CMD` or
`IOT_SMART_CMD_UNBOUND_CMD`, when `ctx.requiresResponse` is set.

### SMART_EVENT_TRIGGER_MODE / SMART_EVENT_ENABLE_DISABLE

**When:** the cloud enables or disables a trigger, a schedule, or every automation of a device
(TRIGGER_MODE), or enables or disables one automation by SMID (ENABLE_DISABLE).
**Task:** core's task, as part of a request.
**Data:** `data.triggerMode` (`type`, `mode`, and the `setTrigger`, `schedule` or
`enableDisableAutomation` member that `type` selects, plus `ctx`) or `data.enableDisable` (`smid`,
`enabled`, `ctx`).
**You answer:** `IOT_CoreRespondSmartCmd()` with `cmdType = IOT_SMART_CMD_TRIGGER_MODE` or
`IOT_SMART_CMD_ENABLE_DISABLE`, when `ctx.requiresResponse` is set. For ENABLE_DISABLE, a device that
does not hold the SMID answers `IOT_ERR_NOT_FOUND`, which core turns into silence so that one
command yields one reply.

### SMART_EVENT_CHECK_SYNC

**When:** the cloud asks whether this device holds the trigger of an SMID.
**Task:** core's task, as part of a request.
**Data:** `data.checkSync`: `smid` and `ctx` (borrowed pointers).
**You answer:** `IOT_CoreRespondSmartCheckSync()` with `exists`, `triggerEid`, `enabled`, `cfm`.
With `exists` false core sends nothing, on purpose.

### SMART_EVENT_REPORT_COND_STATE

**When:** a peer device reports its own seat of a condition group (AND/OR, sub-type 2048): its bit,
its config version, N and K.
**Task:** core's task, as part of a request.
**Data:** `data.reportCondState` (`IOT_CoreSmartCondStatus_t`): `smid`, `eid`, `senderRootEid`,
`version`, `condCount`, `fireCount`, `condIndex`, `mask`, `departing` (`askForFire` is for sync requests). Only the bit at
`condIndex` of `mask` is the sender's; plain values, nothing to free.
**You answer:** nothing to core. Merge the report by the version rule: a higher version adopts the
sender's version and N/K, and either way you take the sender's bit and keep your own. A report with
`departing` set means the sender's seat is leaving: retire it.

### SMART_EVENT_SYNC_COND_REQUEST

**When:** a peer that rebooted asks the devices of its condition groups for their state, announcing
its own at the same time.
**Task:** core's task, as part of a request.
**Data:** `data.syncCondRequest`: `groups` (an array of `IOT_CoreSmartCondStatus_t`, owned by the
handler and freed when you return), `groupCount`, `requesterEid`.
**You answer:** nothing to core directly. For each group, answer the requester only when your state
differs, with `IOT_CoreRequestSmartSyncCondResponse()`. For a group this device is not in, answer
with `IOT_CoreRequestSmartCondNotMember()` only when the group's `askForFire` is set (an
ask-before-fire, where a fire waits on the answer); a reboot re-sync leaves it 0 and its
non-members stay silent, so that one reboot does not cost a unicast per absent SMID.

### SMART_EVENT_COND_NOT_MEMBER

**When:** a peer answered your sync request with "I hold no trigger for this SMID".
**Task:** core's task, as part of a request.
**Data:** `data.condNotMember`: `smid`, `responderEid` (for logs).
**You answer:** nothing. This is positive proof that the seat is gone; silence from a peer only means
it is offline and must never retire a seat.

---

## IR Events

An IR product hears from the cloud and the app through `IOT_EVENT_IR` (`IOT_CoreEventIr.h`). All
seven events share one struct, `IOT_CoreIrEvent_t`, on **core's task, as part of a request**. The
input pointers (`header`, `data`) are borrowed: valid only until you return. On path A the contract
stores keysets and answers the capability question itself (declare it with `IOT_ContractIrConfigure()`);
sending IR and learning a remote stay yours and arrive at `onCoreEvent`.

### IOT_IR_EVENT_EXEC_RAW / _RAW_ZIP / _PRTC / _AC

**When:** the cloud or the app tells the device to transmit an IR code: raw timing pairs (EXEC_RAW),
compressed timings (EXEC_RAW_ZIP), a protocol id with its code bytes (EXEC_PRTC), or a
differential air-conditioner state (EXEC_AC).
**Task:** core's task, as part of a request.
**Data:** `data` / `dataLen` (EXEC_RAW, EXEC_PRTC code bytes, EXEC_RAW_ZIP payload, with `header` /
`headerLen` for the zip), `protocol` (the protocol id for EXEC_PRTC, the brand for EXEC_AC), and for
EXEC_AC `acPower`, `acMode`, `acTemp`, `acFan`, `acSwing`. All pointers are borrowed.
**You answer:** `ir->result`: `IOT_OK` once the code has been sent (or accepted for sending), or the
error that stopped it. Preset to `IOT_ERR_NOT_SUPPORTED`, so a device with no IR hardware says so by
leaving it.

### IOT_IR_EVENT_QUERY_CAPABILITY

**When:** the cloud asks what IR this device can do.
**Task:** core's task, as part of a request.
**Data:** nothing in; you fill `capMode` and `capDetect`.
**You answer:** `capMode` (0 = transmit only, 1 = receive only, 2 = both) and `capDetect` (0 = no
protocol detection, 1 = detection supported). Left alone, the device reports transmit-only.

### IOT_IR_EVENT_STORE_KEYSET

**When:** the cloud sends the packed keyset (the learned keys) for an IR appliance.
**Task:** core's task, as part of a request.
**Data:** `eid` (the appliance's sub-device) and `data` / `dataLen` (the packed keyset, borrowed).
**You answer:** `ir->result` once the keyset is stored. On path A the contract stores it in the IR
store and answers.

### IOT_IR_EVENT_LEARN_START

**When:** the app asks the device to open a learn / detect window and listen for a remote.
**Task:** core's task, as part of a request.
**Data:** `learnMode` (0x01 = capture raw timings without decoding; otherwise decode) and `ctx`, the
requester's routing. This is the one asynchronous IR event: `ctx.senderId` and `ctx.requestId`
are borrowed and gone when you return, but the capture arrives long after, so **deep-copy** `ctx`
(including the bytes `senderId` and `requestId` point to) before you return if your learn task
answers later.
**You answer:** `ir->result` now means only "the window is open" (`IOT_OK`) or why it could not be
(an error). The capture is reported later, from your own task, with `IOT_CoreReportIrDetected()`, or
`IOT_CoreReportIrLearnFailed()` when the window ends without a usable capture; both take your copy
of `ctx`. Without the failure report the app cannot tell a timeout from a device that stopped
answering.

---

## SETTING Events

Configuration changes from the cloud.

### SETTING_EVENT_WIFI_CHANGED

**When:** the cloud sent new WiFi credentials and the device stored them and joined the new network.
**Task:** core's task, in order.
**Data:** `data.wifiChanged.ssid`, a NUL-terminated array inside the event (at most
`IOT_CORE_WIFI_SSID_MAX_LEN` bytes). The password is never carried.
**You answer:** nothing.

When the cloud sends new WiFi credentials, once the device has stored them and joined the new
network. `ssid` is a NUL-terminated array inside the event (at most `IOT_CORE_WIFI_SSID_MAX_LEN`
bytes), valid for the whole handler call; copy it if you keep it. The password is never sent.

```c
static void handleSettingEvent(const IOT_CoreSettingEvent_t *settingEvent)
{
    switch (settingEvent->type)
    {
    case SETTING_EVENT_WIFI_CHANGED:
    {
        const char *newSsid = settingEvent->data.wifiChanged.ssid;
        IOT_LOGI(TAG, "WiFi SSID changed to: %s", newSsid);
        // Core already stores the new WiFi config and reconnects.
        // Use this to update your UI or trigger any SDK-side logic.
        break;
    }
    }
}
```

---

## MESH Events

### MESH_EVENT_KEYS_RECEIVED

**When:** the cloud delivers a mesh network or application key to this device.
**Task:** core's task, as part of a request.
**Data:** `data.keys`: `kind`, `keyIndex`, `key` / `keyLen` (borrowed: valid only until you return)
and `result`.
**You answer:** `data.keys.result`, preset to `IOT_OK`: write the error only if you tried to store
the key and could not.

The cloud delivered a mesh network or application key (`data.keys`: `kind`, `keyIndex`, `key`,
`keyLen`). Core does not keep it: storing it and joining the mesh are your SDK's job. `key` is valid
only during the call, so copy what you keep.

The cloud waits for an answer, and the answer is yours. Unlike every other request, core presets
`data.keys.result` to `IOT_OK`: the app sends the mesh keys to every device during setup and
abandons the setup if one is refused, so a device that keeps no mesh keys must accept them. A mesh
hub writes the error when it tried to store the key and could not:

```c
case MESH_EVENT_KEYS_RECEIVED:
    meshEvent->data.keys.result = my_store_mesh_key(&meshEvent->data.keys);
    break;
```

A device that does not act as a mesh hub leaves it alone (or registers no `IOT_EVENT_MESH`
handler), and the cloud is told the key was accepted.

---

## OS Events

System-level events.

### OS_EVENT_REBOOT_REQUESTED

**When:** the cloud asks this device to restart.
**Task:** core's task, as part of a request.
**Data:** `data.rebootRequested.result` only.
**You answer:** `data.rebootRequested.result = IOT_OK` once you have arranged the restart; core does
not restart the chip. Preset to `IOT_ERR_NOT_SUPPORTED`.

The cloud asked this device to restart. This is a request, not a notice: core does not restart
the chip. You claim it, then restart. `data.rebootRequested.result` (`IOT_CoreOsEventRebootRequestedData_t`)
is your answer; core presets it to `IOT_ERR_NOT_SUPPORTED`, reads it when your callback returns,
and tells the cloud success for `IOT_OK` or failure with the code otherwise.

Do your cleanup, arrange the restart a moment later so the reply can still leave the device (a
short timer), and write `IOT_OK`:

```c
static void handleOsEvent(IOT_CoreOsEvent_t *osEvent)
{
    switch (osEvent->type)
    {
    case OS_EVENT_REBOOT_REQUESTED:
        // Flush any pending DAO writes, stop timers, etc., then schedule the restart.
        if (scheduleRestart(500))
        {
            osEvent->data.rebootRequested.result = IOT_OK; // claimed
        }
        break;
    }
}
```

If you do not claim it (no `IOT_EVENT_OS` handler, or one that leaves `result` alone), nothing
restarts and the cloud is told the request is not supported. An existing handler for the old
`OS_EVENT_REBOOT` must be renamed and must now claim the request and restart itself.

### OS_EVENT_OTA_REQUESTED

**When:** the cloud asks for a firmware update.
**Task:** core's task, as part of a request.
**Data:** `data.otaRequested`: `url`, `authToken` / `authTokenLen`, `cert` / `certLen` (all borrowed:
valid only until you return; use `IOT_CoreOtaGetPending()` to read the saved request later),
`autoUpdate` and `claim`.
**You answer:** `data.otaRequested.claim`: `IOT_CORE_OTA_CLAIM_NOW`, `IOT_CORE_OTA_CLAIM_AFTER_REBOOT`
or (left alone) `IOT_CORE_OTA_CLAIM_NONE`; core answers the cloud once, after you return.

The cloud asked for a firmware update. This is a request, not a notice: core has saved it (read it
back with `IOT_CoreOtaGetPending()`, even from inside your callback) but does not download, and it
has not answered yet. Your claim is `data.otaRequested.claim` (`IOT_CoreOtaClaim_t`), preset to
`IOT_CORE_OTA_CLAIM_NONE`; core reads it when your callback returns and answers the cloud once:

| Claim | Meaning | Core tells the cloud | Then |
|---|---|---|---|
| `IOT_CORE_OTA_CLAIM_NOW` | You download in this boot | Accepted | If the device restarts before the image is written, the next boot is a normal boot (BLE up, `DEV_EVENT_BOOTED`) and core reports the update failed. The cloud or the user retries |
| `IOT_CORE_OTA_CLAIM_AFTER_REBOOT` | You restart into an update boot and download there | Accepted | The next boot is an update boot: `OS_EVENT_OTA_BOOT_STARTED`, which you must claim too |
| `IOT_CORE_OTA_CLAIM_NONE` (left alone, or no handler) | Not taken | Not supported | Core erases the saved request; nothing is left behind |

The callback runs on core's task, and core answers only after it returns. **Never restart, and never
block waiting, inside it**: claim, return, and restart from a short timer (long enough for the
answer to leave the device; the reference SDK waits 1.5 s).

```c
case OS_EVENT_OTA_REQUESTED:
    // Download on a lean boot with more free heap: restart into an update boot.
    if (scheduleRestart(1500))
    {
        osEvent->data.otaRequested.claim = IOT_CORE_OTA_CLAIM_AFTER_REBOOT;
    }
    break;
```

To download in this boot instead, start the platform downloader with core's progress callback,
which sends every progress and result message for you, and claim `IOT_CORE_OTA_CLAIM_NOW` only if it
started:

```c
case OS_EVENT_OTA_REQUESTED:
{
    IOT_CoreOsEventOtaRequestedData_t *d = &osEvent->data.otaRequested;
    if (IOT_AppManagerStartUpdateProcess(d->url, d->cert, d->certLen, d->authToken, d->authTokenLen,
                                         NULL, IOT_CoreOtaPlatformProgressCb) == IOT_OK)
    {
        d->claim = IOT_CORE_OTA_CLAIM_NOW;
    }
    break;
}
```

The `url`, `authToken` and `cert` pointers are borrowed: valid only during the callback.

### OS_EVENT_OTA_BOOT_STARTED

**When:** this boot exists only to run an update you claimed with `IOT_CORE_OTA_CLAIM_AFTER_REBOOT`.
**Task:** the calling task: the one inside `IOT_CoreInit()`, before it returns.
**Data:** `data.otaBootStarted.handled`.
**You answer:** `data.otaBootStarted.handled = true` to claim it; left false, core marks the update
failed and restarts.

This boot exists only to run an update you claimed with `IOT_CORE_OTA_CLAIM_AFTER_REBOOT`. It is
sent synchronously from inside `IOT_CoreInit()`, once the network is up, in place of
`DEV_EVENT_BOOTED`; BLE has been released for the download's heap. Claim it by setting
`data.otaBootStarted.handled = true`, skip the rest of your own start-up, and run the download from
`IOT_CoreOtaGetPending()`.

Leaving it unclaimed is a bug in your SDK: without a download nothing would bring BLE back. Core
then reports the update failed (on the next boot's first cloud connection) and restarts into a
normal boot. This is the one case in which core restarts the device on its own.

---

## SERVICE Events

### IOT_SERVICE_EVENT_MSG

**When:** your own backend sent this device a service message.
**Task:** core's task, as part of a request; do not block.
**Data:** `json` / `jsonLen` (NUL-terminated, borrowed: valid only until you return) and `result`.
**You answer:** `result`: `IOT_OK` acknowledges receipt, any error tells the sender the device refused
it. Preset to `IOT_ERR_NOT_SUPPORTED`.

A message your own backend sent this device. Core hands it over unread (it never parses or
validates it) as `IOT_CoreServiceEvent_t` (`IOT_CoreEventService.h`):

| Field | Meaning |
|---|---|
| `json`, `jsonLen` | The message, NUL-terminated, valid only during the callback: copy what you keep |
| `result` | Your answer: `IOT_OK` acknowledges receipt, any error tells the sender the device refused it. Preset to `IOT_ERR_NOT_SUPPORTED` |

Every message arrives as this one event, whatever its kind. Put a field in your JSON that
tells your message kinds apart, and read it here.

The callback runs on the task that processes every other cloud and app command, so do not
block: copy the message, queue it to your own task, and return.

- **Path A (`sdkcontract`):** the event arrives at your `onCoreEvent`. Cast the `const` away
  to write `result`; see "Answering a request" in
  [sdkcontract's core-events page](../../sdkcontract/docs/how-to/core-events.md#answering-a-request).
- **Path B:** register `IOT_EVENT_SERVICE` with `IOT_DevCoreRegisterEventCb()` and write
  `result` before returning.

```c
static void onCoreEvent(const IOT_CoreEvent_t *event)
{
    if (event->category != IOT_EVENT_SERVICE)
        return;
    IOT_CoreServiceEvent_t *svc = (IOT_CoreServiceEvent_t *) &event->evt.service;
    svc->result = myQueueCopy(svc->json, svc->jsonLen) ? IOT_OK : IOT_ERR_NO_MEM;
}
```

To send the other way, use `IOT_CoreServiceMsgPublish()` (`IOT_CoreServiceMsg.h`) from your
own task.

---


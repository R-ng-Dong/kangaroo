# Handle the core events the contract passes on

Every core event the contract does not handle itself arrives at your `onCoreEvent`, unchanged,
with the contract's part already done. With `onCoreEvent` NULL these events are dropped — and the
requests among them are answered "not supported", except the device status request, which the
contract answers ([device-status.md](device-status.md)).

Lifecycle events (setup, network, cloud, WiFi recovery, Zigbee, WiFi changed) are queued and arrive
on core's event task, one at a time and in the order they happened. Requests from the cloud or the
app (identify, device status, reboot, update, IR, mesh keys, service messages) are delivered
synchronously on core's task too, and the next one waits for you. An event your own call into core
caused arrives on your task before that call returns (`DEV_EVENT_BOOTED` or
`OS_EVENT_OTA_BOOT_STARTED` inside `IOT_ContractStart()`). Do not keep the pointer after you return.

## What arrives, and what the contract already did

| Event | The contract already | Your SDK gets |
|---|---|---|
| State command, state query, history query (`IOT_EVENT_STATE` `SET` / `GET` / `RECORD`) | Ran it end to end; a state command is answered from its outcome | Nothing (`SET` reaches you as `apply`) |
| Sensor-log request (`IOT_EVENT_TYPE_STATE_SENSOR_LOG`) | Answered it with an empty log, unless a custom history store is installed | The request, only with `IOT_ContractRecordUseCustomStore()` |
| `IOT_EVENT_SMART` | Stored and ran it | Nothing |
| Device join, removal (`DEV_EVENT_DEVICE_REMOVED`), local factory reset (`DEV_EVENT_FACTORY_RESET`), group bind / unbind, attribute set | Stored it, or wiped its stores, and answered the cloud | The [hooks](hooks.md); `apply` for an attribute set on this device |
| `DEV_EVENT_DEVICE_STATUS_REQUEST` | Nothing yet: it answers after you, if you did not ([device-status.md](device-status.md)) | The request: leave it, and the contract answers it |
| `DEV_EVENT_CLOUD_CONNECTED` | Sent history recorded offline, started the Smart scheduler, re-synced condition groups, scheduled a full state report of every device | The event: do not re-report state |
| Other `IOT_EVENT_DEVICE`: boot (`DEV_EVENT_BOOTED`); setup (`DEV_EVENT_PROVISION_STARTED`, then exactly one of `_CANCELLED`, `_FAILED`, `_COMPLETED`); network and cloud (`DEV_EVENT_NETWORK_*`, `DEV_EVENT_CLOUD_*`: `_CONNECTING`, `_CONNECTED`, `_DISCONNECTED` for a working link that dropped, `_CONNECT_FAILED` for an attempt that never came up); WiFi recovery (`DEV_EVENT_WIFI_RECOVERY_STARTED` / `_ENDED`); identify; Zigbee | Nothing | The event. Identify must be claimed (below) |
| `OS_EVENT_REBOOT_REQUESTED`, `OS_EVENT_OTA_REQUESTED` | Wrote out what it holds in RAM (as `IOT_ContractPrepareRestart()`) | The event: both are requests you claim (below) |
| `OS_EVENT_OTA_BOOT_STARTED` | Nothing | The event: claim it if you run the download in this boot |
| IR keyset store, IR capability question | Stored the keyset; answered with `IOT_ContractIrConfigure()`'s values | Nothing ([ir.md](ir.md)) |
| Other `IOT_EVENT_IR` (send, learn) | Nothing | The event |
| `IOT_EVENT_SERVICE` (a message from your own backend) | Nothing | The event |
| `IOT_EVENT_MESH`, `IOT_EVENT_SETTING` | Nothing | The event. A mesh key is accepted unless you report an error |

## Answering a request

Some events carry an answer you write back before you return. Core reads it when your callback
returns and sends it to whoever asked. `onCoreEvent` receives a `const` pointer, but core owns the
event for the call and these fields are meant to be written, so cast the `const` away:

```c
static void app_on_core_event(const IOT_CoreEvent_t *event)
{
    IOT_CoreEvent_t *e = (IOT_CoreEvent_t *) event;
    if (event->category == IOT_EVENT_SERVICE)
    {
        /* event->evt.service.json / jsonLen: your backend's message, unparsed.
         * Copy it, hand it to your own task, and return. */
        e->evt.service.result = IOT_OK;
    }
}
```

Every result is preset to `IOT_ERR_NOT_SUPPORTED`, which is what the asker receives if you leave it
— except the device status request, which the contract answers when you leave it.

| Event | Field | Write | If you leave it |
|---|---|---|---|
| `OS_EVENT_REBOOT_REQUESTED` | `evt.os.data.rebootRequested.result` | `IOT_OK` once the restart is arranged | The cloud is told it is not supported; nothing restarts |
| `OS_EVENT_OTA_REQUESTED` | `evt.os.data.otaRequested.claim` | `IOT_CORE_OTA_CLAIM_NOW` (you download in this boot) or `IOT_CORE_OTA_CLAIM_AFTER_REBOOT` (you restart into an update boot) | Core erases the request and tells the cloud it is not supported |
| `OS_EVENT_OTA_BOOT_STARTED` | `evt.os.data.otaBootStarted.handled` | `true` if you run the download | Core reports the update failed and restarts |
| `IOT_EVENT_IR` send (`EXEC_*`) | `evt.ir.result` | `IOT_OK` when the code was sent, or the error | The cloud is told the device does not support it |
| `IOT_EVENT_IR` `LEARN_START` | `evt.ir.result` | `IOT_OK` when the learn window opened (the capture is reported later) | The cloud is told the device does not support it |
| `IOT_EVENT_SERVICE` | `evt.service.result` | `IOT_OK` to acknowledge, any error to refuse | The sender is told the device does not support service messages |
| `DEV_EVENT_IDENTIFICATION` | `evt.device.data.identification.result` | `IOT_OK` once the device is identifying itself, or the error | The asker is told it is not supported |
| `DEV_EVENT_DEVICE_STATUS_REQUEST` | `evt.device.data.deviceStatusRequest.result` | Optional: the return of `IOT_CoreRespondDeviceStatus(&...deviceStatusRequest.ctx, &status)`, your own reply | The contract answers it for you ([device-status.md](device-status.md)) |
| `MESH_EVENT_KEYS_RECEIVED` | `evt.mesh.data.keys.result` (preset to `IOT_OK`) | The error, if you tried to store the key and could not | The key is accepted: every device receives the keys during setup, and the phone abandons the setup if one is refused |

A state command's result is the contract's: it answers from its own handling.

## Reboot and update

**Never restart inside `onCoreEvent`**: core answers the cloud after it returns, so arrange the
restart (a short timer) and claim the request. The contract has already written out what it holds
in RAM, so you may restart without `IOT_ContractPrepareRestart()`. An update you claim moves the
device status to `IOT_CONTRACT_STATUS_UPDATING` until a restart, so an update you run yourself must
end in one. Core's update API is in `IOT_CoreOta.h` and `IOT_CoreEventOs.h`.

## Service messages

Every message your backend sends the device arrives as one `IOT_EVENT_SERVICE` event. Core does not
parse or validate it, so put a field in your JSON that tells your message kinds apart and read it
here. Sending the other way is `IOT_CoreServiceMsgPublish()` (`IOT_CoreServiceMsg.h`).

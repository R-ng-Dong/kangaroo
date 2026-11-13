# The device status request

The cloud or the app asks for this device's status (`DEV_EVENT_DEVICE_STATUS_REQUEST`): the app
does it during setup over BLE, and the cloud later. **The contract answers it for you.** You need
no code for it, and with no `onCoreEvent` at all it is still answered.

## What the contract sends

| Field | Value |
|---|---|
| `firmwareVersion` | The running firmware's version (`IOT_AppManagerGetVersion()`) |
| `uptimeSeconds` | Seconds since boot |
| `lastActivationTime` | Unix time of the last state command the contract ran; 0 until one ran with the clock set |
| `ramUsagePercent`, `totalRamKB` | Heap in use, and the heap's size |
| `nvsUsagePercent`, `totalNvsKB` | Storage in use, and the storage's size |
| `protocolFlags` | `[IOT_DEVICE_MODE_INDEPENDENT, mesh, zigbee, 0, 0, 0]` |

Mesh and Zigbee are 1 only for a hub role the device declared with `IOT_CoreSetHubRole()`
(`IOT_CoreHubRoleFlags.h`); Zigbee also needs its coordinator up. A transport that is merely compiled
in is not claimed: the cloud offers what the device claims, and a claim it cannot honour fails later.

## Change what is sent

Register a hook before `IOT_ContractStart()`. It gets the filled status just before it is sent;
change any field, and what is in it when you return is what goes out.

```c
static void app_device_status(IOT_CoreDeviceStatusData_t *status)
{
    status->protocolFlags[0] = IOT_DEVICE_MODE_MASTER; /* a product that is a master, not independent */
}

void app_main(void)
{
    /* ... */
    IOT_ContractDeviceStatusConfigure(app_device_status);
    IOT_ContractStart(&config, &callbacks);
}
```

The hook runs on the task that delivers core events while the requester waits: keep it short. A
string you point `firmwareVersion` at must stay valid until the hook returns. `NULL` removes the
hook.

## Answer it yourself

Still supported. The request reaches `onCoreEvent` first; reply on its context and write the
reply's return into the result, and the contract sends nothing:

```c
case DEV_EVENT_DEVICE_STATUS_REQUEST:
{
    IOT_CoreDeviceEventStatusRequestData_t *req =
        &((IOT_CoreEvent_t *) event)->evt.device.data.deviceStatusRequest;
    req->result = IOT_CoreRespondDeviceStatus(&req->ctx, &my_status);
    break;
}
```

The contract answers only a request whose `result` is still `IOT_ERR_NOT_SUPPORTED`, the value core
presets. Any other value is your answer, including an error from a reply that failed: the request
is answered once.

## The status report after a cloud connect

The request is the cloud or the app asking. The unsolicited report you send after the cloud
connects (`IOT_CoreRequestReportDeviceStatus()`, `IOT_CoreState.h`) is still yours to send.

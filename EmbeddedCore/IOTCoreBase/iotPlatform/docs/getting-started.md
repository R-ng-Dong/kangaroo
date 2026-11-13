# Getting started: port the stack to a new chip or OS

`iotcore` ships as a prebuilt library that calls only the headers in `iotPlatform/include/`. Porting
means providing those functions for your chip or OS, in a directory of your own beside
`implplatform/esp32/` (the reference: read it next to each step). Nothing above the platform
changes. [implementation-guide.md](implementation-guide.md) is the full reference for every
interface; this page is the order to work in and how to know each step works.

## What you provide, and what you do not

| You implement | You do not implement |
|---|---|
| OS abstraction (`IOT_Osal.h`): tasks, queues, mutexes, critical sections, heap figures, uptime | Anything in `iotcore`: protocol, message handling, provisioning, the cloud session |
| Timers (`IOT_TimerMgmt.h`) and logging (`IOT_Log.h`) | Core's own configuration store: core keeps it on your key-value storage |
| Key-value storage and its cache (`dao/IOT_DaoStorage.h`, `dao/IOT_Cached.h`, `dao/IOT_DaoMgmt.h`) | The device, group, automation and history stores: `sdkcontract` or the SDK keeps them on your storage |
| Network (`network/`): WiFi and/or Ethernet, with events | |
| MQTT client (`IOT_MqttMgmt.h`) and TCP server (`IOT_TcpServer.h`) | |
| BLE (`ble/IOT_BleDriver.h`): a driver vtable your BLE stack registers | |
| Application management (`IOT_AppManager.h`): version, restart, the update download | |
| `IOT_InitPlatform()` (`IOT_Platform.h`): brings all of the above up, in order | |
| Optional drivers, only for a feature you ship: mDNS, mesh, Zigbee coordinator | |

An optional interface you do not register answers `IOT_ERR_NOT_SUPPORTED`, so a port without mDNS
or mesh still links and runs. `IOT_PlatformGetServiceHealth()` reports which optional services
actually work.

## Order

Bring the port up in this order; each step is testable before the next exists.

1. **OS abstraction, timers, logging.** Every other layer uses them. Check: a timer fires once after
   the milliseconds you asked, a mutex blocks a second task, `IOT_LOGI` prints. Timer callbacks run
   on a small stack: they may only re-arm, set a flag or hand work to a task.
2. **Storage and cache.** `IOT_DaoInit()` opens it; `IOT_DaoStorageGet/Set` round-trip every
   `IOT_DaoType`, a missing key answers `IOT_ERR_NOT_FOUND`, and values survive a restart. Match
   the reference implementation's error codes exactly: core and the contract branch on them.
3. **Network.** Join a WiFi network with stored credentials, raise connect, disconnect and
   connect-failed events, and reconnect with backoff by yourself.
4. **MQTT and TCP server.** A TLS MQTT session that connects, subscribes and publishes; a TCP server
   for the local network.
5. **BLE.** Register your driver ([ble-driver.md](ble-driver.md)); advertising and the GATT service
   are what an unprovisioned device is set up through, so nothing can be provisioned without it.
6. **`IOT_InitPlatform()`.** Bring everything up in this order, then start `iotcore`
   (`IOT_CoreInit()` on an SDK of your own, or `IOT_ContractStart()` on `sdkcontract`) and set a
   device up from the app.
7. **Application management.** Version, restart, and the update download, which core hands to
   the SDK on an update request.

## Two things that cost porting partners the most time

- **Heap, not certificates.** A TLS session needs one large contiguous block; when it is missing,
  the failure looks like a certificate or network error. Watch the largest free block, not total
  free heap, and keep the TLS buffer sizes in [sdkconfig-reference.md](sdkconfig-reference.md).
- **Error codes your storage returns.** A storage port that answers success where the reference
  answers "not found" (or the other way round) makes core and the stores take the wrong branch
  with no error anywhere. Mirror the reference's mapping, including for an empty value.

When the checklist at the end of [implementation-guide.md](implementation-guide.md#7-checklist) is
complete, the port is done.

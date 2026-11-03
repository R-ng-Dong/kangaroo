# Changelog archive — 12_23_2025 to 05_12_2026

These entries predate tagged releases, so unlike later versions they have no release page
to be read from. They are kept here in full.

**This file is frozen.** Nothing new is added to it. Recent releases live in
[CHANGELOG.md](../CHANGELOG.md); anything that drops off the bottom of that file from
`v2026.09.09` onward is readable at its own tag and in
[Releases](../../../releases), so it does not need archiving here.

Oldest entry last, same as the main file.

---

# 05_12_2026

## libiotcore.a updated (esp32, esp32s3, esp32c3, esp32c6)
- Added timezone persistence APIs: `IOT_CoreSetTimeZone()` and `IOT_CoreGetTimeZone()`.
- Moved provisioning mode types into new public header `IOT_ProvisionTypes.h`, included by `IOT_Core.h`.
- Added `DEV_EVENT_DEVICE_ATTR_SET` for DAO-completed sub-device attribute writes. SDK writers must call
  `IOT_CoreRespondDeviceCmd()` with `cmdType = IOT_DEVICE_CMD_DEVICE_ATTR` after persisting the attribute value.
- Group bind/unbind handlers must pass `cmdData2` through to `IOT_CoreRespondDeviceCmd()` when responding with
  `IOT_DEVICE_CMD_BIND_GRP`, `IOT_DEVICE_CMD_BIND_GRP_ELMS`, or `IOT_DEVICE_CMD_UNBOUND_GRP`.

## iotPlatform network API
- `IOT_NwkUnregisterEventCb()` now unregisters a specific callback function.
- Added network auto-reconnect controls: `IOT_NwkSetAutoReconnect()` and `IOT_NwkGetAutoReconnect()`.

## Example and docs
- Updated `example/esp32App_rd` for `DEV_EVENT_DEVICE_ATTR_SET` and complete DEVICE command response fields.
- Updated partner docs for timezone APIs, network callback unregister/auto-reconnect APIs, and device event response patterns.
- Updated the SDK Writer Guide with terminology for the Core Event Contract, SDK Integration Model, SDK-Owned
  Responsibilities, and App-Facing API Guidance.
- Expanded SDK Writer Guide coverage for group management, group control, location-routed control, and group DAO
  element-binding behavior.
- Removed ready-to-flash example binaries from the SDK package. Build `example/esp32App_rd` from source when example
  firmware is needed.
- Updated example app version to `2.0.0.0`.

---

# 04_28_2026

## libiotcore.a updated (esp32, esp32s3, esp32c3, esp32c6)
- Bug fix: GET_STATE and sensor-log responses now routed to the requester's app topic (not the device's own topic)

---

# 04_24_2026

## libiotcore.a updated (esp32, esp32s3, esp32c3, esp32c6)
- Countdown control service implemented (`STATE/DEVICE_COUNTDOWN_CONTROL_START`)
- Countdown AttrInfo corrected: short count instead of byte count
- Bug fix: BLE provisioning responses now routed back over BLE (not MQTT)
- Bug fix: device-owned `cfm` counter for BIND_TRIGGER (starts at 1, increments on re-bind)

## IOT_CoreEventHandler.h

New `IOT_RequestProtocol_t` enum:
```c
typedef enum {
    IOT_REQUEST_PROTOCOL_BLE  = 0x01,
    IOT_REQUEST_PROTOCOL_TCP  = 0x02,
    IOT_REQUEST_PROTOCOL_MQTT = 0x03,
} IOT_RequestProtocol_t;
```

`IOT_RequestContext_t` has two new fields:
```c
typedef struct {
    IOT_RequestProtocol_t protocol; // Transport the request arrived on
    ...
    bool requiresResponse; // false = sender expects no response; Reporter APIs no-op silently
} IOT_RequestContext_t;
```

**Migration:** Pass `ctx` through unchanged as before. The new fields are populated by core — no SDK changes required unless you read individual ctx fields.

`IOT_CoreDeviceEventNewDeviceJoinedData_t`: field `syncData` renamed to `cmdData2` for consistency. Update any read sites.

**DEVICE event enum reorder** — `DEV_EVENT_NEW_DEVICE_JOINED` and `DEV_EVENT_DEVICE_REMOVED` removed from `IOT_EventTypeDevice_t`. Rebuild required.

## IOT_CoreReporter.h — REQUIRED: New DAO-completed response APIs

**You must call these APIs after completing DAO work** for SMART, DEVICE, and CHECK_SYNC commands.
Core no longer sends a generic notify before the SDK finishes — the response is only sent when you call these.
Skipping the call means the cloud gets no acknowledgement.

**SMART event response table:**

| Event | Your action | Response API | Notes |
|-------|-------------|--------------|-------|
| `SMART_EVENT_BIND_TRIGGER` | Save trigger to DAO | `IOT_CoreRespondSmartCmd` (`BIND_TRIGGER`) | cfm = 1 (new) or existing+1 (update) — see cfm rule below |
| `SMART_EVENT_UNBOUND_TRIGGER` | Delete trigger from DAO | `IOT_CoreRespondSmartCmd` (`UNBOUND_TRIGGER`) | cfm = 0 |
| `SMART_EVENT_TRIGGER_MODE` | Update mode in DAO | `IOT_CoreRespondSmartCmd` (`TRIGGER_MODE`) | cfm = 0 |
| `SMART_EVENT_BIND_CMD` | Save cmd binding to DAO | `IOT_CoreRespondSmartCmd` (`BIND_CMD`) | same cfm rule as BIND_TRIGGER |
| `SMART_EVENT_UNBOUND_CMD` | Delete cmd binding from DAO | `IOT_CoreRespondSmartCmd` (`UNBOUND_CMD`) | cfm = 0 |
| `SMART_EVENT_CHECK_SYNC` | Look up trigger by smid | `IOT_CoreRespondSmartCheckSync` | set `exists=false` if not found |
| `SMART_EVENT_ACTIVE_BY_USER` | Load bind cmds from DAO, execute them on device | — no response needed | |
| `SMART_EVENT_ACTIVE_BY_TRIGGER` | Load bind cmds from DAO, execute them on device | — no response needed | skip if device triggered it locally already (dedup) |
| `SMART_EVENT_ACTIVE_BY_SCHEDULE` | Load bind cmds from DAO, execute them; save schedule time to DAO for offline replay | — no response needed | |
| `SMART_EVENT_REMOVE_ANNOUNCE` | Delete ALL smid data from DAO (bind cmds + trigger + schedule) | — no response needed | |
| `SMART_EVENT_TRIGGER_UPDATE_ANNOUNCE` | Validate stored trigger keys for announced EID | — no response needed | integrity check only, no DAO write needed |
| `SMART_EVENT_SCHEDULE_UPDATE_ANNOUNCE` | Replace all schedule times in DAO for smid (remove old, store new); start scheduler | — no response needed | |
| `SMART_EVENT_SCHEDULE_REMOVE_ANNOUNCE` | Delete schedule from DAO | — no response needed | |

**DEVICE event response table:**

| Event | Your action | Response API | Notes |
|-------|-------------|--------------|-------|
| `DEV_EVENT_NEW_DEVICE_JOINED` (DEVICE_INFO phase) | Save device info to DAO | `IOT_CoreRespondDeviceCmd` (`SYNC_DEV_JOINED`) | call per phase |
| `DEV_EVENT_NEW_DEVICE_JOINED` (ELM_INFO phase) | Save element info to DAO | `IOT_CoreRespondDeviceCmd` (`SYNC_DEV_JOINED`) | call per phase |
| `DEV_EVENT_NEW_DEVICE_JOINED` (SYNC_COMPLETE) | — | — no response needed | join complete, nothing to ack |
| `DEV_EVENT_DEVICE_REMOVED` (sub-device) | Delete device from DAO | `IOT_CoreRespondDeviceCmd` (`SYNC_DEV_REMOVED`) | |
| `DEV_EVENT_DEVICE_REMOVED` (root device) | Clear all state | — no response needed | device resets itself |
| `DEV_EVENT_GROUP_BIND` | Save group binding to DAO | `IOT_CoreRespondDeviceCmd` (`BIND_GRP` or `BIND_GRP_ELMS`) | use `BIND_GRP_ELMS` if element bindings present |
| `DEV_EVENT_GROUP_UNBIND` | Delete group binding from DAO | `IOT_CoreRespondDeviceCmd` (`UNBOUND_GRP`) | |
| `DEV_EVENT_BOOTED` | Restore hardware state | — no response needed | |
| `DEV_EVENT_IDENTIFICATION` | Start provisioning UI | — no response needed | |
| `DEV_EVENT_PROVISION_COMPLETED` | Save credentials | — no response needed | |
| `DEV_EVENT_WIFI_CONNECTED/DISCONNECTED` | Update UI/LED | — no response needed | |
| `DEV_EVENT_CLOUD_CONNECTED/DISCONNECTED` | Sync state if needed | — no response needed | |

> **See working examples in `example/esp32App_rd/main/main.c`:**
> - `SMART_EVENT_BIND_TRIGGER` (~line 354) — `IOT_CoreRespondSmartCmd` with cfm pattern
> - `SMART_EVENT_UNBOUND_TRIGGER` (~line 374), `TRIGGER_MODE` (~line 269), `BIND_CMD` (~line 424), `UNBOUND_CMD` (~line 443)
> - `SMART_EVENT_CHECK_SYNC` (~line 471) — `IOT_CoreRespondSmartCheckSync`
> - `DEV_EVENT_NEW_DEVICE_JOINED` (~line 531) — `IOT_CoreRespondDeviceCmd` per phase
> - `DEV_EVENT_DEVICE_REMOVED` (~line 579), `DEV_EVENT_GROUP_BIND` (~line 605), `DEV_EVENT_GROUP_UNBIND` (~line 625)

```c
// SMART commands: BIND_TRIGGER, UNBOUND_TRIGGER, TRIGGER_MODE, BIND_CMD, UNBOUND_CMD
typedef struct {
    IOT_SmartCmdType_t   cmdType;
    uint16_t             cfm;
    iot_err_t            result;
    IOT_RequestContext_t ctx;
} IOT_CoreSmartCmdResponse_t;

iot_err_t IOT_CoreRespondSmartCmd(const IOT_CoreSmartCmdResponse_t *response);

// DEVICE commands: BIND_GRP, BIND_GRP_ELMS, UNBOUND_GRP, SYNC_DEV_REMOVED, SYNC_DEV_JOINED
typedef struct {
    IOT_DeviceCmdType_t  cmdType;
    iot_err_t            result;
    IOT_RequestContext_t ctx;
} IOT_CoreDeviceCmdResponse_t;

iot_err_t IOT_CoreRespondDeviceCmd(const IOT_CoreDeviceCmdResponse_t *response);

// SMART CHECK_SYNC
typedef struct {
    uint16_t             smid;
    bool                 exists;     // false = trigger not in DAO, send nothing
    uint16_t             triggerEid; // only valid if exists=true
    bool                 enabled;    // only valid if exists=true
    uint16_t             cfm;        // only valid if exists=true
    IOT_RequestContext_t ctx;
} IOT_CoreSmartCheckSyncResponse_t;

iot_err_t IOT_CoreRespondSmartCheckSync(const IOT_CoreSmartCheckSyncResponse_t *response);
```

**Required call pattern — after every DAO save/delete, call the matching API:**

```c
// Example: SMART BIND_TRIGGER
// cfm is DEVICE-OWNED — do NOT use the cloud-sent value (data->bindTriggerElmInfo.cfm).
// Rule: cfm = 1 on first bind of this smid; cfm = existing_cfm + 1 on re-bind.
YourTrigger_t existing;
uint16_t cfm = (YourDao_GetTrigger(data->bindTriggerElmInfo.smid, &existing) == IOT_OK)
                   ? existing.cfm + 1
                   : 1;
YourDao_SaveTrigger(smid, ..., cfm);

IOT_CoreSmartCmdResponse_t resp = {
    .cmdType = IOT_SMART_CMD_BIND_TRIGGER,
    .cfm     = cfm,           // your computed cfm, not the cloud value
    .result  = IOT_OK,        // or error code on failure
    .ctx     = data->ctx,     // pass through unchanged
};
IOT_CoreRespondSmartCmd(&resp);
```

The same cfm rule applies for `BIND_CMD` — load existing cfm from DAO, increment it, store it, report it back.

These replace the old pattern where core sent a generic notify before the SDK completed DAO work.

> **See working example:** `example/esp32App_rd/main/main.c` — search for `SMART_EVENT_BIND_TRIGGER` (line ~354) and `SMART_EVENT_BIND_CMD` (line ~424) for the full cfm + response call pattern to copy.

## Driver layer removed

`IOT_Driver.h` and `implplatform/esp32/IOT_Driver.c` (and `driver/` subdirectory) have been removed. If you were using `IOT_DriverUart*`, `IOT_DriverI2c*`, or `IOT_DriverGpio*` APIs, use ESP-IDF driver APIs directly instead.

# 04_17_2026

## libiotcore.a updated (esp32, esp32s3, esp32c3, esp32c6)
- OTA progress reporting: directed MQTT response back to requesting app (instead of broadcast)
- OTA result verification: post-reboot result now sent automatically on MQTT reconnect — no SDK action required
- WiFi change response: directed back to requesting app via correct MQTT topic
- Internal bug fixes (duplicate NOTIFY on SET, MQTT topic routing improvements)

## IOT_Core.h — BREAKING

`IOT_CoreInit()` signature simplified:
```c
// Before
iot_err_t IOT_CoreInit(uint8_t *productId, bool isBleSupported, bool isWifiSupported);

// After
iot_err_t IOT_CoreInit(uint8_t *productId);
```
BLE and WiFi support are now determined automatically from the platform configuration.

New provisioning mode API (call before `IOT_CoreStartProvision()` to override default):
```c
typedef enum {
    IOT_PROVISION_BLE_LAN = 0, // default
    IOT_PROVISION_LAN_ONLY,    // not yet implemented
    IOT_PROVISION_AP_MODE,     // not yet implemented
} IOT_ProvisionMode_t;

iot_err_t IOT_CoreSetProvisionMode(IOT_ProvisionMode_t mode);
```

## IOT_CoreEventHandler.h — BREAKING

New `DEV_EVENT_PROVISION_CANCELLED` event added to `IOT_EventTypeDevice_t`.
Fired when the provisioning flow starts but does not complete (device remains unprovisioned).
Useful for resetting UI/LED state after a failed pairing attempt.

**Enum reorder** — `DEV_EVENT_PROVISION_CANCELLED` is inserted between `DEV_EVENT_IDENTIFICATION`
and `DEV_EVENT_PROVISION_COMPLETED`. Rebuild required.

## IOT_AppManager.h — BREAKING

`IOT_OtaProgressCb_t` and `IOT_AppManagerStartUpdateProcess()` now take a `void *user_data` parameter:
```c
// Before
typedef void (*IOT_OtaProgressCb_t)(uint8_t status, uint8_t progress);
iot_err_t IOT_AppManagerStartUpdateProcess(..., IOT_OtaProgressCb_t progressCb);

// After
typedef void (*IOT_OtaProgressCb_t)(uint8_t status, uint8_t progress, void *user_data);
iot_err_t IOT_AppManagerStartUpdateProcess(..., void *user_data, IOT_OtaProgressCb_t progressCb);
```

New OTA status codes and error codes added to `IOT_Core.h`:
- `IOT_OTA_STATUS_INPROGRESS` (0x01), `IOT_OTA_STATUS_NOT_UPDATE` (0x03), `IOT_OTA_STATUS_UPDATING` (0x05), `IOT_OTA_STATUS_SOFTWAREPROGRESS` (0x06), `IOT_OTA_STATUS_UPDATE_LATER` (0x07)
- `IOT_OTA_ERR_*` error codes for `FAILURE` status (URL not found, busy, version lower, download/update errors)

New `IOT_OtaState_t` struct and `IOT_AppManagerGetOtaState()` for querying live OTA state.

## IOT_CoreReporter.h

`IOT_CoreCheckAndReportOtaResult()` and `IOT_CoreReportOtaResult()` removed from public API — OTA result verification is now handled automatically by iotcore after reboot. No SDK action required on MQTT reconnect.

## implplatform updates
- `IOT_AppManager.c`: OTA task updated for `user_data` pass-through
- `IOT_MqttMgmt.c`, `IOT_NetworkMgmt.c`, `wifi.c`: connection lifecycle improvements
- `IOT_BleMgmtNimble.c`: minor fixes
- `IoTLog.c`: log output improvements

## docs/
- `README.md`: rewritten in English with accurate architecture overview and quick-start
- `docs/core/API_Document.md`: full rewrite in English covering all current public APIs

---

# 03_25_2026

## libiotcore.a updated (esp32, esp32s3, esp32c3, esp32c6)
- Internal message building improvements for protocol compatibility
- WiFi scan and network switching stability improvements
- esp32c6 target now included

## implplatform updates
- Minor fixes in BLE, MQTT, timer, DAO, OSAL, and AppManager implementations
- WiFi: fixed auto-reconnect race condition during network switch
- WiFi: scan now respects requested scan interval before returning results
- MQTT: improved connection lifecycle during network changes
- Network: scan result buffer overflow guard (respects caller-provided capacity)

---

# 03_20_2026

## API changed (iotcore public headers)

### IOT_Core.h — BREAKING
- REMOVED `IOT_CoreRemoveDevice(uint16_t eid)` — now internal to iotcore
- REMOVED `IOT_CoreFactoryResetConfig()` — core handles its own config reset internally
- ADDED `IOT_CoreFactoryReset()` — full factory reset (core resets its config + dispatches `DEV_EVENT_DEVICE_REMOVED`)
- ADDED `IOT_CoreGetBootCounter(uint8_t *count)` — get current boot counter (blocking)
- ADDED `IOT_CoreSetBootCounter(uint8_t count)` — set boot counter (blocking)
- ADDED `IOT_CoreSetBootCounterAsync(uint8_t count)` — set boot counter (non-blocking)

### Boot guard — BREAKING
- Boot guard REMOVED from iotcore — SDK must implement its own boot loop recovery
- Use `IOT_CoreGetBootCounter()` / `IOT_CoreSetBootCounter()` to track boot loops
- On boot loop detection, call `IOT_CoreFactoryReset()` to reset the device

## DAO architecture — BREAKING

### iotPlatform/include/dao/ restructured
- REMOVED `IOT_DaoConfigMgmt.h` — config DAO is now iotcore-internal (baked into libiotcore.a)
- REMOVED `IOT_DaoDeviceMgmt.h`, `IOT_DaoGroupMgmt.h` — moved to `sdk/dao/` (SDK-owned)
- ADDED `IOT_Cached.h` — cache interface (was in implplatform, now in iotPlatform)
- ADDED `IOT_DaoStorage.h` — abstract key-value storage interface

### implplatform/esp32/dao/ cleaned up
- REMOVED `IOT_DaoConfigMgmt.c` — now in iotcore
- REMOVED `IOT_DaoDeviceMgmt.c`, `IOT_DaoGroupMgmt.c` — moved to `sdk/dao/`
- REMOVED stale `IOT_Cached.h` (use `iotPlatform/include/dao/IOT_Cached.h`)
- ADDED `IOT_DaoStorage.c` — abstract storage implementation
- `IOT_DaoMgmt.c` — now platform-only (NVS + cache init). SDK DAO inits are your responsibility.
- `IOT_DaoFactoryResetAll()` — no longer called by iotcore. SDK implements if needed.

### New: sdk/ folder
- `sdk/dao/IOT_DaoDeviceMgmt.{h,c}` — reference device DAO implementation
- `sdk/dao/IOT_DaoGroupMgmt.{h,c}` — reference group DAO implementation
- These are reference implementations — customize or replace for your product

## Bug fixes

### IOT_BleMgmtNimble.c
- fixed `IOT_BleIsConnected()` returning false on ESP32 when connection handle is 0 (valid NimBLE handle)
- added connection state reset on disconnect (was missing)

## Multi-target support
- `iotcore/lib/` now includes libraries for esp32, esp32c3, esp32s3
- Per-target `sdkconfig.defaults.{target}` files shipped (BT controller settings per chip)
- CMakeLists uses `${IDF_TARGET}` — just `idf.py set-target esp32c3` and it works

## SDK writer action required
1. **BREAKING**: Replace `IOT_CoreRemoveDevice(eid)` with `IOT_CoreFactoryReset()` for device removal
2. **BREAKING**: Remove any calls to `IOT_CoreFactoryResetConfig()` — core handles this internally now
3. **BREAKING**: Implement your own boot guard using `IOT_CoreGetBootCounter()` / `IOT_CoreSetBootCounter()`
4. **BREAKING**: Update DAO includes:
   - `#include "dao/IOT_DaoConfigMgmt.h"` → remove (no longer available)
   - `#include "dao/IOT_DaoDeviceMgmt.h"` → still works (now from `sdk/` component)
5. Add `sdk` to your main component's REQUIRES in CMakeLists.txt
6. If implementing `IOT_DaoFactoryResetAll()`: it is no longer called by iotcore, but you may still want it for your own factory reset flow

## libiotcore.a updated (esp32, esp32c3, esp32s3)

# 03_13_2026

## API changed (iotcore public headers) — BREAKING

### IOT_CoreEventHandler.h
- **BREAKING**: `IOT_EventStateSetRequest_t` restructured:
  - REMOVED `numDevices`, `eidList` fields
  - ADDED `IOT_SetTargetType_t targetType` + `target` union with 3 variants:
    - `IOT_SET_TARGET_DEVICE` → `target.device.eid`
    - `IOT_SET_TARGET_GROUP` → `target.group.groupEid`
    - `IOT_SET_TARGET_VOICE` → `target.voice.devId`, `target.voice.devIdLen`, `target.voice.groupEid`
  - SDK must check `targetType` and resolve targets accordingly
- renamed `IOT_DEV_JOIN_PHASE_TIME_SYNC` → `IOT_DEV_JOIN_PHASE_SYNC_COMPLETE`
- `IOT_DEV_JOIN_PHASE_SYNC_COMPLETE` data now includes `meshKey`, `meshKeyLen` (SDK persists mesh key)
- added `DEV_EVENT_DEVICE_STATUS_REQUEST` — cloud requests device status report

### IOT_Core.h
- added `IOT_CoreStartProvision()` — restart BLE provisioning advertising
- added `IOT_CoreStopProvision()` — stop BLE provisioning advertising
- added `IOT_CoreMeshProvision()` — load mesh keys from config DAO and join mesh network

### IOT_CoreReporter.h (NEW — replaces IOT_ReportMsgBuilder.h and IOT_ResponseMsgBuilder.h)
- **BREAKING**: Old builder pattern APIs removed (`IOT_MsgReportInitBuilder`, `IOT_MsgReportAddLocationMacDevId`, `IOT_MsgReportAddEidElm`, `IOT_MsgReportAddAttrValue`, `IOT_MsgReportAddReportOption`, `IOT_MsgReportAddEidStateChange`, `IOT_MsgReportAddElmAttrsChange`, `IOT_MsgReportSendMsg`, `IOT_MsgResponseInitBuilder`, `IOT_MsgResponseAddEidElm`, `IOT_MsgResponseAddAttrValue`, `IOT_MsgResponseAddBlockTimeSync`, `IOT_MsgResponseSendMsg`)
- New declarative APIs — SDK provides DATA, core builds messages:
  - `IOT_CoreRequestReportState(const IOT_CoreStateResponse_t *response)` — GET_STATE response
  - `IOT_CoreRequestReportStateChanged(const IOT_CoreStateChangeReport_t *report)` — state change report (SET, local control, smart)
  - `IOT_CoreRequestReportDeviceStatus(const IOT_CoreDeviceStatusData_t *status)` — device health report
  - `IOT_CoreRequestSensorLogResponse(const IOT_CoreSensorLogResponse_t *response)` — sensor log response

## iotPlatform changed

### IOT_BleMgmt.h
- added `IOT_BleIsConnected(bool *isConnected)` — query BLE connection state

### IOT_Osal.h
- added `IOT_OsalGetTotalHeapSize()` — total heap size
- added `IOT_OsalGetUptimeUs()` — system uptime in microseconds

### IOT_DaoMgmt.h
- added `IOT_DaoGetStorageStats(uint32_t *usedEntries, uint32_t *totalEntries)` — NVS usage stats

### IOT_DaoConfigMgmt.h
- added `CONFIG_DATA_ROOT_NWK_ADDR` — root device mesh unicast address

### IOT_MdnsMgmt.h
- renamed APIs from underscore-prefix style to `IOT_Mdns*` convention (`IOT_MdnsStartService`, `IOT_MdnsAddAdvertisement`, etc.)

### IOT_NetworkMgmt.h
- added `IOT_NwkGetConnectionStatus(iface, status)` — unified connection state getter
- added `IOT_NwkGetAuthMode(iface)` — WiFi auth type
- added `IOT_NwkGetRssi(iface)` — signal strength (dBm)
- added `IOT_NwkGetIpInfo(iface, ipInfo)` — IP/netmask/gateway
- added new WiFi error reasons: `ASSOC_FAIL(203)`, `CONNECTION_FAIL(205)`, `AP_TSF_RESET(206)`, `NO_AP_FOUND_COMPATIBLE_SECURITY(210)`

### ethernet.h
- added `IOT_EthGetConnectionStatus(iface_num)` — Ethernet connection state getter

### wifi.h
- added `IOT_WifiGetAuth()`, `IOT_WifiGetRssi()` — WiFi auth mode and RSSI getters
- added `IOT_WifiIpInfo_t` typedef (old name kept as alias)

### mesh/IOT_MeshMgmt.h
- `IOT_MeshJoinNetwork(void)` → `IOT_MeshJoinNetwork(const IOT_MeshCredentials_t *credentials)` — platform receives ready-to-use credentials struct

## platform module updated (implplatform/esp32)

### BLE
- MTU reset before advertising to fix BLE mesh coexistence issues
- NULL guard on `IOT_BleNotifyToCharacteristic()`

### IOT_MqttMgmt
- `isConnected` changed to `volatile bool` for thread safety

### IOT_DaoMgmt
- sensor log init/reset wrapped with `IOT_FEATURE_SENSOR_LOG` feature flag (no action needed — distribution does not define this flag)

### IOT_NetworkMgmt
- implemented connection state, auth, RSSI, IP info getters

### ethernet
- added `CONFIG_IOT_ETHERNET_ENABLED` Kconfig guard — Ethernet source files conditionally compiled

## CMakeLists.txt changed
- iotplatform REQUIRES: added `mdns` (managed component via `idf_component.yml`)
- ethernet source files and `esp_eth` dependency now conditional on `CONFIG_IOT_ETHERNET_ENABLED`
- added `idf_component.yml` with `espressif/mdns` dependency

## sdkconfig updated
- compiler optimization changed: Debug → Size (-Os)
- disabled unused NimBLE services and BLE 5.0 features
- significant binary size reduction (~296 KB saved)

## partitions.csv
- `rogo` NVS partition size: 120k (was 240k in some configurations)

## SDK writer action required
1. **BREAKING**: Replace `#include "IOT_ReportMsgBuilder.h"` and `#include "IOT_ResponseMsgBuilder.h"` with `#include "IOT_CoreReporter.h"`
2. **BREAKING**: Rewrite state report/response code to use new declarative APIs (see example main.c)
3. **BREAKING**: Update SET handler to check `setData->targetType` and use `setData->target` union instead of `numDevices`/`eidList`
4. Rename `IOT_DEV_JOIN_PHASE_TIME_SYNC` → `IOT_DEV_JOIN_PHASE_SYNC_COMPLETE`
5. Handle `DEV_EVENT_DEVICE_STATUS_REQUEST` to report device health to cloud
6. Add `idf_component.yml` with `espressif/mdns` dependency to your iotplatform component
7. Add `managed_components/` and `dependencies.lock` to `.gitignore`

## libiotcore.a updated

# 03_03_2026

## iotPlatform changed

### IOT_AppManager.h
- `IOT_AppManagerStartUpdateProcess` signature changed: added `authToken` and `authTokenLen` parameters for OTA server authentication
- from: `IOT_AppManagerStartUpdateProcess(const char* updateUrl, const char* cert, size_t certLen, IOT_OtaProgressCb_t progressCb)`
- to: `IOT_AppManagerStartUpdateProcess(const char* updateUrl, const char* cert, size_t certLen, const char* authToken, size_t authTokenLen, IOT_OtaProgressCb_t progressCb)`
- when a Bearer token is provided, it is sent as `Authorization: Bearer <token>` HTTP header during OTA download

## platform module updated (implplatform/esp32)

### IOT_AppManager
- added HTTP client init callback that sets `Authorization: Bearer <token>` header when auth token is provided
- token is passed via `http_config.user_data` → `http_client_init_cb`

## SDK writer action required
- update `IOT_AppManagerStartUpdateProcess()` call: add `authToken` and `authTokenLen` before the progress callback (or `NULL, 0` if no token needed)

## libiotcore.a updated

# 03_02_2026

## API changed (iotcore public headers)

### IOT_Core.h
- renamed `IOT_DevCoreDeinit()` → `IOT_CoreDeinit()`
- `IOT_CoreDeinit()` is now implemented (was a stub returning `IOT_ERR_FAIL`)
- Tears down BLE, LAN (MQTT/TCP), and timer subsystems

## SDK writer action required
- rename `IOT_DevCoreDeinit()` → `IOT_CoreDeinit()` if used in your code

## iotPlatform changed

### IOT_AppManager.h
- `IOT_AppManagerStartUpdateProcess` signature changed: added `IOT_OtaProgressCb_t progressCb` parameter
- from: `IOT_AppManagerStartUpdateProcess(const char* updateUrl, const char* cert, size_t certLen)`
- to: `IOT_AppManagerStartUpdateProcess(const char* updateUrl, const char* cert, size_t certLen, IOT_OtaProgressCb_t progressCb)`
- added `IOT_OtaProgressCb_t` callback typedef
- added `IOT_OTA_STATUS_SUCCESS` (0x00), `IOT_OTA_STATUS_FAILURE` (0x02), `IOT_OTA_STATUS_DOWNLOADING` (0x04) constants

## Bug fixes (iotcore internal)
- OTA HTTPS: removed `skip_cert_common_name_check` which was disabling SNI, causing Kubernetes ingress to serve a fake certificate

## New feature (iotcore internal)
- OTA progress is now reported to the cloud over MQTT every 10%, plus a final success/failure

## SDK writer action required
- update `IOT_AppManagerStartUpdateProcess()` call: add progress callback as last parameter (or `NULL` to skip progress reporting)

## iotPlatform changed

### IOT_MqttMgmt.h (BREAKING — version 1.0 → 2.0)
- API changed from singleton to multi-client with opaque handles
- added `IOT_MqttClientHandle_t` — opaque handle type (`typedef struct IOT_MqttClient *IOT_MqttClientHandle_t`)
- `IOT_MqttInit()` → `IOT_MqttInit(IOT_MqttClientHandle_t *outHandle)` — now returns a handle
- `IOT_MqttDeinit()` → `IOT_MqttDeinit(IOT_MqttClientHandle_t handle)`
- ALL functions now take `IOT_MqttClientHandle_t handle` as first parameter:
  - `IOT_MqttConfigClient(handle, config)`
  - `IOT_MqttConnect(handle)`
  - `IOT_MqttDisconnect(handle)`
  - `IOT_MqttGetConnectionStatus(handle, &isConnected)`
  - `IOT_MqttSendMessage(handle, topic, payload, len)`
  - `IOT_MqttSubscribe(handle, topic, qosMax, &msgId)`
  - `IOT_MqttUnsubscribe(handle, topic)`
  - `IOT_MqttRegisterEventCallback(handle, eventType, callback, arg)`
  - `IOT_MqttUnregisterEventCallback(handle, eventType)`

## iotcore internal changed (inside iotcore.a)

### utils/IOT_Mqtt.h
- `Mqtt_ConnectFromConfig()` now takes `IOT_MqttClientHandle_t handle` as first parameter

### wiledirect/IOT_WileDirectLan.h
- added `IOT_WileDirectLanMqttReadyCb_t` callback type
- added `IOT_WileDirectLanMqttConnectAndSubscribe(onReady, arg)` — connects to broker, subscribes to all standard topics, fires callback when all subscriptions confirmed

## SDK writer action required
- update all `IOT_Mqtt*()` calls to use handle-based API:
  1. declare `IOT_MqttClientHandle_t mqttClient;`
  2. call `IOT_MqttInit(&mqttClient);`
  3. pass `mqttClient` as first argument to all other `IOT_Mqtt*()` functions
- if using `Mqtt_ConnectFromConfig()`, add handle as first parameter

## libiotcore.a updated

# 02_27_2026

## iotPlatform changed

### IOT_BleMgmt.h
- renamed `BLE_ADDR_TYPE_*` → `IOT_BLE_ADDR_TYPE_*` (PUBLIC, RANDOM, RPA_PUBLIC, RPA_RANDOM) to avoid conflicts with Bluedroid headers

## platform module updated (implplatform/esp32)

### BLE
- renamed `IOT_BleMgmt.c` → `IOT_BleMgmtNimble.c` for clarity
- added `IOT_BleMgmtBluedroid.c` — Bluedroid BLE stack implementation (guarded by `CONFIG_BT_BLUEDROID_ENABLED`)
- both NimBLE and Bluedroid now produce identical advertising packets (16-bit UUID, company ID in LE byte order)

## sdkconfig.defaults updated
- full sdkconfig.defaults now shipped (BLE, WiFi, LWIP, MQTT, mbedTLS, FreeRTOS, system settings)
- previously only had SNTP max servers setting

## SDK writer action required
- rename `BLE_ADDR_TYPE_PUBLIC` → `IOT_BLE_ADDR_TYPE_PUBLIC` (and similarly for RANDOM, RPA_PUBLIC, RPA_RANDOM) if used in your code
- if switching to Bluedroid: set `CONFIG_BT_BLUEDROID_ENABLED=y` and `# CONFIG_BT_NIMBLE_ENABLED is not set` in sdkconfig

## libiotcore.a updated

# 02_25_2026

## API changed (iotcore public headers)

### IOT_ReportMsgBuilder.h
- `IOT_MsgReportAddReportOption()` — REMOVED `_3rdService` parameter
- from: `IOT_MsgReportAddReportOption(sid, pushNotify, logEvent, triggerCloud, _3rdService)`
- to: `IOT_MsgReportAddReportOption(sid, pushNotify, logEvent, triggerCloud)`

## Bug fixes (iotcore internal)
- fixed report message delivery issue

## platform module updated (implplatform/esp32)

### IOT_MqttMgmt
- MQTT publish QoS changed from 1 to 0

### IOT_DaoMgmt
- smart trigger/schedule init wrapped with `IOT_FEATURE_SMART_TRIGGER` / `IOT_FEATURE_SMART_SCHEDULE` feature flags (no action needed — distribution does not define these flags)

## example main.c updated
- updated `IOT_MsgReportAddReportOption()` call to match new 4-param signature (removed last `0`)

## libiotcore.a updated

# 02_12_2026

## API changed (iotcore public headers)

### IOT_CoreEventHandler.h
- renamed `targetElementType` back to `targetDeviceType` in IOT_EventStateSetRequest_t
- REMOVED `protocol` field from IOT_EventStateSetRequest_t
- added `numDevices`, `eidList` to IOT_EventStateSetRequest_t (group batch dispatch)
- added `senderFrom`, `senderId`, `senderSize` to IOT_EventStateSetRequest_t
- added `requestId`, `requestIdLen` to IOT_EventStateSetRequest_t (voice request)
- added 4 new device events: DEV_EVENT_WIFI_CONNECTED, DEV_EVENT_WIFI_DISCONNECTED, DEV_EVENT_CLOUD_CONNECTED, DEV_EVENT_CLOUD_DISCONNECTED
- added new structs: IOT_CoreDeviceEventWifiConnectedData_t (ip, netmask, gateway), IOT_CoreDeviceEventWifiDisconnectedData_t (reason)
- simplified schedule event structs: `smidScheduleAnnounce` -> `smid`, ScheduleRemoveAnnounce now has `smid`

### IOT_ReportMsgBuilder.h
- REMOVED `locationId`/`locationIdLen` fields from `blockLocationMacDevId_t` — report builder reads locationId internally now
- added `IOT_MsgReportEnrichVoice()` — call after adding report blocks, before SendMsg

## iotPlatform changed

### IOT_AppManager.h
- `IOT_AppManagerStartUpdateProcess` signature changed: `(const char* updateUrl)` -> `(const char* updateUrl, const char* cert, size_t certLen)`

### IOT_TimerMgmt.h
- added `bool IOT_TimerIsTimeSynced(void)`

### IOT_Locker.h
- DELETED — use IOT_Osal mutex functions instead

### IOT_Log.h
- renamed `LOG_COLOR_*` -> `IOT_COLOR_*`
- log format changed: `[tag]` -> `tag:` (ESP-IDF style)
- level strings shortened: `"INFO"` -> `"I"`, `"WARN"` -> `"W"`, etc.

### IOT_DaoConfigMgmt.h
- `DEVICE_ID_LEN` wrapped with `#ifndef` guard

### IOT_DaoDeviceMgmt.h
- added `bool IOT_DaoDeviceGetEidByDevId(uint8_t *devId, uint16_t *outEid)`
- `devId` in IOT_DevIdentityInfo_t now uses `DEVICE_ID_LEN` macro instead of hardcoded 12

### IOT_NetworkMgmt.h
- added `IOT_NwkErrorReason_t` enum (WiFi disconnect reason codes)

### network/wifi.h
- added `iot_wifi_err_reason_t` enum (WiFi error reason codes)

## platform module updated (implplatform/esp32)

### IOT_AppManager
- OTA is implemented and working

### wifi
- `IOT_WifiGetConnectionStatus()` now returns actual WiFi state

### IOT_TimerMgmt
- added mutex protection for heap operations (thread-safe)
- timer thread stack increased to 6KB

## CMakeLists.txt changed
- iotplatform REQUIRES: added `esp_https_ota` and `esp_http_client` (for OTA support)

## sdkconfig
- ensure `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` is enabled (for OTA cert bundle fallback)

## libiotcore.a updated

# 01_29_2026

## New component:
- iotcommon: utils module to handle memory or str.

## example CMakeLists.txt fix
- iotcore: added `iotcommon` to REQUIRES in `idf_component_register` and removed from `target_link_libraries` to fix linker error (`cannot find -liotcommon`)
- iotplatform: added `iotcommon` to REQUIRES

## API changed

### IOT_CoreEventHandler.h
- renamed `targetDeviceType` to `targetElementType` in IOT_EventStateSetRequest_t
- added new device events: DEV_EVENT_NEW_DEVICE_JOINED, DEV_EVENT_DEVICE_REMOVED, DEV_EVENT_BOOTED
- added new structs: IOT_DevJoinPhase_t, IOT_DevJoinElmInfo_t, IOT_CoreDeviceEventNewDeviceJoinedData_t, IOT_CoreDeviceEventDeviceRemovedData_t

### IOT_DaoDeviceMgmt.h
- added `IOT_DaoDeviceGetElementType(uint16_t devEid, uint16_t elemId, uint16_t *outElemType)`
- added `IOT_DaoDeviceFactoryReset(void)`

### IOT_NetworkMgmt.h
- added `NETWORK_TYPE_NONE = 0` to IOT_NetworkType enum (end-of-array marker)

## platform module updated

### IOT_Esp32Platform
- added `IOT_DeinitPlatform()` stub
- use NETWORK_TYPE_NONE for ifaces array terminator

### IOT_TimerMgmt
- refactored SNTP time sync to use callback (`time_sync_callback`) instead of blocking wait
- changed NTP servers to use IP addresses (129.6.15.28 NIST, 216.239.35.0 Google)

### IOT_DaoDeviceMgmt
- implemented `IOT_DaoDeviceGetElementType()`
- implemented `IOT_DaoDeviceFactoryReset()`
- reduced verbose logging

### IOT_NetworkMgmt
- use `Mem_SafeCalloc`/`SAFE_FREE` instead of malloc/free
- added null checks for callback managers before use
- improved error handling and cleanup on failures

### IOT_BleMgmt
- refactored `getAttrIndex()` to return iot_err_t and use output params
- general code cleanup

## example main.c updated
- added `#include "IOT_AppManager.h"`
- changed `targetDeviceType` to `targetElementType`
- added handling for DEV_EVENT_NEW_DEVICE_JOINED
- added handling for DEV_EVENT_DEVICE_REMOVED (calls `IOT_ApplicationRestartChip`)

## libiotcore.a updated

# 01_20_2026

## REQUIRES changed.
- add driver at example/components/iotplatform/CMakeLists.txt (line 7)

## API changed

### iot_err_t MsgReport(core api)
- from    iot_err_t IOT_MsgReportSendMsg(uint64_t inSid, uin t8_t *senderId, uint8_t senderIdLen);    to    iot_err_t IOT_MsgReportSendMsg(uint64_t inSid);

### update new core event
- Provision completed

### DaoDevice (Get and Set are both changed).
#### Description
- from  iot_err_t IOT_DaoDeviceGetData(IOT_DaoDeviceDataType_t dataType, uint16_t eid, void **outData, uint16_t *outDataLen);   to    iot_err_t IOT_DaoDeviceGetIdentityInfo(uint16_t eid, IOT_DevIdentityInfo_t *outDevIdentityInfo);, iot_err_t IOT_DaoDeviceGetCommonInfo(uint16_t eid, IOT_DevCommonInfo_t *outDevCommonInfo);, iot_err_t IOT_DaoDeviceGetDeviceElemsState(uint16_t DevEid, IOT_DaoDeviceElemsAttrsInfo_t *outElemsInfo);
- from .... IOT_DaoDeviceSetData     to    SetCommon, SetIdentity, ... add elemstate, update elemstate....
#### example
- IOT_DaoDeviceGetData(DEVICE_DATA_ID...) -> IOT_DaoDeviceGetIdentityInfo(uint16_t eid, IOT_DevIdentityInfo_t *outDevIdentityInfo); (call IOT_DaoDeviceFreeIdentityInfo to free data after using)
- IOT_DaoDeviceGetData(DEVICE_DATA_COMMON, ...) -> IOT_DaoDeviceGetCommonInfo ....
- IOT_DaoDeviceGetData(DEVICE_DATA_ELEM_STATE, ....) -> IOT_DaoDeviceGetDeviceElemsState


## new platform module added
- IOT_Osal.h

## platform module updated.
### IOT_Timer
- update init function (fix time sync bug).
### IOT_DaoConfig
- update async func for DaoConfig.

## private module updated
### IOT_Cached.
- dao/IOT_Cached.h, .c
### dao_common.
- update async func.

## esp32 menuconfig updated
- config max server: [text](example/esp32App_rd/sdkconfig.defaults)
- Component config → LWIP → SNTP

# 01_07_2026

## update new module
 - IOT_AppManager: iotPlatform/inc, impl/esp32

## add new event for device
    -  in IOT_CoreEventHandler.h (DEV_EVENT_IDENTIFICATION) for indicator device when adding.
## update REQUIRES for iotplatform
 - add app_update in REQUIRES

## moving log_init() into InitPlatform()

## update example
    - update devices event.


# 12_29_2025

## update productID example.
    - edit productID example
## update core funcs
    - renamed IOT_DevCoreInit -> IOT_CoreInit


# 12_23_2025
## BleMgmt:
    - api name is changed: from IOT_BleInitServices to IOT_BleAddServices!

## errorManager:
    - added IOT_ERR_NOT_INITIALIZED.

## example:
    - locationId is passed via callback data, not getting from dao.

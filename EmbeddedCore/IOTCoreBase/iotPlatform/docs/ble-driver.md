# Hướng dẫn Implementation IOT_BleMgmt

## Tổng quan

Tài liệu này hướng dẫn chi tiết cách implement BLE driver cho các platform khác nhau (ESP32, nRF52, STM32, Linux,...). File example `implplatform/esp32/ble/IOT_BleMgmtNimble.c` (NimBLE stack) và `implplatform/esp32/ble/IOT_BleMgmtBluedroid.c` (Bluedroid stack) được sử dụng làm tham khảo. Xem **Section 0** cho driver-registration model mới nhất.

---

## Lưu ý, đây chỉ là tài liệu tham khảo, đối tác có thể  triển khai các hướng khác nhau, miễn là đáp ứng được đầu ra cần thiết của API.

---

## 0. Driver-Registration Model (READ FIRST)

**As of the current SDK, BLE uses a driver-registration model** — the same
pattern as mesh and Zigbee. Why: `iotcore` (which drives BLE provisioning) links
against the abstract `IOT_Ble*()` API, not against any specific BLE stack. A
dispatch layer always provides those symbols and forwards them to whatever driver
registers at runtime — so one prebuilt `libiotcore.a` serves NimBLE, Bluedroid,
a custom stack, or no BLE at all, without recompiling the core.

### Which path applies to you?

- **(A) You reuse the reference ESP32 driver (most partners).** Registration is
  **automatic** — the reference driver self-registers via a
  `__attribute__((constructor))`. You do **NOT** call `IOT_BleRegisterDriver()`.
  Your only responsibilities:
  1. Enable a BT stack in `sdkconfig` (`CONFIG_BT_NIMBLE_ENABLED` or
     `CONFIG_BT_BLUEDROID_ENABLED`).
  2. Keep the `-u IOT_BleMgmt*Register` force-link flags in the `iotplatform`
     component `CMakeLists.txt` (see the "Self-registration via a constructor"
     subsection below). Drop them and BLE silently never initializes.
  You can stop reading here — the rest of this section is for path (B).

- **(B) You write your own BLE driver** (new stack / new platform). Implement the
  `IOT_BleDriver_t` vtable and register it, as described below.

You no longer provide free-standing `IOT_Ble*()` definitions. Instead:

- The SDK ships a **façade / dispatch layer** at `iotPlatform/src/ble/IOT_BleMgmt.c`
  that implements the public API (`IOT_BleInit`, `IOT_BleAddServices`, `IOT_BleStart`,
  `IOT_BleStartAdvertising`, `IOT_BleStopAdvertising`, `IOT_BleIsConnected`,
  `IOT_BleNotifyToCharacteristic`, `IOT_BleRegisterEventCallback`,
  `IOT_BleDeinit`, and the scan/beacon/name variants). Each function forwards to
  a registered **driver vtable**; if no driver is registered it returns
  `IOT_ERR_NOT_SUPPORTED`.
- You implement a **driver vtable** of type `IOT_BleDriver_t`
  (`iotPlatform/include/ble/IOT_BleDriver.h`) and register it via
  `IOT_BleRegisterDriver()`.

### The driver vtable

```c
// iotPlatform/include/ble/IOT_BleDriver.h
typedef struct {
    iot_err_t (*init)(void);
    iot_err_t (*deinit)(void);
    iot_err_t (*addServices)(const IOT_BleMgmtGattServ_t *service);
    iot_err_t (*start)(void);           // optional — leave NULL if no activation step
    iot_err_t (*startAdvertising)(IOT_BleMgmtAdvData_t *adv);
    iot_err_t (*stopAdvertising)(void);
    iot_err_t (*startBeaconAdvertising)(IOT_BleMgmtAdvData_t *adv);
    iot_err_t (*stopBeaconAdvertising)(void);
    iot_err_t (*notifyToCharacteristic)(const char *addr, const uint8_t *charUuid,
                                        const uint8_t *data, uint16_t len);
    iot_err_t (*isConnected)(bool *out);
    iot_err_t (*disconnect)(void);      // optional — leave NULL if unsupported
    iot_err_t (*startScan)(uint32_t intervalMs);
    iot_err_t (*stopScan)(void);
    iot_err_t (*setDeviceName)(const char *name);
    iot_err_t (*registerEventCallback)(IOT_BleMgmtEventStatusType_t event,
                                       IOT_BleEventCb_t cb, void *arg);
    iot_err_t (*unregisterEventCallback)(IOT_BleMgmtEventStatusType_t event);
} IOT_BleDriver_t;

void IOT_BleRegisterDriver(const IOT_BleDriver_t *driver);
```

The function-by-function guidance in the rest of this document still applies —
just implement each one as a **static function** and wire it into your vtable
instead of exporting the `IOT_Ble*()` name directly.

### Self-registration via a constructor (+ force-link)

The reference ESP32 drivers self-register with a constructor so registration
happens automatically before `app_main()` runs:

```c
// implplatform/esp32/ble/IOT_BleMgmtNimble.c (Bluedroid variant is analogous)
static const IOT_BleDriver_t s_nimbleDriver = {
    .init = bleInit, .deinit = bleDeinit, .addServices = bleAddServices,
    /* ...all vtable members... */
};

void __attribute__((constructor)) IOT_BleMgmtNimbleRegister(void) {
    IOT_BleRegisterDriver(&s_nimbleDriver);
}
```

**Static-library gotcha**: a constructor only runs if its object file is pulled
into the final link. Because nothing *references* the driver object (it registers
itself), the linker would otherwise drop it from the archive. The ESP32
`iotplatform` component forces the object in with a synthetic undefined reference
in its `CMakeLists.txt`:

```cmake
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-u IOT_BleMgmtNimbleRegister"
    "-u IOT_BleMgmtBluedroidRegister")
```

Only the symbol for the selected BT stack actually exists; an unresolvable `-u`
wish for the other is harmless (silently ignored by the linker).

**If you implement your own BLE driver**, you must:
1. Populate an `IOT_BleDriver_t` vtable with your static functions.
2. Register it — either from a `__attribute__((constructor))` (and add a matching
   `-u <YourRegisterFn>` force-link flag if it lives in a static library), or by
   calling `IOT_BleRegisterDriver(&yourDriver)` explicitly early in startup
   (before `IOT_CoreInit()`).

The sections below describe the required behavior of each vtable function.

> **Optional `disconnect` slot.** The facade `iot_err_t IOT_BleDisconnect(void)`
> (declared in `IOT_BleMgmt.h`) tears down the current BLE connection by
> dispatching to the driver's `disconnect` slot. It is **additive and optional**:
> leave the slot `NULL` and the facade returns `IOT_ERR_NOT_SUPPORTED`; if
> implemented, return `IOT_ERR_INVALID_STATE` when there is no active connection.

> ⚠️ **New slot `start`, inserted after `addServices`.** It backs `IOT_BleStart()` (§2.1).
> A stack with no separate activation step leaves it `NULL` and `IOT_BleStart()` returns
> `IOT_OK` — the Bluedroid reference driver does exactly that. A vtable built with
> designated initialisers (`.init = …`) needs no other change; one built positionally must
> be rewritten, because every member after `addServices` moved.

---

## 1. Kiến trúc Tổng thể

### 1.1 Mục đích của IOT_BleMgmt

IOT_BleMgmt cung cấp **abstraction layer** cho BLE operations, cho phép:
- Code application độc lập với BLE stack cụ thể
- Dễ dàng port sang platform khác
- API thống nhất cho tất cả BLE operations

### 1.2 Sơ đồ Layer

```
┌──────────────────────────────────┐
│   Application Code               │
│   (IOT Core)                     │
└────────────┬─────────────────────┘
             │ Platform-independent API
             │ (IOT_BleMgmt.h)
┌────────────▼─────────────────────┐
│   IOT_BleMgmt Implementation     │
│   (ESP32/nRF52/Linux/etc)        │
└────────────┬─────────────────────┘
             │ Platform-specific API
             │ (NimBLE/SoftDevice/BlueZ)
┌────────────▼─────────────────────┐
│   BLE Stack                      │
│   (NimBLE/Nordic SDK/BlueZ)      │
└──────────────────────────────────┘
```

---

## 2. API Functions Cần Implement

### 2.1 Initialization & Lifecycle

#### IOT_BleInit()
```c
iot_err_t IOT_BleInit(void);
```

**Mục đích**: Khởi tạo BLE stack và callback management system

**Các bước implement**:

1. **Initialize platform BLE stack**:
   

2. **Setup callbacks cho platform stack**

**Example (ESP32)**:
```c
iot_err_t IOT_BleInit(void) {
    // 1. Allocate callback management

    // 2. Initialize NimBLE (port + host config) — but do NOT start the host task yet

    // 3. Wire the host "synced" callback unconditionally (see below)

    return IOT_OK;
}
```

> ⚠️ **Do not start the BLE host inside `init`.** On NimBLE, starting the host
> (`ble_hs_start()` → `ble_gatts_start()`) is what registers every *pending* GATT service,
> and it frees the pending list afterwards. A service added after that lands on a new
> pending list that nothing starts — it is **silently never registered**: the device boots
> and advertises normally, with no service behind the advert. That presents as a
> provisioning failure, not a BLE error. Start the host in `start` (see `IOT_BleStart()` below),
> after services are registered.

**Wire the "host synced" callback in `init`, unconditionally** — not only when someone
registers `BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE`. Nothing requires a caller to register that
event, and the driver needs the sync signal to know the controller has handed the host an
identity address. Advertising before sync fails (on NimBLE, `ble_hs_id_infer_auto()`
returns `BLE_HS_ENOADDR`); the race is won or lost by chip speed, so it can pass on one
target and fail on another — and it bites the unprovisioned boot, which advertises
immediately, i.e. exactly after a factory reset. Registering `INIT_DONE` should still
forward the event to the caller.

---

#### IOT_BleStart()
```c
iot_err_t IOT_BleStart(void);
#define IOT_BLE_START_MAX_BLOCK_MS 3000u
```

**Mục đích**: Activate the BLE host after every GATT service has been registered.

Lifecycle:

```
IOT_BleInit() -> IOT_BleAddServices() ... -> IOT_BleStart() -> IOT_BleStartAdvertising() / IOT_BleStartScan()
```

Core's own provisioning path already registers its service and calls `IOT_BleStart()` in
this order; you need the explicit call only for GATT services of your own. **Register every
service first, then start.**

**Contract for the driver's `start`**:

- **Idempotent**: only the first successful call activates; later calls return
  immediately. But return `IOT_OK` from the idempotent path only once the host has actually
  **synced** — if an earlier start timed out, or the host task was never created, run the
  bounded wait again (re-creating the host task if it is missing) instead of trusting a
  "started" flag.
- **Blocks** on the first call while the controller synchronises, for at most
  `IOT_BLE_START_MAX_BLOCK_MS`. This is a contract, not a hint: callers size their own
  waits against it. A driver whose activation can take longer must raise the value in the
  header, not exceed it quietly.
- Return codes:

| return | meaning |
|---|---|
| `IOT_OK` | host running and synced (or the stack needs no activation — `start == NULL`) |
| `IOT_ERR_INVALID_STATE` | called before `IOT_BleInit()` |
| `IOT_ERR_NOT_SUPPORTED` | no BLE driver registered |
| `IOT_ERR_TIMEOUT` | the host did not synchronise within `IOT_BLE_START_MAX_BLOCK_MS` |
| `IOT_ERR_NO_MEM` | the host task could not be created (check immediately after creating it — NimBLE's own init does not surface a failed `xTaskCreate()`; do not wait out the full timeout for a sync that can never arrive) |

Callers should treat **any** non-`IOT_OK` result as retryable.

**Implicit start.** If `startAdvertising`, `startBeaconAdvertising` or `startScan` is
called before `IOT_BleStart()`, the reference NimBLE driver activates the host itself and
logs a warning. Do not rely on it: it hides a lifecycle step inside a function that does not
name it, and turns a host failure into an advertising error.

**Bluedroid**: no separate activation step — the driver leaves `start` `NULL`.

---

#### IOT_BleDeinit()
```c
iot_err_t IOT_BleDeinit(void);
```

**Mục đích**: Dừng BLE stack và cleanup resources

**Các bước**:
1. Stop BLE stack
2. Free allocated memory
3. Reset state variables

**Example**:
```c
iot_err_t IOT_BleDeinit(void) {
    // Stop BLE stack

    // Free callback management


    // Free service access structures

}
```

---

### 2.2 Event Callback System

#### IOT_BleRegisterEventCallback()
```c
iot_err_t IOT_BleRegisterEventCallback(
    IOT_BleMgmtEventStatusType_t event,
    IOT_BleEventCb_t cb,
    void *arg
);
```

**Mục đích**: Đăng ký callback cho BLE events

**Event Types**:
- `BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE` - BLE stack initialized
- `BLE_MGMT_EVENT_STATUS_TYPE_RESET` - BLE stack reset
- `BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED` - Device connected
- `BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT` - Device disconnected
- `BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT` - Scan result received
- `BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE` - Scan completed
- ... (xem full list trong header)

**Implementation Pattern**:

```c
iot_err_t IOT_BleRegisterEventCallback(
    IOT_BleMgmtEventStatusType_t event,
    IOT_BleEventCb_t cb,
    void *arg)
{
    // 1. Validate event type
    if (event >= BLE_MGMT_EVENT_STATUS_TYPE_MAX) {
        return IOT_ERR_INVALID_ARG;
    }

    // 2. Handle special events that need platform-specific setup
    switch (event) {
        case BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE:


        case BLE_MGMT_EVENT_STATUS_TYPE_RESET:

        // Other events use GAP callback (internalGapCb)
        default:
            break;
    }

    // 3. Store callback in registry


    return IOT_OK;
}
```

**Key Point**: Bạn cần map từ platform events sang `IOT_BleMgmtEventStatusType_t`:

```c
// Example: Mapping function
static IOT_BleMgmtEventStatusType_t getEventTypeFromPlatformEvent(int platform_event) {
    switch (platform_event) {
        case PLATFORM_EVENT_CONNECTED:
            return BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED;
        case PLATFORM_EVENT_DISCONNECTED:
            return BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT;
        // ... more mappings
        default:
            return BLE_MGMT_EVENT_STATUS_TYPE_MAX;
    }
}
```

---

#### IOT_BleUnregisterEventCallback()
```c
iot_err_t IOT_BleUnregisterEventCallback(IOT_BleMgmtEventStatusType_t event);
```

**Implementation**:
```c
iot_err_t IOT_BleUnregisterEventCallback(IOT_BleMgmtEventStatusType_t event) {
    if (event >= BLE_MGMT_EVENT_STATUS_TYPE_MAX) {
        return IOT_ERR_INVALID_ARG;
    }

    if (!callbackMgmt[event].isRegistered) {
        return IOT_ERR_FAIL;  // Not registered
    }

    // ...
    return IOT_OK;
}
```

---

### 2.3 GATT Service Management

#### IOT_BleAddServices()
```c
iot_err_t IOT_BleAddServices(const IOT_BleMgmtGattServ_t *service);
```

**Mục đích**: Đăng ký GATT services, characteristics, descriptors

Register services **before** `IOT_BleStart()` (§2.1); on NimBLE register them statically
(`ble_gatts_count_cfg()` / `ble_gatts_add_svcs()`), so `CONFIG_BT_NIMBLE_DYNAMIC_SERVICE`
is not needed (§4.1). Called before `IOT_BleInit()` has brought the controller up, return
`IOT_ERR_INVALID_STATE` — the Bluedroid reference driver used to return `IOT_ERR_FAIL` here,
so a caller comparing against `IOT_ERR_FAIL` no longer matches.

**Đây là hàm phức tạp nhất!** Cần:
1. Parse service definitions từ platform-independent format
2. Convert sang format của platform BLE stack
3. Register với stack
4. Track handles để dùng cho notify/indicate

**High-level Flow**:

```
Input: IOT_BleMgmtGattServ_t array
  │
  ├─> Count services
  │
  ├─> For each service:
  │     ├─> Convert UUID to platform format
  │     ├─> Count characteristics
  │     │
  │     ├─> For each characteristic:
  │     │     ├─> Convert UUID & flags
  │     │     ├─> Set access callback (wrapper)
  │     │     ├─> Count descriptors
  │     │     │
  │     │     └─> For each descriptor:
  │     │           ├─> Convert UUID & flags
  │     │           └─> Set access callback
  │     │
  │     └─> Register service with platform stack
  │
  └─> Store service/char/desc handles for later use
```

**Key Data Structures** (Internal):

Bạn cần track mapping giữa UUIDs và handles:


**Access Callback Wrapper**:

Platform stack sẽ gọi callback của bạn khi có read/write. Bạn cần:
1. Convert platform context → `IOT_BleMgmtGattAccessContext_t`
2. Call application callback
3. Convert result trở lại platform format

```c
static int internalCharAccessCallback(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct platform_gatt_access_ctxt *platform_ctxt,
    void *arg)
{
    // ...
}
```

**Service Registration Callback**:

Khi platform stack registers services, bạn cần lưu handles:


---

### 2.4 Advertising

#### IOT_BleStartAdvertising()
```c
iot_err_t IOT_BleStartAdvertising(IOT_BleMgmtAdvData_t *adv_requirements);
```

**Mục đích**: Start connectable advertising

**Parameters in `IOT_BleMgmtAdvData_t`**:
- `uuid`: Service UUID to advertise
- `manufactureId`: Manufacturer ID
- `manufactureData`: Manufacturer-specific data
- `manufactureDataLen`: Length of manufacturer data
- `name` / `nameLen`: **optional device name**, sent in the **scan response** as the
  Complete Local Name (AD type `0x09`). `nameLen == 0` (or `name == NULL`) omits the
  name (the scan response is still sent when `extraRsp` is set). The string need not be NUL-terminated — exactly `nameLen`
  bytes are copied — but it must stay valid for as long as advertising runs.
- `extraAdv` / `extraAdvLen` and `extraRsp` / `extraRspLen`: already-encoded AD elements
  (length, type, data) supplied by the layer above. Append them after your own fields, `extraAdv`
  in the advertising packet and `extraRsp` in the scan response. They are optional (NULL / 0 = none)
  and must stay valid while advertising.

**Scan response rule.** Send a scan response on a connectable advert when either a name or
`extraRsp` is set. When neither is, set or clear it explicitly (an empty scan response) so a
previous advert's data does not linger. The shared builder below packs both payloads, and reports
an empty scan response as length 0; clearing it is the driver's job, as the NimBLE and Bluedroid
drivers do.

> Phones list a nameless advert as "unknown device", so a provisioning advert that
> carries no name is hard for a user to pick out. The name goes in the scan response
> rather than the advert because the 31-byte advert is already full.

**You do not have to encode the payload yourself.** `iotPlatform/include/ble/IOT_BleAdvBuild.h`
builds both packets from the same struct, so every driver produces identical bytes:

```c
uint8_t adv[31], rsp[31];
uint8_t advLen = 0, rspLen = 0;

IOT_BleAdvBuildPayload(adv_requirements, adv, sizeof(adv), &advLen);
IOT_BleAdvBuildScanRsp(adv_requirements, rsp, sizeof(rsp), &rspLen);  /* rspLen 0 = no name */
```

**Implementation Steps**:

1. **Build advertising data** — call the two helpers above.

2. **Setup advertising parameters** — connectable, general discoverable.

3. **Start advertising** — hand `adv`/`rsp` to your stack; skip the scan response
   when `rspLen` is 0.

#### BLE 5.0 extended advertising

If your build enables BLE 5.0 extended advertising, the legacy advertising calls stop
working — and they fail **silently**:

- **NimBLE** with `CONFIG_BT_NIMBLE_EXT_ADV=y` compiles `ble_gap_adv_set_fields()`,
  `ble_gap_adv_rsp_set_fields()`, `ble_gap_adv_start()` and `ble_gap_adv_stop()` down to
  "return `BLE_HS_ENOTSUP`". The build succeeds, the device boots, and it never advertises.
- **Bluedroid** with `CONFIG_BT_BLE_50_FEATURES_SUPPORTED` has the controller refuse the
  legacy advertising calls ("Command Disallowed"), with or without anything else running.

The reference drivers therefore advertise through **extended advertising sets** on those
builds (NimBLE: `ble_gap_ext_adv_configure / _set_data / _rsp_set_data / _start / _stop`;
Bluedroid: the `esp_ble_gap_ext_adv_*` chain, which completes step by step through GAP
events, so the payload buffers must outlive the call). Both keep the on-air PDU a legacy
`ADV_IND` (connectable, scannable) or `ADV_NONCONN_IND` (beacon) by setting the legacy-PDU
property, so phones and scanners see no difference. BLE 4.2 builds keep the legacy path.

If you wrote your own driver, port it before enabling extended advertising. Rules the
reference drivers follow:

- **Instance ids**: the connectable advert uses set **2**, the beacon set **3**
  (`IOT_BLE_ADV_INST_CONN` / `IOT_BLE_ADV_INST_BEACON` in
  `implplatform/esp32/ble/ble_ext_adv_policy.h`). Sets **0** and **1** are left to BLE Mesh,
  which reconfigures its own set at will — sharing one hands it your advert.
- `CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES` is the **highest instance id, not a count**: it
  must be at least 3.
- "Connectable but not scannable" has no legacy-PDU encoding; the stack rejects it.
- **Stopping a set you never configured is success**, as the legacy stop was: the extended
  API reports an error for an unconfigured instance, so track which sets you configured and
  forget them all in `deinit`.
- **Budget the RAM.** Extended advertising costs on the order of **9 KB of heap** once a
  device is also scanning, which can be the difference between a TLS handshake fitting and
  not. A heap-starved handshake is reported by mbedTLS as a *certificate* error
  (`-0x3B00`, `-0x2880`), not as out-of-memory — if TLS starts failing after you enable
  BLE 5.0, check the largest free block first. NimBLE's default ACL / msys buffer counts are
  sized for busy multi-connection peripherals; a device holding one connection can usually
  halve them.

---

#### IOT_BleStopAdvertising()
```c
iot_err_t IOT_BleStopAdvertising(void);
```

**Simple implementation**:
```c
iot_err_t IOT_BleStopAdvertising(void) {

}
```

---

#### IOT_BleStartBeaconAdvertising()
```c
iot_err_t IOT_BleStartBeaconAdvertising(IOT_BleMgmtAdvData_t *adv_requirements);
```

**Khác với StartAdvertising**: Non-connectable (beacon mode)

```c
iot_err_t IOT_BleStartBeaconAdvertising(IOT_BleMgmtAdvData_t *adv_requirements) {
    // Setup advertising data (same as StartAdvertising)
    // ...

    // Different: Non-connectable mode

    return IOT_OK;
}
```

---

### 2.5 Scanning

#### IOT_BleStartScan()
```c
iot_err_t IOT_BleStartScan(uint32_t intervalTime);
```

**Parameters**:
- `intervalTime`: Scan duration in milliseconds (0 = forever)

**Implementation**:

```c
iot_err_t IOT_BleStartScan(uint32_t intervalTime) {
    // Setup scan parameters

    // Convert intervalTime from ms to platform units

    // Start scan

}
```
---

#### IOT_BleStopScan()
```c
iot_err_t IOT_BleStopScan(void);
```

---

### 2.6 Notifications

#### IOT_BleNotifyToCharacteristic() // send notification
```c
iot_err_t IOT_BleNotifyToCharacteristic(
    const char *addr,
    const uint8_t *charUuid,
    const uint8_t *data,
    uint16_t len
);
```

**Mục đích**: Send notification to connected client

**Parameters**:
- `addr`: Client address (có thể NULL nếu chỉ có 1 connection)
- `charUuid`: UUID của characteristic cần notify
- `data`: Data to send
- `len`: Data length

**Implementation**:

1. **Send notification**:
   ```c
   iot_err_t IOT_BleNotifyToCharacteristic(
       const char *addr,
       const uint8_t *charUuid,
       const uint8_t *data,
       uint16_t len)
   {


   }
   ```

---

## 3. Internal Helper Functions

### 3.1 GAP Event Callback

Đây là callback chính nhận tất cả GAP events từ platform:

```c
static int internalGapCb(struct platform_gap_event *event, void *arg) {
    if (event == NULL || callbackMgmt == NULL) {
        return 0;
    }
    // ...
    return 0;
}
```

---

## 4. Platform-Specific Considerations

### 4.1 ESP32 với NimBLE

**Threading**:
- NimBLE chạy trong FreeRTOS task riêng
- Callbacks được gọi trong NimBLE task context
- Cần careful với synchronization

**Memory**:
- Sử dụng `malloc/free` cho dynamic allocation
- NimBLE có own mbuf system cho data buffers

**UUID Format**:
- NimBLE support 16-bit, 32-bit, 128-bit UUIDs
- Cần convert từ byte array sang `ble_uuid_t`

**Host lifecycle** (see §2.1):
- `init` prepares the port and host config but does **not** start the host task;
  `start` does, after services are registered.
- Set **`CONFIG_BT_NIMBLE_DYNAMIC_SERVICE=n`.** With services registered statically before
  the host starts, it is dead weight — and with BLE Mesh enabled it is a crash: mesh calls
  `ble_gatts_start()` itself, and with dynamic services on, that second call re-initialises
  the service pool underneath entries already allocated, giving a boot loop. If your board
  config sets it to `y`, turn it off.
- Do not call `ble_svc_gatt_init()` (or other sysinit functions) from your
  service-registration path. It asserts once the host is running; call it once, from
  `init`.

---

### 4.2 nRF52 với SoftDevice
---

### 4.3 Linux với BlueZ

---

## 5. Checklist

Trước khi submit implementation:

- [ ] Tất cả functions trong header đã implement
- [ ] Callback system hoạt động (register/unregister/dispatch)
- [ ] Host-synced callback wired in `init` unconditionally; nothing advertises before sync
- [ ] `start` slot (or `NULL`): idempotent, bounded by `IOT_BLE_START_MAX_BLOCK_MS`, retryable after a failure
- [ ] GATT services register thành công — before the host starts
- [ ] Advertising và scanning hoạt động (on BLE 5.0 builds: through extended advertising sets 2 and 3, legacy PDUs)
- [ ] Notifications được gửi đúng
- [ ] Error handling đúng (return codes)
- [ ] Thread-safe nếu platform support multi-threading

---

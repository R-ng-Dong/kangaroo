# SDK Writer Guide: Architecture Overview

The documents in `docs/sdk/` are the **SDK Writer Guide**: guidance for teams building a partner SDK layer,
such as `raiosdk`, on top of the shared `iotcore`.

This guide explains the **Core Event Contract**, **SDK Integration Model**, **SDK-Owned Responsibilities**, and
**App-Facing API Guidance**. This document orients you to the layer model, DAO ownership rules, boot sequence,
and event dispatch contract before you read the per-domain guides.

### Two ways to build an SDK

- **Path A — build on `sdkcontract/` (recommended).** `sdkcontract` is a component shipped as source. It keeps the
  Device, Group, IR, Smart and Record stores, runs every request that changes cloud-visible state (state commands,
  joins and removals, groups, IR keysets, automations, history) and sends every state and history report. Your SDK
  writes `apply` (drive the hardware), calls `IOT_ContractLocalChange()` when the hardware changed by itself, and
  handles the core events still its own in `onCoreEvent`. Start with
  [../../sdkcontract/README.md](../../sdkcontract/README.md); `examples/esp32-basic` and `examples/custom-attrs` are
  built this way.
- **Path B — your own SDK directly on `iotcore`,** with your own storage. This is the model the rest of this guide
  and guides 1–6 describe, and it is fully supported: `iotcore` never depends on `sdkcontract`. Your SDK must then
  keep every rule in [7_Record_Contract.md](7_Record_Contract.md) and [8_Smart_Contract.md](8_Smart_Contract.md)
  itself.

Sections 4 and 5 below say where the two paths differ.

---

## 1. Key Concepts

- **Core Event Contract**: The event categories, event structures, request context, and parameter meanings emitted by
  `iotcore`.
- **SDK Integration Model**: How a partner SDK sits between `iotcore` and product firmware.
- **SDK-Owned Responsibilities**: DAO ownership, event handling, hardware control, reporting, and state persistence owned
  by the SDK.
- **App-Facing API Guidance**: How the SDK should expose simpler callbacks and APIs to product firmware while hiding raw
  core protocol details.

---

## 2. Guide Map

| Concept | Read |
|---|---|
| Path A (build on `sdkcontract/`) | `sdkcontract/README.md`, then `7_Record_Contract.md` and `8_Smart_Contract.md` for what it keeps for you |
| The rules the cloud and the app rely on (both paths) | `7_Record_Contract.md`, `8_Smart_Contract.md` |
| SDK Integration Model | `0_SDK_Architecture.md` |
| Core Event Contract | `1_Device_Control.md`, `2_Sub_Device_Hub.md`, `3_Smart_Automation.md` |
| SDK-Owned Responsibilities (path B) | `1_Device_Control.md`, `2_Sub_Device_Hub.md`, `3_Smart_Automation.md`, `4_DAO_Guide.md`, `6_Record_History.md` |
| App-Facing API Guidance | `5_App_API_Design.md` |

---

## 3. Layer Stack

```
┌─────────────────────────────────────────────────┐
│         App (product firmware)                  │
│   calls your SDK's public API                   │
└────────────────────┬────────────────────────────┘
                     │  callbacks + batch state API
┌────────────────────▼────────────────────────────┐
│   Your SDK  (e.g. raiosdk)                      │
│   — this is the layer YOU implement —           │
│   path A: apply, hardware input, onCoreEvent    │
│   path B: DAOs, event handling, hardware ctrl   │
└────────────────────┬────────────────────────────┘
                     │  path A: IOT_ContractStart(), apply,
                     │          IOT_ContractLocalChange()
┌────────────────────▼────────────────────────────┐
│   sdkcontract  (path A only, built from source) │
│   stores, Smart, Record, request handling       │
└────────────────────┬────────────────────────────┘
                     │  IOT_DevCoreRegisterEventCb()
                     │  Reporter APIs
                     │  (path B: your SDK calls these directly)
┌────────────────────▼────────────────────────────┐
│   iotcore  (protocol engine)                    │
│   wire parsing, encryption, event routing       │
└────────────────────┬────────────────────────────┘
                     │  iotPlatform abstractions
┌────────────────────▼────────────────────────────┐
│   implplatform/esp32                            │
│   NVS, BLE, WiFi, mesh                          │
└────────────────────┬────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────┐
│   iotcommon                                     │
│   memory, strings — no IoT dependencies         │
└─────────────────────────────────────────────────┘
```

**Your SDK is the layer you implement.** iotcore is provided pre-built (`libiotcore.a`); iotPlatform, implplatform,
iotcommon and sdkcontract are provided as source components. Build `sdkcontract` as shipped: a locally patched copy is
unsupported and is overwritten by the next release.

---

## 4. DAO Ownership

There are three clear ownership rules:

1. **Config DAO (provision state, WiFi credentials, MQTT endpoints, mesh keys) → iotcore-internal.** These are baked into `libiotcore.a`. You never access config storage directly; use the public query APIs (`IOT_CoreIsProvisioned()`, `IOT_CoreGetRootEid()`, etc.).

2. **Device / Group / Smart / IR / Record stores → `sdkcontract` on path A, your SDK on path B.**
   - **Path A:** the contract owns them and every write to them. The Device and IR store headers
     (`sdkcontract/include/dao/IOT_DaoDeviceMgmt.h`, `IOT_DaoIRMgmt.h`) are public and **read-only**; the Group and
     Smart stores are internal. Your SDK keeps only data of its own, in stores of its own.
   - **Path B:** you implement and own these. The `iotPlatform/include/dao/IOT_DaoStorage.h` abstract interface
     gives you a platform-independent key-value store backed by NVS. How you structure your data on top of that is
     entirely your responsibility; [4_DAO_Guide.md](4_DAO_Guide.md) describes the shape.

3. **`IOT_DaoInit()` is platform-only.** It initializes NVS and the in-memory cache, and runs as part of
   `IOT_InitPlatform()`. On path A, `IOT_ContractStart()` brings the contract's stores up. On path B, your SDK
   initializes its own DAOs inside its init function, after `IOT_InitPlatform()`.

---

## 5. Boot Sequence

**Path A** — from your `app_main()`:

1. **`IOT_InitPlatform()`** — initializes NVS, WiFi, BLE, and timers. Must be called first.
2. **Pre-start settings, all optional** — Record rules (`IOT_ContractRecordRegisterRule()`),
   `IOT_ContractIrConfigure()`, `IOT_ContractRecordUseCustomStore()`, `IOT_ContractSubscribe()`, and core's
   `IOT_CoreConfigure*()` calls. Only before Start.
3. **`IOT_ContractStart(&config, &callbacks)`** — brings the stores up, takes the core event callback for every
   category and starts core. Never call `IOT_CoreInit()` or `IOT_DevCoreRegisterEventCb()` yourself: a callback
   registered before Start makes Start fail, and one registered after is refused (`IOT_ERR_INVALID_STATE`). If Start
   fails while a firmware update is pending, discard the update (`IOT_CoreOtaDiscardPending()`) and restart; see
   `sdkcontract/README.md`, *Start*.

**Path B** — from your `app_main()`:

1. **`IOT_InitPlatform()`** — initializes NVS, WiFi, BLE, and timers. Must be called first.

2. **Your DAO init calls** — initialize your SDK-owned DAO namespaces (device, group, smart, etc.)

3. **`IOT_DevCoreRegisterEventCb()`** — register your event handlers before core starts (see Event Dispatch Model below)

4. **`IOT_CoreInit(modelId)`** — starts the protocol engine. If not yet provisioned, BLE advertising and LAN config start automatically. If already provisioned, WiFi and MQTT connect automatically.

**Note:** Calling `IOT_CoreInit()` without `IOT_InitPlatform()` first results in undefined behavior — platform function pointers are NULL.

---

## 6. Event Dispatch Model

iotcore parses the wire protocol and fires structured events on your SDK. On path A the contract registers for
every category and hands your `onCoreEvent` the events it does not handle itself (the table is in
`sdkcontract/README.md`, *Forwarded events*). On path B you register one callback per event category using:

```c
iot_err_t IOT_DevCoreRegisterEventCb(IOT_CoreEventCategory_t category,
                                     IOT_CoreEventCb_t callback,
                                     void *arg);
```

The categories are:

| Category | `IOT_CoreEventCategory_t` value | What it covers |
|---|---|---|
| State | `IOT_EVENT_STATE` | GET/SET attribute commands, attribute history (record) requests |
| Device | `IOT_EVENT_DEVICE` | Lifecycle, device join/remove, group bind/unbind |
| Mesh | `IOT_EVENT_MESH` | Mesh-related protocol events from cloud (reserved) |
| Setting | `IOT_EVENT_SETTING` | Configuration changes (e.g. WiFi SSID update) |
| Smart | `IOT_EVENT_SMART` | Automation bind/unbind, trigger evaluation, schedule fire |
| OS | `IOT_EVENT_OS` | System-level events (e.g. reboot command, OTA) |
| IR | `IOT_EVENT_IR` | IR commands for your IR hardware |
| Service | `IOT_EVENT_SERVICE` | A service message from your own backend |

They run from `IOT_EVENT_STATE` to `IOT_EVENT_CATEGORY_LAST` with no gaps, so a loop to it registers every category,
including one appended in a later release. Keep a `default:` in your event `switch` for the same reason.

**Threading model:** Events are dispatched on iotcore's internal receive task. Your callback runs on that task. Do your DAO reads/writes and build your response inside the callback. For operations that must not block the receive path — for example, evaluating all automations after a state change — schedule a deferred FreeRTOS timer callback from within the event handler and return immediately.

---

## 7. Two Response Patterns

**Path B.** On path A the contract answers every request it handles, and you call none of the response functions
below for those events.

Not all events require an explicit response call. The pattern depends on whether the cloud is waiting for a DAO-completed acknowledgement:

| Pattern | When to use | How |
|---|---|---|
| **Inline-ack** | State SET commands, lifecycle events — iotcore sends the protocol ACK before your callback fires | Do DAO work and actuate hardware fire-and-forget inside the callback. There is no response function to call. |
| **DAO-completed** | Device join (`DEV_EVENT_NEW_DEVICE_JOINED`), device remove (`DEV_EVENT_DEVICE_REMOVED`), group bind/unbind (`DEV_EVENT_GROUP_BIND` / `DEV_EVENT_GROUP_UNBIND`), device attribute set (`DEV_EVENT_DEVICE_ATTR_SET`), smart bind/unbind trigger and cmd events — the cloud waits for your result | Call `IOT_CoreRespondDeviceCmd()`, `IOT_CoreRespondSmartCmd()`, or `IOT_CoreRespondSmartCheckSync()` after completing your DAO work. Failing to call these leaves the cloud waiting until it times out. |
| **Query response** | `IOT_EVENT_TYPE_STATE_GET`, `IOT_EVENT_TYPE_STATE_RECORD` — cloud is waiting for current device state or attribute history | Read from DAO, call `IOT_CoreRequestReportState()` or `IOT_CoreRequestRecordResponse()` with the data (history replies synchronously, inside the callback). Failing to respond leaves the cloud request unanswered. The legacy `IOT_EVENT_TYPE_STATE_SENSOR_LOG` reply is not supported by this release's core library — see the Device Control Guide, section 3. |

Which events use which pattern is documented in the per-domain guides that follow.

---

## 8. `IOT_RequestContext_t` Rule

Every request event carries an `IOT_RequestContext_t ctx` field. This bundles the transport protocol, sender identity, transaction ID, and response-routing metadata into a single opaque struct.

Three rules govern how your SDK handles it:

1. **Never unpack or repack individual fields.** The internal layout of `IOT_RequestContext_t` may change. Treat it as an opaque token.

2. **Pass it through as-is** when building a report or response:
   ```c
   IOT_CoreStateReportParams_t report = {
       .ctx = event->evt.state.data.setRequest.ctx,
       // ... other fields
   };
   ```

3. **The only exception** is reading `ctx.senderFrom` for policy decisions. For example, checking whether a SET came from an automation (`IOT_SENDER_AUTOMATION`, `IOT_SENDER_AUTOMATION_UUID_V4`, `IOT_SENDER_SCHEDULE`, `IOT_SENDER_SCHEDULE_UUID_V4`) to decide whether to skip re-evaluating automation triggers — which would cause a feedback loop.

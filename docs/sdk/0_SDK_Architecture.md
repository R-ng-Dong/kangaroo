# SDK Writer Guide: Architecture Overview

The documents in `docs/sdk/` are the **SDK Writer Guide**: guidance for teams building a partner SDK layer,
such as `raiosdk`, on top of the shared `iotcore`.

This guide explains the **Core Event Contract**, **SDK Integration Model**, **SDK-Owned Responsibilities**, and
**App-Facing API Guidance**. This document orients you to the layer model, DAO ownership rules, boot sequence,
and event dispatch contract before you read the per-domain guides.

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
| SDK Integration Model | `0_SDK_Architecture.md` |
| Core Event Contract | `1_Device_Control.md`, `2_Sub_Device_Hub.md`, `3_Smart_Automation.md` |
| SDK-Owned Responsibilities | `1_Device_Control.md`, `2_Sub_Device_Hub.md`, `3_Smart_Automation.md`, `4_DAO_Guide.md` |
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
│   DAO management, event handling, hardware ctrl │
└────────────────────┬────────────────────────────┘
                     │  IOT_DevCoreRegisterEventCb()
                     │  Reporter APIs
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

**Your SDK is the layer you implement.** iotcore, iotPlatform, implplatform, and iotcommon are provided as pre-built components.

---

## 4. DAO Ownership

There are three clear ownership rules:

1. **Config DAO (provision state, WiFi credentials, MQTT endpoints, mesh keys) → iotcore-internal.** These are baked into `libiotcore.a`. You never access config storage directly; use the public query APIs (`IOT_CoreIsProvisioned()`, `IOT_CoreGetRootEid()`, etc.).

2. **Device / Group / Smart / IR / SensorLog DAO → SDK-owned.** You implement and own these. The `iotPlatform/include/dao/IOT_DaoStorage.h` abstract interface gives you a platform-independent key-value store backed by NVS. How you structure your data on top of that is entirely your responsibility.

3. **`IOT_DaoInit()` is platform-only.** It initializes NVS and the in-memory cache. Your SDK is responsible for initializing its own DAOs inside your SDK init function, after `IOT_DaoInit()` has already run as part of `IOT_InitPlatform()`.

---

## 5. Boot Sequence

Call these in order from your `app_main()`:

1. **`IOT_InitPlatform()`** — initializes NVS, WiFi, BLE, and timers. Must be called first.

2. **Your DAO init calls** — initialize your SDK-owned DAO namespaces (device, group, smart, etc.)

3. **`IOT_DevCoreRegisterEventCb()`** — register your event handlers before core starts (see Event Dispatch Model below)

4. **`IOT_CoreInit(modelId)`** — starts the protocol engine. If not yet provisioned, BLE advertising and LAN config start automatically. If already provisioned, WiFi and MQTT connect automatically.

**Note:** Calling `IOT_CoreInit()` without `IOT_InitPlatform()` first results in undefined behavior — platform function pointers are NULL.

---

## 6. Event Dispatch Model

iotcore parses the wire protocol and fires structured events on your SDK. You register one callback per event category using:

```c
iot_err_t IOT_DevCoreRegisterEventCb(IOT_CoreEventCategory_t category,
                                     IOT_CoreEventCb_t callback,
                                     void *arg);
```

The six categories are:

| Category | `IOT_CoreEventCategory_t` value | What it covers |
|---|---|---|
| State | `IOT_EVENT_STATE` | GET/SET attribute commands, sensor log requests |
| Device | `IOT_EVENT_DEVICE` | Lifecycle, device join/remove, group bind/unbind |
| Mesh | `IOT_EVENT_MESH` | Mesh-related protocol events from cloud (reserved) |
| Setting | `IOT_EVENT_SETTING` | Configuration changes (e.g. WiFi SSID update) |
| Smart | `IOT_EVENT_SMART` | Automation bind/unbind, trigger evaluation, schedule fire |
| OS | `IOT_EVENT_OS` | System-level events (e.g. reboot command) |

**Threading model:** Events are dispatched on iotcore's internal receive task. Your callback runs on that task. Do your DAO reads/writes and build your response inside the callback. For operations that must not block the receive path — for example, evaluating all automations after a state change — schedule a deferred FreeRTOS timer callback from within the event handler and return immediately.

---

## 7. Two Response Patterns

Not all events require an explicit response call. The pattern depends on whether the cloud is waiting for a DAO-completed acknowledgement:

| Pattern | When to use | How |
|---|---|---|
| **Inline-ack** | State SET commands, lifecycle events — iotcore sends the protocol ACK before your callback fires | Do DAO work and actuate hardware fire-and-forget inside the callback. There is no response function to call. |
| **DAO-completed** | Device join (`DEV_EVENT_NEW_DEVICE_JOINED`), device remove (`DEV_EVENT_DEVICE_REMOVED`), group bind/unbind (`DEV_EVENT_GROUP_BIND` / `DEV_EVENT_GROUP_UNBIND`), device attribute set (`DEV_EVENT_DEVICE_ATTR_SET`), smart bind/unbind trigger and cmd events — the cloud waits for your result | Call `IOT_CoreRespondDeviceCmd()`, `IOT_CoreRespondSmartCmd()`, or `IOT_CoreRespondSmartCheckSync()` after completing your DAO work. Failing to call these leaves the cloud waiting until it times out. |
| **Query response** | `IOT_EVENT_TYPE_STATE_GET`, `IOT_EVENT_TYPE_STATE_SENSOR_LOG` — cloud is waiting for current device state or sensor log data | Read from DAO, call `IOT_CoreRequestReportState()` or `IOT_CoreRequestSensorLogResponse()` with the data. Failing to respond leaves the cloud request unanswered. |

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

# 5 — App-Facing API Guidance

**SDK Writer Guide concepts:** App-Facing API Guidance.

**Audience:** SDK writers building their own SDK layer (e.g., `raiosdk/`) on top of iotcore.

**Purpose:** Guidance on designing the public API surface that your app-writer customers will use. This doc covers what
to expose, what to hide, and how to simplify iotcore's protocol concepts into clean, hardware-centric abstractions.

> **Reference implementations:** `IOT_SDKEvents.h` and `IOT_HubEvents.h` are shipped with this distribution as reference designs. They show Rogo's own choices for type names, event structures, and callback signatures. You may adopt them directly or design your own equivalents — either is valid. The principles in this doc apply regardless.

---

## 1. The Principle

Your SDK is a translation layer between iotcore's protocol world and the product firmware's hardware world. App writers (the product developers using your SDK) should never see `IOT_CoreEvent_t`, `IOT_RequestContext_t`, or any DAO types. Your SDK absorbs the protocol complexity and exposes clean, hardware-centric callbacks.

**Rule:** if an app writer needs to know about BLE provisioning internals, MQTT routing, or NVS namespaces to use your SDK, the API is leaking.

---

## 2. What to Expose

| SDK concern | App-facing equivalent | Why expose |
|---|---|---|
| `IOT_LIFECYCLE_EVENT_*` | Lifecycle callback with simplified event type + `isProvisioned` bool | App needs to know when to start hardware, show provision LED, and perform cleanup on device removal |
| `IOT_NWK_*` | Network status callback with WiFi/cloud state | App shows connectivity LED, drives status indicators, and may log network events |
| Device control (from STATE_SET) | Hardware actuation callback: `elemId` + `attrId` + decoded value + control source | App actuates hardware (relay, PWM, motor) — this is the core product function |
| `IOT_MeshEvent_t` | Mesh node join/leave callback (hub devices only) | App may want to track topology for logging or LED indicators; mesh address is the only field needed |
| State push | Batch state update function: app provides `elemId` + `attrId` + value → SDK updates DAO + reports to cloud | App reports button presses, sensor readings, and local state changes without knowing anything about MQTT or message building |
| State read | Batch state read function: app provides `elemId` + `attrId` → SDK returns current DAO value | App restores hardware state on boot from the SDK's persistent record — app never touches NVS directly |

---

## 3. What to Hide

**Raw event types** (`IOT_CoreStateEvent_t`, `IOT_CoreDeviceEvent_t`, etc.) — app writers have no use for protocol-level event structures. Exposing them leaks iotcore version details into app code: any future change to `IOT_CoreStateEvent_t` would require app-writer recompilation and code changes.

**`IOT_RequestContext_t`** — this struct bundles transport protocol, sender UUID, TID, and request correlation ID. These are routing internals the app cannot act on. Exposing `IOT_RequestContext_t` forces app writers to understand MQTT/BLE routing just to implement a light switch. It also creates a contract: once exposed, you can never change the struct layout without breaking app code.

**DAO APIs** — app writers must not touch persistent storage directly. Your SDK is the source of truth for device state. If the app writes NVS directly and your SDK caches stale values, state diverges and cloud reports become wrong. All persistence goes through your SDK's state push/read functions.

**Smart/group/mesh provisioning internals** — trigger binding, group management, and mesh key distribution are protocol-level operations your SDK handles completely. The app should never know a `smid` exists, what `IOT_SET_TARGET_GROUP` means, or how mesh devKeys are derived. If an app writer could accidentally call bind-trigger logic, they could corrupt automation state.

**`IOT_CoreRespondDeviceCmd()` / reporter APIs** — response routing is your SDK's responsibility. If the app could call these, it could send duplicate responses, respond on the wrong transport, or corrupt the protocol flow. The app actuates hardware and calls your state update function; your SDK handles all iotcore reporter calls.

---

## 4. Simplification Patterns

### Collapsing Sender Types

iotcore identifies the origin of every control request with a `senderFrom` byte. There are over a dozen `IOT_SENDER_*` values. App writers use this information for LED feedback or logging — they don't need to distinguish Google Home from Alexa, or a UUID-v4 automation from a legacy automation. Map all values to four app-facing categories:

| `IOT_SENDER_*` constants | Maps to |
|---|---|
| `IOT_SENDER_CONTROL` (0x01) | `CONTROL_FROM_LOCAL` |
| `IOT_SENDER_APP` (0x0D), `IOT_SENDER_IOS` (0x0F), `IOT_SENDER_ANDROID` (0x10) | `CONTROL_FROM_APP` |
| `IOT_SENDER_CLOUD` (0x0E), `IOT_SENDER_SCENARIO_UUID_V4` (0x13), `IOT_SENDER_AUTOMATION` (0x14), `IOT_SENDER_AUTOMATION_UUID_V4` (0x15), `IOT_SENDER_SCHEDULE` (0x16), `IOT_SENDER_SCHEDULE_UUID_V4` (0x17), `IOT_SENDER_COUNTDOWN` (0x18), `IOT_SENDER_GOOGLE_VOICE` (0x20), `IOT_SENDER_ALEXA` (0x21), `IOT_SENDER_OLLI` (0x22), `IOT_SENDER_THIRD_PARTY` (0x29) | `CONTROL_FROM_CLOUD` |
| `IOT_SENDER_DEVICE` (0x11), `IOT_SENDER_DEVICE_SYNC` (0x12) | `CONTROL_FROM_DEVICE` |
| `IOT_SENDER_UNKNOWN` (0x00) | `CONTROL_FROM_LOCAL` (fallback) |

`IOT_SENDER_UNKNOWN` maps to `CONTROL_FROM_LOCAL` as a safe fallback — it is most likely a local or internal command with no sender identification.

The reference design in `IOT_SDKEvents.h` implements this as `IOT_ControlSource_t` with four values (`IOT_CONTROL_FROM_LOCAL`, `IOT_CONTROL_FROM_APP`, `IOT_CONTROL_FROM_CLOUD`, `IOT_CONTROL_FROM_DEVICE`).

### Single Control Callback Per Element

Don't expose a raw SET event with an element list. It forces app writers to understand `dev_set_info_t`, iterate `elemList`, and decode big-endian attribute bytes. Instead, your SDK should iterate the element list internally and dispatch one callback per element, already resolved to a scalar value:

```c
// App-facing control callback (example design):
typedef struct {
    uint16_t eid;               // device EID (root device only)
    uint16_t elemId;            // which element
    uint16_t attrId;            // which attribute
    uint32_t value;             // decoded scalar value
    YourControlSource_t source; // LOCAL / APP / CLOUD / DEVICE
} YourDeviceControlRequest_t;

typedef void (*YourDeviceControlCb_t)(const YourDeviceControlRequest_t *req);
```

This works for scalar attributes (ONOFF, BRIGHTNESS, FAN_SPEED, COLOR_TEMP, etc.). For compound attributes (COLOR_HSV, AC state), provide typed decoder helpers that the app calls if it registers a handler for those attribute IDs.

### Opaque Lifecycle Events

App writers receive a simple `BOOTED` event with an `isProvisioned` bool — no BLE provisioning state machine is visible. `PROVISION_COMPLETE` and `DEVICE_REMOVED` fire as simple named events. The app never knows that provisioning happened over BLE GATT or that device removal wiped NVS.

```c
// Example lifecycle event design:
typedef enum {
    YOUR_LIFECYCLE_BOOTED,
    YOUR_LIFECYCLE_PROVISION_COMPLETE,
    YOUR_LIFECYCLE_PROVISION_CANCELLED,
    YOUR_LIFECYCLE_DEVICE_REMOVED,
    YOUR_LIFECYCLE_IDENTIFY,
} YourLifecycleEvent_t;

typedef void (*YourLifecycleCb_t)(YourLifecycleEvent_t event, bool isProvisioned);
```

The reference design in `IOT_SDKEvents.h` implements this as `IOT_LifecycleEventType_t` and `IOT_LifecycleCb_t`.

### No Context Pass-Through

App callbacks carry only actuation data. `IOT_RequestContext_t` stays entirely inside your SDK — it is used to route the iotcore response, but the app never sees it. The app never needs to know which transport a command arrived on.

---

## 5. Failure Contracts to Document for App Writers

These contracts must appear verbatim (or equivalent) in your SDK's public header file comments. App writers need to understand the consequence of each failure before they write their hardware actuation code.

**Control callback:** returning a non-zero error code means hardware actuation failed. Your SDK has already updated the DAO state and will report the new value to cloud regardless. To correct a mismatch between cloud state and actual hardware state, call your batch state update function with the actual current hardware value immediately after detecting the failure.

**DEVICE_REMOVED callback:** your SDK wipes all persistent data and restarts the chip immediately after this callback returns. Use this callback for final cleanup only — turn off outputs, flush remote logs, reset peripherals. Do NOT call `esp_restart()` yourself; the SDK does it.

**Factory reset function:** does not return. Perform any required cleanup (output shutdown, flush) before calling it.

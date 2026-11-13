# Design the API your app writers use

**Both paths.** Your SDK is a translation layer between core's protocol world and the product
firmware's hardware world. The product developers who build on your SDK (app writers) should never
see `IOT_CoreEvent_t`, `IOT_RequestContext_t` or a store type: your SDK absorbs the protocol and
exposes hardware-centric callbacks. On path A your app-facing surface sits on `apply`,
`IOT_ContractReport()` and the read-only Device store instead of on core events and your own stores.

> **The names below are illustrative.** No shipped header declares them; design your own
> equivalents. The principles hold whatever you call them.

**The test:** if an app writer must know about BLE provisioning, cloud routing or storage
namespaces to use your SDK, the API is leaking.

## What to expose

| Core concept | App-facing equivalent | Why |
|---|---|---|
| `DEV_EVENT_BOOTED`, the setup events, `DEV_EVENT_DEVICE_REMOVED`, `DEV_EVENT_IDENTIFICATION` | A lifecycle callback with a simple event type and an `isProvisioned` flag | Start the hardware, show a setup LED, clean up on removal |
| `DEV_EVENT_NETWORK_*`, `DEV_EVENT_CLOUD_*`, `DEV_EVENT_WIFI_RECOVERY_*` | A connectivity callback (WiFi, cloud) | Status LEDs and indicators |
| A state command (`IOT_EVENT_TYPE_STATE_SET`, or `apply` on path A) | A control callback: element, attribute, decoded value, source | Actuate the relay, the dimmer, the motor: the product's core function |
| A state report | A "state changed" function: element, attribute, value | Report a button or a sensor without knowing anything about messages |
| A store read | A "read state" function: element, attribute → current value | Read the stored state without touching storage |

## What to hide

- **Raw event types** (`IOT_CoreStateEvent_t`, `IOT_CoreDeviceEvent_t`, …). Exposing them ties app
  code to core's version: a change in an event struct becomes the app writer's recompile.
- **`IOT_RequestContext_t`.** Routing the app cannot act on; once exposed, its layout is frozen.
- **Store APIs.** Your SDK is the source of truth for device state. An app that writes storage
  directly makes the stored state and the reports diverge.
- **Smart, groups and mesh keys.** Protocol operations your SDK handles completely. An app should
  never know an SMID exists or what `IOT_SET_TARGET_GROUP` means.
- **Core's report and answer functions.** Routing answers is your SDK's job; an app that could call
  them could answer twice or on the wrong transport.

## Simplification patterns

### Collapse the sender types

Core identifies the origin of every command with `ctx.senderFrom` (`IOT_SENDER_*`, over a dozen
values). An app writer needs it for feedback or logging, not to tell one voice assistant from
another. Map them to four sources:

| `IOT_SENDER_*` | App-facing source |
|---|---|
| `IOT_SENDER_CONTROL`, `IOT_SENDER_UNKNOWN` (fallback) | Local |
| `IOT_SENDER_APP`, `IOT_SENDER_IOS`, `IOT_SENDER_ANDROID` | App |
| `IOT_SENDER_CLOUD`, `IOT_SENDER_SCENARIO_UUID_V4`, `IOT_SENDER_AUTOMATION`, `IOT_SENDER_AUTOMATION_UUID_V4`, `IOT_SENDER_SCHEDULE`, `IOT_SENDER_SCHEDULE_UUID_V4`, `IOT_SENDER_COUNTDOWN`, `IOT_SENDER_GOOGLE_VOICE`, `IOT_SENDER_ALEXA`, `IOT_SENDER_OLLI`, `IOT_SENDER_THIRD_PARTY` | Cloud |
| `IOT_SENDER_DEVICE`, `IOT_SENDER_DEVICE_SYNC` | Device |

On path A the contract already does this: `IOT_ContractApply_t.source` is an
`IOT_ContractSource_t`.

### One control callback per element

Do not hand the app a SET event with an element list: it would have to iterate `elemList` and
decode big-endian attribute bytes. Iterate inside your SDK and call the app once per element, with
the value already decoded:

```c
typedef struct {
    uint16_t elemId;            // which element
    uint16_t attrId;            // which attribute
    uint32_t value;             // decoded scalar value
    YourControlSource_t source; // LOCAL / APP / CLOUD / DEVICE
} YourDeviceControlRequest_t;

typedef iot_err_t (*YourDeviceControlCb_t)(const YourDeviceControlRequest_t *req);
```

This covers scalar attributes (on/off, brightness, fan speed, colour temperature). For compound
ones (colour, an air conditioner's state) give the app typed decoders.

### Opaque lifecycle events

The app receives a simple `BOOTED` with an `isProvisioned` flag; setup complete and device removed
are plain named events. It never learns that setup ran over BLE or that a removal wiped storage.

```c
typedef enum {
    YOUR_LIFECYCLE_BOOTED,
    YOUR_LIFECYCLE_PROVISION_COMPLETE,
    YOUR_LIFECYCLE_PROVISION_CANCELLED,
    YOUR_LIFECYCLE_DEVICE_REMOVED,
    YOUR_LIFECYCLE_IDENTIFY,
} YourLifecycleEvent_t;

typedef void (*YourLifecycleCb_t)(YourLifecycleEvent_t event, bool isProvisioned);
```

## Failure contracts to write into your headers

App writers need each consequence before they write their actuation code; state these in your
public header comments.

- **Control callback:** what a non-zero return means. On path A an error means the value is not
  committed (not stored, recorded or reported); say so, and say that the app reports the hardware's
  actual value with your "state changed" function when the two differ.
- **Device removed:** whether your SDK restarts the chip after the callback returns. If it does,
  the callback is for final cleanup only (outputs off, flush), and the app must not restart itself.
- **Factory reset:** whether your reset function returns. If it does not, the app finishes its
  cleanup before calling it.

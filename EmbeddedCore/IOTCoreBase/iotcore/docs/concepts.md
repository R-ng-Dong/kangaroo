# Concepts

What `iotcore` is responsible for, what an SDK built on it owns, and how an event flows from the
cloud or the app to your code and back. Read [getting-started.md](getting-started.md) first if you
have not built a device yet; [events.md](events.md) has every event and
[reference.md](reference.md) every name.

## The layers

```
┌──────────────────────────────────────────────────────────────┐
│ Cloud / mobile app                                           │
└──────────────────────────────┬───────────────────────────────┘
                               │ messages (BLE, local network, cloud)
┌──────────────────────────────▼───────────────────────────────┐
│ iotcore — protocol engine, prebuilt library                  │
│ parses and answers messages, provisioning, the cloud session │
│ raises events; builds and sends every report you ask for     │
└──────────────────────────────┬───────────────────────────────┘
                               │ events ↓        reports, answers ↑
┌──────────────────────────────▼───────────────────────────────┐
│ path A: sdkcontract (shipped source, built as-is)            │
│         stores, Smart, Record, every state and history report│
│ path B: your SDK does all of that itself                     │
└──────────────────────────────┬───────────────────────────────┘
                               │ apply / your callbacks
┌──────────────────────────────▼───────────────────────────────┐
│ Your SDK and product: hardware, inputs, what the app writer  │
│ sees                                                         │
└──────────────────────────────────────────────────────────────┘
        everything above runs on iotPlatform (interfaces) →
        implplatform (one chip or OS) → iotcommon (utilities)
```

Calls go **down**, events come **up**. Core never calls an SDK function by name: it raises events
to the callbacks registered for each category, and your code calls core's public functions to
report and answer. `iotPlatform` and `iotcommon` depend on nothing above them, and core depends on
no SDK. Who works on which layer is in [roles.md](roles.md).

**The SDK provides data, core builds messages.** You never assemble a message: you hand core the
device's state, the history blocks or the status as structs, and core encodes, routes and sends
them. The wire format is core's alone and is not published.

## Two ways to build an SDK

- **Path A — build on `sdkcontract` (recommended).** `IOT_ContractStart()` registers every event
  category and starts core; you write `apply` and report your hardware's own changes. Start at
  [sdkcontract's README](../../sdkcontract/README.md). On path A you never call `IOT_CoreInit()` or
  `IOT_DevCoreRegisterEventCb()`, nor core's state or history report functions.
- **Path B — your own SDK directly on core.** You register a callback per category, start core,
  keep your own stores and answer every request yourself. The Record and Smart rulebooks
  ([record.md](../../sdkcontract/docs/record.md), [smart.md](../../sdkcontract/docs/smart.md)) are
  the rules you must keep; the how-to pages here show each piece.

Both paths use the same events; on path A the contract handles some of them and passes the rest to
your `onCoreEvent` ([the table](../../sdkcontract/docs/how-to/core-events.md)).

## Ownership

| Who | Owns |
|---|---|
| Core | Message parsing and encoding, transports, encryption, provisioning, WiFi and the cloud session, its own configuration (identity, credentials, location), the event categories and every report's format |
| `sdkcontract` (path A) or your SDK (path B) | The Device, Group, IR, Smart and Record stores; running state commands, joins, removals, groups, automations and history; every state and history report |
| Your SDK, both paths | Driving the hardware, reporting its own changes, the device status, reboot and update, IR send and learn, mesh, identify, messages from your own backend, any store of your own |
| The platform | Storage, network, BLE, timers, the OS ([iotPlatform](../../iotPlatform/README.md)) |

Core's configuration is reached only through `IOT_Core.h` (`IOT_CoreIsProvisioned()`,
`IOT_CoreGetRootEid()`, `IOT_CoreGetRootDeviceId()`, `IOT_CoreFactoryReset()`), never through its
storage. Attributes mean nothing to core: it hands you an id and raw bytes and sends back the bytes
you report ([how-to/attributes.md](how-to/attributes.md)).

## The event flow

### Categories

| Category | What it carries | Header |
|---|---|---|
| `IOT_EVENT_STATE` | State commands (SET) and queries (GET), history queries | `IOT_CoreEventState.h` |
| `IOT_EVENT_DEVICE` | Boot, setup, network, cloud, WiFi recovery, joins, removals, groups, identify, status, Zigbee | `IOT_CoreEventDevice.h` |
| `IOT_EVENT_MESH` | Mesh keys from the cloud | `IOT_CoreEventMesh.h` |
| `IOT_EVENT_SETTING` | New WiFi credentials | `IOT_CoreEventSetting.h` |
| `IOT_EVENT_SMART` | Automations, triggers, schedules, sync-on-off, condition groups | `IOT_CoreEventSmart.h` |
| `IOT_EVENT_OS` | Reboot and update requests, the update boot | `IOT_CoreEventOs.h` |
| `IOT_EVENT_IR` | IR send, learn, keysets, capability | `IOT_CoreEventIr.h` |
| `IOT_EVENT_SERVICE` | Messages from your own backend | `IOT_CoreEventService.h` |

Categories run from `IOT_EVENT_STATE` to `IOT_EVENT_CATEGORY_LAST` with no gaps, so a loop to it
registers every category, including one a later release appends. Keep a `default:` in every
`switch` for the same reason. One callback per category; `IOT_DevCoreRegisterEventCb()` refuses a
second.

### Which task, and when

Every handler runs on one of three tasks:

- **Core's task, in order** — lifecycle events (setup, network, cloud, WiFi recovery, Zigbee, WiFi
  changed). Queued and delivered one at a time in the order core raised them; a slow handler delays
  the next one but never reorders them.
- **Core's task, as part of a request** — anything the cloud or the app asks (state, Smart, IR, mesh
  key, identify, status, reboot, update, service message, joins, groups). Handled synchronously: the
  next request waits for your handler, so return quickly and hand slow work to a task of your own.
- **The calling task** — an event your own call caused, before that call returns:
  `DEV_EVENT_BOOTED` or `OS_EVENT_OTA_BOOT_STARTED` inside `IOT_CoreInit()`, `DEV_EVENT_FACTORY_RESET`
  inside `IOT_CoreFactoryReset()`, any event you raise with `IOT_CoreDispatchEvent()`.

So your handlers run on more than one task: guard what they share. Every boot begins the same way:
`DEV_EVENT_BOOTED` first (a normal boot; `IOT_CoreIsProvisioned()` already holds this boot's answer
inside it), or `OS_EVENT_OTA_BOOT_STARTED` instead on a boot that exists only to run an update; then
setup, network and cloud events in the order they happen. A setup's `DEV_EVENT_PROVISION_STARTED`
always ends in exactly one of CANCELLED, FAILED or COMPLETED; a cloud attempt's CONNECTING in exactly
one of CONNECTED or CONNECT_FAILED.

Never block in a timer callback, and never call a store, a report or a publish from one: those
block. A timer only re-arms, sets a flag or hands work to core's task with `IOT_CoreDeferWork()`.

### Borrowed data

Every pointer inside an event — `ctx.senderId`, `ctx.requestId`, payloads, keys, lists — is valid
only until your handler returns. Copy what you keep. To answer later from another task (an IR learn
window), deep-copy the request context, including the bytes its pointers point to.

### Requests are claimed and answered once

A notice needs nothing back. A **request** is answered exactly once, in one of two ways:

- **A field you write before returning.** Core presets it, reads it when your handler returns and
  answers the sender from it: `IOT_OK` is success, any other code is failure with that code.
  The preset is `IOT_ERR_NOT_SUPPORTED` — so an unhandled request, or a category with no
  callback, is answered "not supported" — except the mesh key, preset to `IOT_OK` because the app
  abandons setup when one is refused.

  | Event | Field |
  |---|---|
  | `IOT_EVENT_TYPE_STATE_SET` | `data.setRequest.result` |
  | `DEV_EVENT_IDENTIFICATION` | `data.identification.result` |
  | `DEV_EVENT_DEVICE_STATUS_REQUEST` | `data.deviceStatusRequest.result` (the return of `IOT_CoreRespondDeviceStatus()`) |
  | `MESH_EVENT_KEYS_RECEIVED` | `data.keys.result` (preset `IOT_OK`) |
  | `OS_EVENT_REBOOT_REQUESTED` | `data.rebootRequested.result` — core never restarts the chip for you |
  | `OS_EVENT_OTA_REQUESTED` | `data.otaRequested.claim`: `IOT_CORE_OTA_CLAIM_NOW`, `_AFTER_REBOOT`, or left `_NONE` |
  | `OS_EVENT_OTA_BOOT_STARTED` | `data.otaBootStarted.handled = true`, or core marks the update failed and restarts |
  | `IOT_EVENT_IR` | `evt.ir.result`; `capMode` / `capDetect` for the capability question |
  | `IOT_EVENT_SERVICE` | `evt.service.result` |

- **A call you make before returning** — a state query (`IOT_CoreRequestReportState()`), a
  history query (`IOT_CoreRequestRecordResponse()`), a device command (`IOT_CoreRespondDeviceCmd()`),
  a Smart command (`IOT_CoreRespondSmartCmd()`, `IOT_CoreRespondSmartCheckSync()`). The
  `IOT_CoreRespond*()` calls send nothing when `ctx.requiresResponse` is false, so you may call them
  unconditionally; an unacknowledged command is never answered, whatever you do.

Answer before you return and never restart inside a handler: core sends the answer after your
handler returns, so arrange a restart a moment later (a short timer) and then claim the request.

### The request context

`IOT_RequestContext_t ctx` travels with every request: the transport, the sender, the transaction.
Treat it as an opaque token and pass it unchanged into the report or answer (`.ctx = setData->ctx`).
The one field you may read is `senderFrom`, for policy.

### Loop prevention

A state change caused by Smart's own output must not be fed back to trigger evaluation, or a trigger
fires a command that fires the trigger again. The rule is numeric: a change whose
`senderFrom >= IOT_SENDER_SCENARIO_UUID_V4` (scenario, automation, schedule, countdown, voice
assistants, third party) is not evaluated; a change from the cloud, the app, a phone, a local
control or the device itself is. When your own automation dispatches a command, send it with
`IOT_SENDER_AUTOMATION`. On path A the contract applies this rule
([Smart rulebook](../../sdkcontract/docs/smart.md), rule 4).

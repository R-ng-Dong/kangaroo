# Role Boundaries: Core, SDK, App, Platform, and Common

Who works on which layer, what each layer owns, and where each writer's documentation lives. The
layers themselves and the event flow between them are in [concepts.md](concepts.md).

The same source tree contains several layers, but each layer serves a different writer:

```text
App writer
  uses SDK public APIs and callbacks

SDK writer
  handles core events, owns product/device behavior, exposes app-facing APIs

Core writer
  owns protocol parsing, event dispatch, routing, and public core contracts

Platform writer
  implements OS, network, BLE, timers, DAO storage, and hardware adapters

Common writer
  owns foundation utilities with no IoT dependencies
```

---

## Layer Map

| Layer | Directory | Primary writer | Responsibility |
|---|---|---|---|
| App | Product firmware (the ESP-IDF project's `main/`) | App writer | Board behavior, GPIO, user input, hardware actuation, product UI |
| SDK contract | `sdkcontract/` | Shipped component | Device, Group, Smart, IR and Record stores; Smart evaluation; Record history; the state-change entry; device removal and factory reset |
| SDK | Your SDK | SDK writer | Event handling, actuation, product-specific DAOs, app-facing API; built on `sdkcontract/` |
| Core | `iotcore/` | Core writer | Protocol engine, message parsing, event dispatch, request context, reporting APIs |
| Platform interface | `iotPlatform/` | Platform writer | Abstract interfaces used by core and SDK code |
| Platform implementation | `implplatform/` | Platform writer | ESP32/ESP8266/Linux implementations of platform interfaces |
| Common | `iotcommon/` | Common writer | Memory, strings, generic utilities, no IoT-layer dependencies |

---

## Core Writer

A core writer works on `iotcore/`.

Core owns the protocol world:

- Parse wire messages from BLE, TCP, MQTT, LAN, and other transports.
- Convert protocol payloads into structured public events.
- Dispatch events through `IOT_DevCoreRegisterEventCb()`.
- Define the Core Event Contract in public headers.
- Provide reporter/response APIs that route replies correctly.
- Own config DAO data needed by core itself: provisioning state, WiFi credentials, MQTT endpoints, location ID, root EID,
  and related core configuration.

Core should not own product behavior. It should not know how a relay, dimmer, motor, sensor, group DAO, smart DAO, IR
DAO, or product-specific device model works.

Core-writer public contract files are the headers in `iotcore/include/`, mapped by role in
`iotcore/include/README.md`. The ones every SDK touches:

- `IOT_Core.h` — lifecycle (and `IOT_ProvisionMode_t`)
- `IOT_CoreEventHandler.h` — inbound events, umbrella over one header per category
- `IOT_CoreReporter.h` — outbound calls, umbrella over `IOT_CoreState.h`, `IOT_CoreSmart.h`, `IOT_CoreSensorLog.h`, `IOT_CoreIr.h`

When a core writer changes event structs, callback categories, request context fields, response APIs, or target
resolution semantics, SDK-writer docs must be updated.

---

## SDK Writer

An SDK writer builds the SDK layer above `iotcore`, on `sdkcontract/` (path A) or directly on the same core (path B).

SDK owns the product/device world:

- Path A: build `sdkcontract/` as shipped (do not edit it) and start through `IOT_ContractStart()`. The contract
  owns the request that changes cloud-visible state (state commands, joins, groups, removals, Smart, Record) and
  the device, group, smart, IR and record stores, and sends every state and history report. The SDK supplies
  `apply` (actuate local hardware, or send to a sub-device), reports its own hardware changes with
  `IOT_ContractReport()` / `IOT_ContractReportFields()` (`IOT_ContractSubDeviceReport()` for sub-devices),
  and handles the events passed to `onCoreEvent`.
  It never registers core callbacks, starts core or calls core's state/history report functions itself.
- Path B: register core event callbacks, handle every category, keep its own stores, and call core reporter/response
  APIs after its store work is complete. The rules are in the [Record](../../sdkcontract/docs/record.md) and
  [Smart](../../sdkcontract/docs/smart.md) rulebooks.
- Own any product-specific DAOs.
- Expose simpler app-facing APIs and callbacks.

SDK should treat `IOT_RequestContext_t` as routing metadata. Pass it through unchanged to core reporter/response APIs.
The SDK may inspect documented fields for policy decisions, such as avoiding smart-trigger feedback loops, but it should
not rebuild the context.

SDK writers should hide raw core protocol details from app writers. App code should not need to understand
`IOT_CoreEvent_t`, `dev_set_info_t`, group target expansion, MQTT routing, or DAO storage layout.

---

## App Writer

An app writer builds product firmware on top of an SDK.

App owns board behavior:

- Configure GPIO, PWM, ADC, UART, buttons, LEDs, motors, sensors, and other product hardware.
- Register SDK callbacks for lifecycle, network, and control events.
- Apply hardware actuation when the SDK asks.
- Report local state changes through SDK APIs.
- Restore hardware state on boot through SDK state-read APIs.

App should not call raw core event APIs or core reporter APIs. The app should not write SDK DAO storage directly. It
should use SDK public APIs so SDK state, cloud reports, and persistent storage stay consistent.

---

## Platform Writer

A platform writer owns `iotPlatform/` contracts and `implplatform/` implementations.

Platform owns portability:

- OS abstraction and task/timer behavior.
- Network connect/disconnect operations.
- BLE provisioning transport.
- DAO storage and cache primitives.
- Platform-specific utilities and hardware integration needed by core or SDK layers.

Platform code must not depend on `iotcore` or the SDK layer. The dependency direction is downward: core and SDK can call
platform interfaces, but platform implementations should remain reusable underneath them.

---

## Common Writer

A common writer owns `iotcommon/`.

Common owns generic utilities:

- Memory helpers.
- String and buffer helpers.
- Foundation utilities that do not know about IoT protocol concepts.

`iotcommon` must stay independent. It must not include or depend on `iotcore`, the SDK layer, `iotPlatform`, or
`implplatform` APIs.

---

## Shared Vocabulary

| Term | Meaning |
|---|---|
| Core Event Contract | Event categories, event structs, request context, target fields, and parameter meanings emitted by core ([events.md](events.md)) |
| SDK Integration Model | How an SDK sits between `iotcore` and product firmware ([concepts.md](concepts.md)) |
| SDK-Owned Responsibilities | Store ownership, event handling, hardware control, reporting, and state persistence owned by SDK code |
| App-Facing API Guidance | How SDK writers expose simple product APIs while hiding raw core protocol details ([how-to/app-facing-api.md](how-to/app-facing-api.md)) |

---

## Boundary Rules

- `iotcommon` has no IoT dependencies.
- `iotPlatform` and `implplatform` do not depend on `iotcore` or the SDK layer.
- `iotcore` does not depend on the SDK layer.
- Your SDK uses only the public headers in `iotcore/include/` and `sdkcontract/include/`.
- Core's private headers are for core internals only; they are not part of the shipped API.
- SDK-owned business logic belongs under SDK service/event/DAO modules, not inside core handlers.
- Core handlers should parse protocol and dispatch events; they should not become SDK service implementations.

---

## Where each writer reads

Documentation lives with the component that ships it.

| Writer | Start at |
|---|---|
| SDK writer on `sdkcontract` (path A) | [sdkcontract's README](../../sdkcontract/README.md) and its `docs/` |
| SDK writer directly on core (path B) | [iotcore's README](../README.md): [getting-started.md](getting-started.md), [concepts.md](concepts.md), [events.md](events.md), [reference.md](reference.md) |
| Platform writer | [iotPlatform's README](../../iotPlatform/README.md) and its `docs/` |
| App writer | The documentation of the SDK they build on |
| Common writer | The comments in `iotcommon/include/` |

When the same feature crosses layers, document each contract at the layer where that writer makes decisions. For example,
group control has a core event contract, SDK-owned group DAO behavior, and app-facing callbacks; those are related but
not the same audience.

# Documentation index

Start with the question you have, not the folder name.

## "Which way do I build my SDK?"

There are two, and both are supported:

- **Path A — build on `sdkcontract/` (recommended).** The contract keeps the Device, Group, IR,
  Smart and Record stores, runs every request that changes cloud-visible state and sends every
  report. You write `apply` (drive the hardware), report what your hardware did by itself with
  `IOT_ContractLocalChange()`, and handle the core events still yours in `onCoreEvent`. Start at
  **[../sdkcontract/README.md](../sdkcontract/README.md)**; working code is
  **[../examples/esp32-basic/](../examples/esp32-basic/)** (and
  **[../examples/custom-attrs/](../examples/custom-attrs/)** with attributes of your own).
- **Path B — your own SDK directly on `iotcore`,** with your own storage, as before. Nothing
  forces a move. The design guides under *"How should I structure my own SDK?"* below are
  written for this path, and the Record and Smart contract guides are your rulebook.

On both paths, **[sdk/7_Record_Contract.md](sdk/7_Record_Contract.md)** and
**[sdk/8_Smart_Contract.md](sdk/8_Smart_Contract.md)** state the rules the cloud and the app rely
on, and say for each rule what the contract does for you.

## "How do I call the core API?"

- **[core/API_Document.md](core/API_Document.md)** — the full public API: `IOT_Core.h`,
  `IOT_CoreEventHandler.h`, `IOT_CoreReporter.h`. Every function you link against, what each
  event carries, and when to call what. This is the reference you will keep open.
  Every header in `iotcore/include/` is public, and `iotcore/include/README.md` maps each one
  by role. The three every SDK touches:
  - `IOT_Core.h` — lifecycle (and `IOT_ProvisionMode_t`; `IOT_ProvisionTypes.h` is now only a
    forwarding header);
  - `IOT_CoreEventHandler.h` — inbound events; an umbrella over `IOT_CoreEventTypes.h` and one
    `IOT_CoreEvent<Category>.h` per category (`IOT_EVENT_CATEGORY_LAST` names the last one);
  - `IOT_CoreReporter.h` — outbound calls; an umbrella over `IOT_CoreState.h`,
    `IOT_CoreSmart.h`, `IOT_CoreSensorLog.h` and `IOT_CoreIr.h`. Attribute history replies
    and pushes are in `IOT_CoreRecord.h`, which you include yourself.

  Existing includes keep working; prefer the specific header in new code. Service messages to
  and from your own backend are `IOT_CoreServiceMsg.h` (publish, per channel) and
  `IOT_CoreEventService.h` (the `IOT_EVENT_SERVICE` event).
- **[core/Attribute_Reference.md](core/Attribute_Reference.md)** — attribute IDs, sizes,
  defaults and enums. **Definitive.** The `Size` column is load-bearing: the attribute id
  carries no length, sizes count 16-bit fields (`2 * N`, so one field is 2 bytes), and
  multi-byte values are big-endian.
- **[core/Custom_Attributes.md](core/Custom_Attributes.md)** — declaring and using attribute
  ids of your own, for product features the reference list does not cover: the partner id
  range, the three places a size has to be right, and the registration step that makes the
  app show it. Working code: **[../examples/custom-attrs/](../examples/custom-attrs/)**.

## "How do I bring this up on my hardware?"

- **[platform/1. Platform_Implementation_Guide.md](platform/1.%20Platform_Implementation_Guide.md)**
  — every platform interface you must implement, with the ESP32 implementation as the worked
  reference. Ends in a checklist.
- **[platform/2. BLE_Implementation_Guide.md](platform/2.%20BLE_Implementation_Guide.md)** —
  the BLE driver vtable and registration.
- **[platform/3. sdkconfig_Reference.md](platform/3.%20sdkconfig_Reference.md)** — the ESP-IDF
  settings that are *load-bearing*: the value to use and what breaks if you change it. **Read
  this first if a device disconnects under load, or an OTA dies partway through.** It opens
  with a symptom → setting table.

## "How should I structure my own SDK?"

Design guides. They describe what **you** implement on path B — the pieces `iotcore` leaves to
your SDK. On path A `sdkcontract/` does most of it; 0 and 5 still apply, and the others explain
what the contract is doing for you.

- **[sdk/0_SDK_Architecture.md](sdk/0_SDK_Architecture.md)** — layers and ownership. Read first.
- **[sdk/1_Device_Control.md](sdk/1_Device_Control.md)** — element state and control flow.
- **[sdk/2_Sub_Device_Hub.md](sdk/2_Sub_Device_Hub.md)** — hub role and sub-devices.
- **[sdk/3_Smart_Automation.md](sdk/3_Smart_Automation.md)** — triggers, schedules, cond-groups.
- **[sdk/4_DAO_Guide.md](sdk/4_DAO_Guide.md)** — what to persist, and the storage interface.
- **[sdk/5_App_API_Design.md](sdk/5_App_API_Design.md)** — the surface you expose to app writers.
- **[sdk/6_Record_History.md](sdk/6_Record_History.md)** — path B: the lifecycle of a Record you
  write yourself, and the fixed limits core enforces.
- **[sdk/7_Record_Contract.md](sdk/7_Record_Contract.md)** — the Record rules, both paths, and
  the core Record API (history replies, the GET_STATE checkpoint, pushing closed blocks).
- **[sdk/8_Smart_Contract.md](sdk/8_Smart_Contract.md)** — the Smart rules, both paths.

> The path-B guides name DAOs such as `IOT_DaoSmartTriggerSet()` that you will **not** find in
> this package. That is deliberate: they describe the shape of stores you write yourself. On
> path A, the stores come from `sdkcontract/` and you only read them (the Device and IR store
> headers in `sdkcontract/include/dao/`). The copy-and-adapt Device and Group DAOs that used to
> ship in `reference/dao/` are retired.

## "What changed?"

- **[../CHANGELOG.md](../CHANGELOG.md)** — newest first. Read the top entry before upgrading;
  breaking changes and required `sdkconfig` edits are called out there. **Upgrading across
  several versions? Read every entry in between** — each may carry its own required action.
- **[CHANGELOG-archive.md](CHANGELOG-archive.md)** — releases from 12_23_2025 to 05_12_2026,
  which predate tagged releases. Frozen; nothing new is added.

## "Something is broken"

- **[../SUPPORT.md](../SUPPORT.md)** — what to send us so the answer comes back in one round
  trip instead of five.

# Documentation index

Start with the question you have, not the folder name.

## "How do I call the core API?"

- **[core/API_Document.md](core/API_Document.md)** — the full public API: `IOT_Core.h`,
  `IOT_CoreEventHandler.h`, `IOT_CoreReporter.h`. Every function you link against, what each
  event carries, and when to call what. This is the reference you will keep open.
- **[core/Attribute_Reference.md](core/Attribute_Reference.md)** — attribute IDs, sizes,
  defaults and enums. **Definitive.** The `Size` column is load-bearing: the attribute id
  carries no length, and multi-byte values are big-endian.

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

Design guides. They describe what **you** implement — the pieces that are deliberately not
shipped, because they are your product's behaviour, not the protocol's.

- **[sdk/0_SDK_Architecture.md](sdk/0_SDK_Architecture.md)** — layers and ownership. Read first.
- **[sdk/1_Device_Control.md](sdk/1_Device_Control.md)** — element state and control flow.
- **[sdk/2_Sub_Device_Hub.md](sdk/2_Sub_Device_Hub.md)** — hub role and sub-devices.
- **[sdk/3_Smart_Automation.md](sdk/3_Smart_Automation.md)** — triggers, schedules, cond-groups.
- **[sdk/4_DAO_Guide.md](sdk/4_DAO_Guide.md)** — what to persist, and the storage interface.
- **[sdk/5_App_API_Design.md](sdk/5_App_API_Design.md)** — the surface you expose to app writers.

> These name DAOs such as `IOT_DaoSmartTriggerSet()` that you will **not** find in this
> package. That is deliberate: `reference/dao/` ships only Device and Group as worked examples
> to copy and adapt. The rest are yours to write, and the guides describe their shape.

## "What changed?"

- **[../CHANGELOG.md](../CHANGELOG.md)** — newest first. Read the top entry before upgrading;
  breaking changes and required `sdkconfig` edits are called out there.

## "Something is broken"

- **[../SUPPORT.md](../SUPPORT.md)** — what to send us so the answer comes back in one round
  trip instead of five.

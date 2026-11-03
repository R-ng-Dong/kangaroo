# iotcore public headers

Everything in this directory is the public API of `iotcore`: these headers, and nothing else,
are what an SDK written against `iotcore` may include. Core's own internal headers are not
part of the library's surface and are not distributed.

A header's name says nothing about which way it faces, so here is the map. It lists every
header in this directory; anything not in it is not part of the API.

| Role | Header | What it holds |
|---|---|---|
| Engine lifecycle | `IOT_Core.h` | init/deinit, provisioning, WiFi and cloud session, boot counter, config dump, work queue |
| Engine lifecycle | `IOT_ProvisionTypes.h` | **deprecated** forwarding header; `IOT_ProvisionMode_t` is in `IOT_Core.h` |
| Inbound — core calls the SDK | `IOT_CoreEventHandler.h` | umbrella: category enum, `IOT_CoreEvent_t`, callback registration; includes the nine below |
| Inbound | `IOT_CoreEventTypes.h` | `IOT_SENDER_*`, group and sentinel constants, `IOT_RequestContext_t`, domain types |
| Inbound | `IOT_CoreEventState.h`, `IOT_CoreEventDevice.h`, `IOT_CoreEventMesh.h`, `IOT_CoreEventSetting.h`, `IOT_CoreEventSmart.h`, `IOT_CoreEventOs.h`, `IOT_CoreEventIr.h`, `IOT_CoreEventService.h` | one event category each; `IOT_CoreEventService.h` delivers messages from your own backend |
| Outbound — the SDK calls core | `IOT_CoreReporter.h` | umbrella: declares nothing, includes the four below |
| Outbound | `IOT_CoreState.h` | GET_STATE answers, state-change reports, device status, DEVICE command response |
| Outbound | `IOT_CoreSmart.h` | trigger reports, sync-on-off, condition groups, SMART command responses |
| Outbound | `IOT_CoreSensorLog.h` | sensor-log responses |
| Outbound | `IOT_CoreRecord.h` | record responses and pushes — V2, replacing `IOT_CoreSensorLog.h` |
| Shared | `IOT_CoreTimeRequest.h` | whether a GET_STATE asked for the time — one predicate, so core and the SDK cannot disagree |
| Outbound | `IOT_CoreIr.h` | IR sub-device data request |
| Outbound | `IOT_CoreOta.h` | the OTA surface (included by `IOT_Core.h`) |
| Outbound | `IOT_CoreServiceMsg.h` | partner business messages: publish an opaque, partner-defined payload on one of your service channels (receiving: `IOT_CoreEventService.h`) |
| Capability declaration | `IOT_CoreHubRole.h` | hub-role startup (Zigbee lifecycle still core-owned) |
| Capability declaration | `IOT_CoreHubRoleFlags.h` | which hub roles the SDK declares (included by `IOT_CoreHubRole.h`) |
| Diagnostics (dev only) | `IOT_CoreHeapProbe.h` | heap sampling around a scope; bodies compile out unless `CONFIG_IOT_HEAP_PROBE_ENABLED` |

Each header here compiles on its own: include just the one you need, in any order, with no
prerequisite header. Where one header needs another it includes it itself, as the
"included by" notes above record.

They are safe to include from C++. Every declaration sits inside an `extern "C"` block, so a
C++ `main` or a C++ SDK links against the library without wrapping these includes yourself.

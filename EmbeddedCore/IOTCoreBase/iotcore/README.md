# iotcore

The protocol engine every device runs. It talks to the cloud and the mobile app over BLE, the local
network and the cloud connection; runs provisioning, WiFi and the cloud session; turns every request
into an event for your SDK; and builds and sends every report and answer you hand it as data. It
ships as a prebuilt library (`lib/<target>/libiotcore.a`) with its public headers (`include/`).

**It is not** a device model: it does not know what an attribute means, keeps no device, group,
automation or history store, and never drives hardware. Those belong to `sdkcontract` (path A) or to
your own SDK (path B).

**It is for** SDK writers. On path A you start core through `sdkcontract` and meet only the events
it passes on; on path B you register a callback per event category, start core yourself and answer
every request ([concepts.md](docs/concepts.md#two-ways-to-build-an-sdk)).

## Quick start (path B)

```c
IOT_InitPlatform();                                    /* storage, network, BLE, timers */
for (int c = IOT_EVENT_STATE; c <= IOT_EVENT_CATEGORY_LAST; c++)
    IOT_DevCoreRegisterEventCb((IOT_CoreEventCategory_t) c, my_on_event, NULL);
IOT_CoreInit(MODEL_ID);                                /* DEV_EVENT_BOOTED arrives inside it */
```

Explained in [docs/getting-started.md](docs/getting-started.md). On path A start with
[sdkcontract's README](../sdkcontract/README.md) instead.

## Documentation

| Read | When you want to |
|---|---|
| [docs/getting-started.md](docs/getting-started.md) | Bring up a device on core directly, step by step |
| [docs/concepts.md](docs/concepts.md) | Know the layers, who owns what, which events come on which task, how a request is answered once |
| [docs/roles.md](docs/roles.md) | Know which writer works on which layer, and where each reads |
| [docs/events.md](docs/events.md) | Look up an event: when, task, data, what you answer |
| [docs/reference.md](docs/reference.md) | Look up any public function, type or event, by header |
| [docs/how-to/handle-state.md](docs/how-to/handle-state.md) | Handle state commands and queries, report your hardware's changes |
| [docs/how-to/hub-and-sub-devices.md](docs/how-to/hub-and-sub-devices.md) | Run a hub: joins, removals, groups, attribute sets, hub roles |
| [docs/how-to/smart-engine.md](docs/how-to/smart-engine.md) | Store and run automations in your own SDK |
| [docs/how-to/device-status.md](docs/how-to/device-status.md) | Report the device status and answer a status request (both paths) |
| [docs/how-to/own-stores.md](docs/how-to/own-stores.md) | Keep data of your own: storage, cache, memory |
| [docs/how-to/attributes.md](docs/how-to/attributes.md) | Use the attribute table from an SDK of your own |
| [docs/how-to/app-facing-api.md](docs/how-to/app-facing-api.md) | Design the API your app writers use (both paths) |
| [docs/how-to/config-tool.md](docs/how-to/config-tool.md) | Give your users a tool that edits their `sdkconfig` |
| [docs/how-to/ble-advertising.md](docs/how-to/ble-advertising.md) | Add your own fields and name to core's BLE adverts |

The rules the cloud and the app rely on, for both paths, are the
[Record](../sdkcontract/docs/record.md) and [Smart](../sdkcontract/docs/smart.md) rulebooks, and the
attribute table is [sdkcontract's attribute reference](../sdkcontract/docs/attributes.md).

## Headers

Every header in `include/` is public and nothing else is; `include/README.md` maps them by role.
Each compiles on its own and is safe to include from C++.

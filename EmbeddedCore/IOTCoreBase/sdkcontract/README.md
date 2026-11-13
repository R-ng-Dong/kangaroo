# sdkcontract

The behaviour the cloud and the app rely on a device to have, as one component. It runs every
request that changes cloud-visible state (state commands, device joins and removals, groups, IR
keysets, automations, history), keeps the Device, Group, IR, Smart and Record stores, and sends
every state and history report. Your code drives the hardware and tells the contract when the
hardware changed by itself.

**It is not** a hardware driver, a board support package or an application framework: what your
product's outputs and inputs are, and how they are wired, stays yours. It is not required either —
you may write your own SDK on the public `iotcore` API instead ([concepts.md](docs/concepts.md#two-ways-to-build-an-sdk)).

**It is for** SDK writers building a product on `iotcore`. It builds on the public core API
(`iotcore`), the platform interfaces (`iotplatform`) and the common utilities (`iotcommon`), ships
as source, and is built as-is: do not edit it. Version `2.2.0` (`IOT_CONTRACT_VERSION_STRING`).

## Quick start

```c
IOT_InitPlatform();                                     /* storage, WiFi, BLE, cloud link */
my_hardware_init();                                     /* ready before the start: state is restored */
const IOT_ContractConfig_t cfg = {.modelId = MODEL_ID, .elementIds = ELEMS, .elementCount = 1};
const IOT_ContractCallbacks_t cb = {.apply = my_apply}; /* drive one element, return IOT_OK */
IOT_ContractStart(&cfg, &cb);                           /* then IOT_ContractReport() on a button */
```

Build: add this directory to `EXTRA_COMPONENT_DIRS`. The complete, buildable version is
[examples/minimal](examples/minimal/main/main.c), explained in [getting-started.md](docs/getting-started.md).

## Headers

| Header | What it gives you |
|---|---|
| `include/IOT_Contract.h` | `IOT_ContractStart()` with your `apply` and hooks, the reports, contract events, restore, reset and restart |
| `include/IOT_ContractAttrs.h` | The attribute table (`IOT_ATTR_*`, `IOT_DEVTYPE_*`, value constants) and `IOT_ContractRegisterAttrs()` for your own ids |
| `include/IOT_ContractRecord.h` | Which history is kept: Record rules, element overrides, a custom history store |
| `include/dao/IOT_DaoDeviceMgmt.h` | The Device store, read-only |
| `include/dao/IOT_DaoIRMgmt.h` | The IR store, read-only |

`internal_include/` is the component's own: unsupported, not on your include path.

## Documentation

| Read | When you want to |
|---|---|
| [docs/getting-started.md](docs/getting-started.md) | Build your first device, line by line |
| [docs/concepts.md](docs/concepts.md) | Know what the contract owns, which task runs what, how a request flows |
| [docs/how-to/apply.md](docs/how-to/apply.md) | Drive your hardware from commands |
| [docs/how-to/local-change.md](docs/how-to/local-change.md) | Report a button or sensor change |
| [docs/how-to/sub-devices.md](docs/how-to/sub-devices.md) · [hooks.md](docs/how-to/hooks.md) | Run a hub: sub-device reports, joins and removals |
| [docs/how-to/core-events.md](docs/how-to/core-events.md) | Answer reboot, update, identify, IR and service requests |
| [docs/how-to/device-status.md](docs/how-to/device-status.md) | See what the device status request is answered with, and change it |
| [docs/how-to/custom-attributes.md](docs/how-to/custom-attributes.md) · [docs/custom-attributes.md](docs/custom-attributes.md) | Add attributes of your own (the short version; the full guide, with the cloud half) |
| [docs/attributes.md](docs/attributes.md) | Look up a standard attribute: id, size, default, value encoding |
| [docs/how-to/restore-at-boot.md](docs/how-to/restore-at-boot.md) | Choose what the device does with its last state at boot |
| [docs/how-to/reset-and-restart.md](docs/how-to/reset-and-restart.md) | Factory reset, contract reset, your own restart, a failed start |
| [docs/how-to/status-led.md](docs/how-to/status-led.md) | Show setup, WiFi and cloud state on an LED |
| [docs/how-to/water-purifier.md](docs/how-to/water-purifier.md) | Report filters and water, take a filter replace |
| [docs/how-to/ir.md](docs/how-to/ir.md) · [read-state.md](docs/how-to/read-state.md) | Build an IR device; read the stores |
| [docs/how-to/migrate.md](docs/how-to/migrate.md) | Move an existing SDK onto the contract |
| [docs/reference.md](docs/reference.md) | Look up any function, type or event |
| [docs/record.md](docs/record.md) · [docs/smart.md](docs/smart.md) | The Record and Smart rulebooks (both SDK paths) |

## Examples

| Example | Shows |
|---|---|
| [examples/minimal](examples/minimal/main/main.c) | The smallest real device: one on/off element, `apply` on a GPIO, a button reported, factory reset |
| [examples/water-purifier](examples/water-purifier/main/main.c) | A simulated water purifier: filter usage, water and issues reported, a filter replace taken in `apply`, values sent on the first connect |

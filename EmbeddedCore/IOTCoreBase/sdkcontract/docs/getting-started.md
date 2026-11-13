# Getting started: your first device on sdkcontract

This tutorial builds the smallest real device: one on/off element (a relay or an LED), driven by
commands from the app and the cloud and toggled by a local button. You end with a device that can
be set up from the app, switched from it, reports button presses, keeps history and runs
automations — in about 200 lines, all of them in [examples/minimal](../examples/minimal/main/main.c).

You need ESP-IDF set up for your chip, the SDK (this component sits in it as `sdkcontract/`), and
the model id the cloud registered for your product.

## 1. Build the example as it is

```bash
cd sdkcontract/examples/minimal
idf.py set-target esp32s3        # or esp32, esp32c3, esp32c6
idf.py build
```

The project finds the SDK three directories up, so it builds in place. To build a copy elsewhere,
set `ROGO_CORE_SDK_PATH` to the SDK root. What the build wiring does:

- [`CMakeLists.txt`](../examples/minimal/CMakeLists.txt) adds `sdkcontract/` to
  `EXTRA_COMPONENT_DIRS`: the contract is compiled from source, as shipped.
- [`components/iotcore`](../examples/minimal/components/iotcore/CMakeLists.txt) links core's
  prebuilt library for the target and exposes its public headers.
- [`components/iotplatform`](../examples/minimal/components/iotplatform/CMakeLists.txt) compiles the
  platform interface and its ESP32 implementation (storage, WiFi, BLE, MQTT). Keep its `-u` link
  flags: without them BLE setup silently does nothing.
- [`components/iotcommon`](../examples/minimal/components/iotcommon/CMakeLists.txt) compiles the
  common utilities.
- [`main/Kconfig.projbuild`](../examples/minimal/main/Kconfig.projbuild) declares the relay and
  button pins (`idf.py menuconfig` → *Minimal contract device*) and the platform options.
- `sdkconfig.defaults*` and `partitions.csv` are a working starting configuration. Keep
  `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`: a smaller value breaks firmware updates.

Now open [`main/main.c`](../examples/minimal/main/main.c) and follow it top to bottom.

## 2. Say what the product is

```c
#define APP_MODEL_ID "REPLACE_MODEL_ID"

#define APP_ELEM_RELAY 1
static const uint16_t APP_ELEMENTS[] = {APP_ELEM_RELAY};
```

Replace `APP_MODEL_ID` with your 16-character model id. It is text, not hex: never parse it.

`APP_ELEMENTS` lists **your own** element numbers, in the order of the elements in the model the
cloud has for your product. The cloud assigns its own element ids when the device is set up; the
contract translates between the two by position, so every callback and call in this file uses your
numbers. One element here; a 3-gang switch would declare three.

## 3. Get the hardware ready

`relay_init()` configures the relay output and the button input, and creates one lock. The lock
matters: `apply` runs on the task that delivers core events, and the button runs on its own task,
so both can drive the relay at once ([concepts.md](concepts.md#threading)).

`app_main` calls `relay_init()` **before** `IOT_ContractStart()`. Once core has started, the
contract replays the device's last stored state through `apply` — possibly before
`IOT_ContractStart()` even returns — so the hardware must be ready by then
([how-to/restore-at-boot.md](how-to/restore-at-boot.md)).

## 4. Write `apply`

```c
static iot_err_t app_apply(const IOT_ContractApply_t *req)
{
    if (req->elemId != APP_ELEM_RELAY || req->attrId != IOT_ATTR_ONOFF)
        return IOT_ERR_NOT_FOUND;
    uint16_t onOff = IOT_OFF;
    if (IOT_ContractApplyField(req, 0, &onOff) != IOT_OK)
        return IOT_ERR_INVALID_ARG;
    relay_set(onOff != IOT_OFF);
    return IOT_OK;
}
```

`apply` is the one place a command reaches your hardware: from the app, the cloud, a voice
assistant, an automation, a schedule, and the boot restore (`req->source` says which). The contract
calls it once per element, and after it returns does everything else — stores the new state,
records it, sends one report to the cloud and the app, and runs the automations it triggers. So
`apply` only drives the hardware and returns:

- `IOT_OK` — the hardware took the value;
- `IOT_ERR_NOT_FOUND` — not something this product drives (still stored, not an error);
- anything else — not applied, nothing stored for it.

`IOT_ContractApplyField()` reads the value as 16-bit fields, so you never decode bytes. Keep
`apply` short: the next request waits for it. More in [how-to/apply.md](how-to/apply.md).

## 5. Report what the hardware did by itself

The button task polls the button. A short press flips the relay and tells the contract:

```c
bool on = relay_toggle();
iot_err_t err = IOT_ContractReport(APP_ELEM_RELAY, IOT_ATTR_ONOFF, on ? IOT_ON : IOT_OFF);
```

`IOT_ContractReport()` takes your element number and the value, and runs the same steps as for a
command: store, record, report, then automations with the sender "device". It blocks briefly
(storage, the network hand-off), so call it from a task — never from an interrupt or a timer
callback. Before the device is set up there is no one to tell: the contract holds the latest value
and sends it with the first full report. More in [how-to/local-change.md](how-to/local-change.md).

A press held for `CONFIG_APP_FACTORY_RESET_MS` calls `IOT_CoreFactoryReset()`: core wipes its
configuration, the contract wipes its stores, and the device restarts
([how-to/reset-and-restart.md](how-to/reset-and-restart.md)).

## 6. Configure, then start once

```c
const IOT_ContractRecordRule_t onOffHistory = {.elemType = IOT_DEVTYPE_SWITCH, .attrId = IOT_ATTR_ONOFF};
(void) IOT_ContractRecordRegisterRule(&onOffHistory);
(void) IOT_ContractRemovalConfigure(true);
```

Optional set-up goes **before** the start. The Record rule keeps on/off history for every element
whose type is a switch ([record.md](record.md)); `IOT_ContractRemovalConfigure(true)` makes the
contract restart the device after it is removed from the home or factory reset, so the example
needs no `onDeviceRemoved` hook.

```c
const IOT_ContractConfig_t config = {
    .modelId = APP_MODEL_ID,
    .elementIds = APP_ELEMENTS,
    .elementCount = sizeof(APP_ELEMENTS) / sizeof(APP_ELEMENTS[0]),
};
const IOT_ContractCallbacks_t callbacks = { .apply = app_apply };
err = IOT_ContractStart(&config, &callbacks);
```

`IOT_ContractStart()` is the one call that brings the device up: it opens the stores, keeps a copy
of your element list, takes every core event and starts core. Both structs are copied, so they can
live on the stack. Call it once, from `app_main`, after `IOT_InitPlatform()`. Never start core or
register a core event callback yourself.

If it fails, the boot is over. The example discards a pending firmware update and restarts, because
a pending update makes every boot an update boot that would fail the same way, leaving a device that
can never be set up again:

```c
if (IOT_CoreOtaIsPending() && IOT_CoreOtaDiscardPending() == IOT_OK)
    IOT_ApplicationRestartChip();
```

Only then does `app_main` start the button task.

## 7. Flash and set it up

```bash
idf.py flash monitor
```

Set the device up from the app. Switch it from the app: the relay follows and the log shows
`relay ON`. Press the button: the app follows. Add an automation in the app that switches it: the
automation's action arrives at the same `apply`.

## What the example leaves out

The example passes no `onCoreEvent`, so the requests a device answers itself — a reboot, a firmware
update, identify — are dropped, and the asker is told they are not supported (the device status
request is the exception: the contract answers it). A product answers them: see [how-to/core-events.md](how-to/core-events.md). Next steps:

- [how-to/status-led.md](how-to/status-led.md) — show setup, WiFi and cloud state on an LED;
- [how-to/custom-attributes.md](how-to/custom-attributes.md) — attributes of your own;
- [how-to/sub-devices.md](how-to/sub-devices.md) and [how-to/hooks.md](how-to/hooks.md) — a hub;
- [concepts.md](concepts.md) — what the contract owns, and which task runs what.

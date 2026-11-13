# Put your own fields in the BLE advert

**Both paths.** Core advertises over BLE so the app can find the device, both while it waits to be set up
and when a provisioned device accepts new WiFi settings. You can add fields of your own to those adverts:
a different local name, a service-data field (`0x16`) with a battery level, a 32- or 128-bit service UUID list
(`0x04`–`0x07`).

Everything is in `IOT_CoreBleAdv.h`. You register one callback; core calls it each time an advert starts.

## What core keeps

Core always sends its own Flags, its service UUID and its manufacturer data, and by default its name in the
scan response. These are what the app uses to recognise and set up the device, so you cannot replace
them. Fields you add go after core's.

## How much room you have

A legacy advert is 31 bytes per payload, and core's fields come first. The callback is told what is left
(`advFree`, `rspFree`); read it rather than assuming.

| Payload | Free bytes |
|---|---|
| Advertising packet, set-up advert, hex model ID | 10 |
| Advertising packet, set-up advert, non-hex model ID | 18 |
| Advertising packet, change-WiFi advert | 5 |
| Scan response, with core's name | 13 |
| Scan response, after `IOT_CoreBleAdvOmitName()` or with your own name | 31 minus your name |

A field costs its data plus 2 bytes (length and type). With a non-hex model ID (a product ID) the name is
the only place core advertises it, so the name cannot be dropped or replaced.

Dropping or replacing core's name is the way to win scan-response room, but weigh it first: some apps
identify the product by core's default name. If you replace or omit it, those apps no longer list the
device. Keep core's name if your users set up with such an app.

## Example

Register before core starts, that is before `IOT_CoreInit()`, so the very first advert carries your
fields.

```c
#include "IOT_CoreBleAdv.h"

static volatile uint8_t s_batteryPercent = 100;

static void onBleAdv(const IOT_CoreBleAdvContext_t *ctx, IOT_CoreBleAdvFields_t *fields, void *arg)
{
    (void)arg;

    // Name first: it changes how much room is left in the scan response. A complete name (0x09)
    // replaces core's. With a non-hex model ID this is refused and core's name stays.
    // Some apps identify the product by core's default name and stop listing the device
    // once it is replaced: skip this if your users set up with such an app.
    static const uint8_t name[] = {'M', 'y', 'L', 'a', 'm', 'p'};
    if (IOT_CoreBleAdvAddField(fields, IOT_CORE_BLE_SCAN_RSP, 0x09, name, sizeof(name)) != IOT_OK)
    {
        // Refused: core's name stays on air, so the device is still found by its usual name.
    }

    // Battery level as service data (0x16): 16-bit UUID 0x180F, little endian, then the value.
    if (ctx->purpose == IOT_CORE_BLE_ADV_PROVISION && ctx->advFree >= 5)
    {
        uint8_t batt[3] = {0x0F, 0x18, s_batteryPercent};
        if (IOT_CoreBleAdvAddField(fields, IOT_CORE_BLE_ADV_PACKET, 0x16, batt, sizeof(batt)) != IOT_OK)
        {
            // Refused (it did not fit): the advert goes out without it.
        }
    }
}

void setupBleAdv(void)
{
    IOT_CoreBleAdvSetCustomizer(onBleAdv, NULL);   // before IOT_CoreInit()
    // ... then IOT_CoreInit() ...
}

void onBatteryChanged(uint8_t percent)
{
    s_batteryPercent = percent;
    IOT_CoreBleAdvRefresh();   // returns at once; runs on core's task
}
```

`IOT_CoreBleAdvRefresh()` is safe to call from any task. It re-runs your callback and puts the new
bytes on air; when the device is not advertising it succeeds and does nothing, because the next advert
start runs the callback anyway.

Calling `IOT_CoreBleAdvRefresh()` from inside the callback is refused with `IOT_ERR_INVALID_STATE`: every
advert start it caused would queue another refresh. If restarting the change-WiFi advert fails during a
refresh, it stays down until core's change-WiFi recovery starts it again.

Register the customizer once, before `IOT_CoreInit()`, and keep it for the device's life. A later
`IOT_CoreBleAdvSetCustomizer()` may race an advert start on another task, so for at most one advert the
callback and its `arg` can be mismatched. To change what goes on air, change your own state (as
`s_batteryPercent` above) and call `IOT_CoreBleAdvRefresh()`.

The callback runs on the task that starts the advert: the task that called `IOT_CoreInit()`, core's
task, the BLE stack's own task after a phone disconnects, or a task of yours that calls
`IOT_CoreStartProvision()` or `IOT_CoreStartChangeWifiBle()`. Keep it short, never block, use little
stack, and read shared values safely (a single `volatile` byte, as the example does). Call nothing of
core's except `IOT_CoreBleAdvAddField()` and `IOT_CoreBleAdvOmitName()`. Field bytes are copied, so your
buffers need not outlive the call.

## What is refused

A refused field writes nothing and fields you already added are kept.

| Call | Error | Why |
|---|---|---|
| Any, with a NULL pointer, zero length or unknown payload | `IOT_ERR_INVALID_ARG` | Bad arguments |
| Any, outside the callback | `IOT_ERR_INVALID_STATE` | Only the customizer may build fields |
| Flags (`0x01`) | `IOT_ERR_NOT_ALLOWED` | Core owns it |
| Any 16-bit service UUID list (`0x02`, `0x03`), your own UUIDs included | `IOT_ERR_NOT_ALLOWED` | Core owns them |
| Manufacturer data (`0xFF`), whatever the company ID, in either packet | `IOT_ERR_NOT_ALLOWED` | Core's own; a second one breaks on Apple devices ([below](#iphones-and-macs)) |
| A name when the model ID is non-hex; `IOT_CoreBleAdvOmitName()` in the same case | `IOT_ERR_NOT_ALLOWED` | The name carries the model ID |
| A second name in one advert | `IOT_ERR_ALREADY_EXISTS` | One name per advert |
| A second field of a type you already added, in either packet | `IOT_ERR_ALREADY_EXISTS` | A phone keeps one value per type |
| Service data (`0x16`, `0x20`, `0x21`) repeating a UUID you already used | `IOT_ERR_ALREADY_EXISTS` | Different UUIDs are fine: service data is kept per UUID |
| `IOT_CoreBleAdvOmitName()` after you set your own name | `IOT_ERR_INVALID_STATE` | Nothing of core's to drop |
| A field that does not fit | `IOT_ERR_INVALID_SIZE` | Refused whole |
| `IOT_CoreBleAdvRefresh()` from inside the callback | `IOT_ERR_INVALID_STATE` | It would refresh forever |

## Keep the names in step

The advert's local name and the GATT device name (`IOT_BleSetDeviceName()`) should be the same string.
Phones cache the two separately, and a mismatch makes the device appear under two names.

## iPhones and Macs

Apple's Bluetooth API reports one manufacturer-data value per device, and core's advert already carries
one. A second `0xFF` field broke it on a Mac: in the advert it hid core's data, or the whole advert when it
used Apple's company ID; in the scan response its bytes were appended to core's. That is why manufacturer
data is refused. Put your data in service data (`0x16`) under your own UUID instead.

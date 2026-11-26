# Put your own name, fields and vendor data in the BLE advert

**Both paths.** Core advertises over BLE so the app can find a device that is waiting to be set up (the
*setup advert*). You can add your own data to it: up to 10 bytes of vendor data that your own scanner can
read, a different name, a service-data field (`0x16`) with a battery level, a 32- or 128-bit service UUID
list (`0x04`–`0x07`).

Everything is in `IOT_CoreBleAdv.h`: a handful of setters. Call them once at boot, before or after
`IOT_CoreInit()`, from any task. Core puts what you set into every setup advert it starts.

```c
#include "IOT_CoreBleAdv.h"

static const uint8_t vendor[4] = {0x01, 0x02, 0x03, 0x04};

void app_main(void)
{
    IOT_CoreBleAdvSetVendorData(vendor, sizeof(vendor)); /* up to 10 bytes */
    IOT_CoreBleAdvSetName("MYLAMP-A1", 11);             /* optional: your own name */
    /* ... then start core as usual */
}
```

## What core keeps

Core always sends its own Flags, its service UUID and its manufacturer data, and by default its name in the
scan response. The app uses these to recognise and set up the device, so you cannot replace them.

The setup advert is the only one that carries your settings. A device that is already set up advertises to
accept new WiFi settings with core's fields and core's name only.

Nothing is stored across a reboot: set your values at every boot.

## Where your vendor data goes

Your vendor bytes go inside core's manufacturer-data field, right after the model ID, so the device still
sends one manufacturer-data value (a phone keeps only one per device). The field reads:

| Bytes | Content |
|---|---|
| 2 | Company ID `0xFFFF` |
| 2 | `0x72 0x67` |
| 8 | The model ID |
| 1–10 | Your vendor data |

On Android, `ScanRecord.getManufacturerSpecificData(0xFFFF)` returns the data after the company ID, so your
bytes start at index 10. On iOS and macOS, `kCBAdvDataManufacturerData` includes the company ID, so they
start at index 12. The model ID must be hex for vendor data; with a non-hex model ID it is refused.

## How much room you have

A legacy advert is 31 bytes per payload, and core's fields come first. Every setter checks the room when you
call it: if the result would not fit, it returns `IOT_ERR_INVALID_SIZE` and nothing changes.

| Payload | Free bytes (hex model ID) |
|---|---|
| Advertising packet | 10, shared by vendor data and your packet fields |
| Scan response, with core's name | 13 |
| Scan response, with your own name | 29 minus your name |
| Scan response, after `IOT_CoreBleAdvOmitName()` | 31 |

A field costs its data plus 2 bytes (length and type). So 10 bytes of vendor data fill the advertising
packet, and any further field of yours goes in the scan response.

## Fields

```c
/* A battery level as service data in the scan response: UUID 0x1234, then one byte. */
static const uint8_t battery[3] = {0x34, 0x12, 87};
IOT_CoreBleAdvAddField(IOT_CORE_BLE_SCAN_RSP, 0x16, battery, sizeof(battery));
```

- A second field of the same type replaces the first, in either payload. Service data replaces only the
  field with the same UUID, so several service-data fields with different UUIDs can coexist.
- `IOT_CoreBleAdvRemoveField(type, uuid, uuidLen)` removes one (pass the UUID for service data, NULL
  otherwise). `IOT_CoreBleAdvClear()` returns to core's own advert.
- Refused with `IOT_ERR_NOT_ALLOWED`: Flags (`0x01`), 16-bit UUID lists (`0x02`, `0x03`), a name (`0x08`,
  `0x09`: use `IOT_CoreBleAdvSetName()`), and manufacturer data (`0xFF`: use `IOT_CoreBleAdvSetVendorData()`).

## Changing a value while the device advertises

Call the setter again. If the setup advert is on air, core restarts it with the new bytes on its own task;
several calls in a row cause one restart. While a phone is connected nothing restarts, and the next advert
carries the new value.

## The name

Some apps identify the product by core's default name. If you replace it with `IOT_CoreBleAdvSetName()` or
drop it with `IOT_CoreBleAdvOmitName()`, those apps no longer list the device. Keep core's name if your users
set up with such an app. `IOT_CoreBleAdvSetName(NULL, 0)` puts core's name back.

With a non-hex model ID (a product ID), the name is the only place core advertises it, so it cannot be
replaced or dropped.

## Errors

| Return | When |
|---|---|
| `IOT_OK` | Stored |
| `IOT_ERR_INVALID_ARG` | NULL data with a length, length 0 for a field, a name over 29 bytes, service data shorter than its UUID |
| `IOT_ERR_INVALID_SIZE` | Vendor data over 10 bytes, or the result would not fit; the earlier value stays |
| `IOT_ERR_NOT_ALLOWED` | A type core owns; vendor data or a name change with a non-hex model ID |
| `IOT_ERR_NOT_FOUND` | Removing a field that is not set |

Each refusal also logs one warning.

# Attribute Reference

Attribute ids, device types and value encodings recognised by the cloud and the mobile app.

**These constants are not declared by `iotcore`** — attribute semantics belong to your SDK, not
to the protocol engine. Core moves attribute values through unchanged: it hands you the `attrId`
and the raw value bytes in an event, and sends back whatever bytes you report. Nothing in
`iotcore.a` includes an attribute header, and no core API takes these constants as a type.

They ship in `sdkcontract`, in `IOT_ContractAttrs.h`: every id, device type and value constant
below, with the sizes and defaults behind them. The table is not edited — an attribute of your
own is registered with `IOT_ContractRegisterAttrs()` (see
[custom-attributes.md](custom-attributes.md)). Read a value's size with `IOT_ContractAttrSize()`, and ask
whether an id is in the table at all with `IOT_ContractAttrIsKnown()` — a size cannot tell you,
because an unknown id is sized at 2 bytes. With the contract you rarely need either: it sizes,
seeds, stores and reports every value, and sizes history entries
(see [History entries](#history-entries-a-different-width)), for you.

This document remains the definitive list.

The **Size** column is the value length in bytes for that attribute. Get it wrong and the
value is misparsed — the id alone does not carry a length. Multi-byte values are
**big-endian**.

**Sizes count 16-bit fields, not bytes.** Every size is `2 * N`, so a single-field attribute
is **2 bytes, not 1**, and sizes are always even. Attributes not listed here are 2 bytes.

**Never report a zero size.** A zero-size attribute is accepted all the way down:
element state stores it as a placeholder, and core then has no bytes to put in a report, so
the device provisions and runs with that attribute simply absent — a blank control in the app.
Core logs a warning naming the attribute, EID and element it dropped; if you see it, the fix
is wherever that size came from. The contract's table never returns 0, and a registered
attribute has at least one field.

**A size change reaches a device only when it re-joins.** The width is written into the
device's stored element table when it is provisioned, so an OTA alone does not move an
already-provisioned device to a new size; it has to be re-provisioned.

> **Check any size you write yourself against this one.** A device that half-sizes a value
> round-trips correctly against itself and fails only against the app.

Only implement the attributes your device type actually exposes; the rest can be ignored.

**Adding your own:** see [custom-attributes.md](custom-attributes.md), which covers the
partner id range, registering your ids with `IOT_ContractRegisterAttrs()`, and the cloud-side step. In short: never
reuse a value listed here — including the deprecated ones, which stay reserved so old
automation data keeps parsing. Ids are one flat namespace and a duplicate fails *silently*:
the value is parsed as the other attribute, at that attribute's size. A custom id is only
fully useful once the cloud and app recognise it — send its definition to us to register.

---

## Device Types (`IOT_DEVTYPE_*`)

Device type constants used by the cloud provisioning system to classify elements. These are assigned during provisioning — you do not declare them in app code.

| Constant | Value | Description |
|---|---|---|
| `IOT_DEVTYPE_ALL` | 0 | Matches any device type (wildcard, used in group control) |
| `IOT_DEVTYPE_LIGHT` | 2 | Light / luminaire |
| `IOT_DEVTYPE_SWITCH` | 3 | Wall switch |
| `IOT_DEVTYPE_PLUG` | 4 | Smart plug / outlet |
| `IOT_DEVTYPE_CURTAINS` | 5 | Curtain / blind motor |
| `IOT_DEVTYPE_DOORLOCK` | 6 | Door lock |
| `IOT_DEVTYPE_DOORBELL` | 8 | Doorbell |
| `IOT_DEVTYPE_MEDIA_BOX` | 10 | Media / set-top box |
| `IOT_DEVTYPE_CAMERA` | 13 | IP camera |
| `IOT_DEVTYPE_AC` | 16 | Air conditioner |
| `IOT_DEVTYPE_TV` | 17 | Television |
| `IOT_DEVTYPE_FAN` | 18 | Fan |
| `IOT_DEVTYPE_MOTOR_CONTROLLER` | 19 | Motor controller |
| `IOT_DEVTYPE_BUTTON_DASH` | 20 | Button / dash device |
| `IOT_DEVTYPE_SWITCH_SCENE` | 21 | Scene switch |
| `IOT_DEVTYPE_TEMP_SENSOR` | 30 | Temperature sensor |
| `IOT_DEVTYPE_DOOR_SENSOR` | 31 | Door / contact sensor |
| `IOT_DEVTYPE_SMOKE_SENSOR` | 32 | Smoke detector |
| `IOT_DEVTYPE_MOTION_LUX_SENSOR` | 33 | Combined motion + lux sensor |
| `IOT_DEVTYPE_MOTION_SENSOR` | 34 | Motion (PIR) sensor |
| `IOT_DEVTYPE_LUX_SENSOR` | 35 | Light level (lux) sensor |
| `IOT_DEVTYPE_DUST_SENSOR` | 36 | Dust / air quality sensor |
| `IOT_DEVTYPE_PRESENCE_SENSOR` | 38 | Presence (mmWave) sensor |
| `IOT_DEVTYPE_AC_CONTROLLER` | 96 | AC controller (IR-based) |
| `IOT_DEVTYPE_IR_CONTROLLER` | 99 | Generic IR controller |
| `IOT_DEVTYPE_GATE` | 100 | Gate / barrier |
| `IOT_DEVTYPE_WATER_PURIFIER` | 112 | Water purifier |
| `IOT_DEVTYPE_WATER_FILTER_CARTRIDGE` | 113 | Water filter cartridge |
| `IOT_DEVTYPE_GATEWAY` | 192 | Mesh gateway |

---

## Attribute IDs (`IOT_ATTR_*`)

### Action / Control Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_NONE` | 0 | 2 | No attribute (placeholder) |
| `IOT_ATTR_ONOFF` | 1 | 2 | On/off state — see `iot_attr_onoff_t` |
| `IOT_ATTR_OPEN_CLOSE_CTL` | 2 | 4 | Open/close/stop command — see `iot_attr_openclose_action_t` |
| `IOT_ATTR_LOCK_UNLOCK` | 3 | 2 | Lock/unlock command — see `iot_attr_lock_t` |
| `IOT_ATTR_BATTERY` | 9 | 2 | Battery level (0–100) |
| `IOT_ATTR_POSITION` | 11 | 2 | Position (0–100 %) |
| `IOT_ATTR_MODE` | 17 | 2 | Operating mode (AC) — see `iot_attr_mode_t` |
| `IOT_ATTR_FAN_SWING` | 18 | 2 | Fan swing — see `iot_attr_fan_swing_t` |
| `IOT_ATTR_FAN_SPEED` | 19 | 2 | Fan speed — see `iot_attr_fan_speed_t`. One field, deliberately: a second (custom-speed) field is described for value 255, but no app sends it, and widening the size would fail silently on devices in the field |
| `IOT_ATTR_TEMP_SET` | 20 | 2 | Target temperature setpoint |
| `IOT_ATTR_CAMERA_STREAMING` | 22 | 2 | Camera streaming state |
| `IOT_ATTR_CALLING` | 23 | 2 | Calling / intercom state |
| `IOT_ATTR_ALARM` | 24 | 2 | Alarm state |

### Light Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_BRIGHTNESS` | 28 | 2 | Brightness level (0–1000) |
| `IOT_ATTR_KELVIN` | 29 | 2 | Color temperature in Kelvin |
| `IOT_ATTR_BRIGHTNESS_KELVIN` | 30 | 4 | Combined brightness + kelvin (2 bytes each) |
| `IOT_ATTR_COLOR_HSV` | 31 | 6 | HSV color (H, S, V — 2 bytes each) |
| `IOT_ATTR_COLOR_HSL` | 32 | 6 | HSL color (H, S, L — 2 bytes each) |
| `IOT_ATTR_COLOR_HSV_CHANNEL` | 33 | 2 | HSV channel selector |
| `IOT_ATTR_LEVEL` | 36 | 2 | Generic level |

### Sensor / Event Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_HUMID_EVT` | 48 | 2 | Humidity event reading |
| `IOT_ATTR_TEMP_EVT` | 49 | 2 | Temperature event reading |
| `IOT_ATTR_TEMP_HUMID_EVT` | 50 | 4 | Combined temp + humidity (2 bytes each) |
| `IOT_ATTR_OPEN_CLOSE_EVT` | 51 | 2 | Door/contact open-close event |
| `IOT_ATTR_MOTION_LUX_EVT` | 52 | 4 | Combined motion + lux event (2 bytes each) |
| `IOT_ATTR_MOTION_EVT` | 53 | 2 | Motion detected event |
| `IOT_ATTR_LUX_EVT` | 54 | 2 | Lux level event |
| `IOT_ATTR_SMOKE_EVT` | 55 | 2 | Smoke detected event |
| `IOT_ATTR_WALL_MOUNTED_EVT` | 56 | 2 | Wall-mounted button event |
| `IOT_ATTR_MOTION_SENSITIVE_LV` | 57 | 2 | Motion sensitivity level |
| `IOT_ATTR_BUTTON_PRESS_EVT` | 60 | 2 | Button press type — see `iot_attr_button_press_t` |
| `IOT_ATTR_KNOB_EVT` | 61 | 2 | Knob rotation event |
| `IOT_ATTR_LEVEL_EVT` | 62 | 2 | Level event |
| `IOT_ATTR_EVT_PRESENCE` | 70 | 2 | Presence detection event |
| `IOT_ATTR_PRESENCE_MULTI_ZONE_EVT` | 71 | 24 | Multi-zone presence event — 4 leading 16-bit fields + 4 zones × 2 16-bit fields (`4*2 + 2*4*2`) |
| `IOT_ATTR_EVT_PRESENCE_STATE` | 72 | 2 | Presence state |

### Power Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_POWER_VOLTAGE` | 75 | 2 | Voltage |
| `IOT_ATTR_POWER_CONSUMED` | 76 | 2 | Cumulative energy |
| `IOT_ATTR_POWER_CONSUMED_INSTANCE` | 77 | 2 | Instantaneous power |
| `IOT_ATTR_POWER_CURRENT_INSTANCE` | 78 | 2 | Electrical current |

All four are **one field**, as the shipped table sizes them; never size them wider in a value
you build yourself. A wider size also widens their history entries
(below), which the app keeps reading at the narrower width and renders as plausible but wrong
data rather than failing.

### Complex / IR Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_IR` | 81 | 2 | IR command (protocol-based) |
| `IOT_ATTR_IR_KEY` | 82 | 2 | IR key code |
| `IOT_ATTR_IR_RAW` | 83 | 2 | IR raw signal |
| `IOT_ATTR_AC` | 257 | 10 | AC full state, 5 × 16-bit fields: power + mode + temp + fan + swing (2 bytes each) |
| `IOT_ATTR_SMART_TEMPERATURE` | 1024 | 2 | Smart automation temperature trigger |

### Device Info Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_VENDOR` | 225 | 2 | Vendor identifier |
| `IOT_ATTR_MOTOR_CALIB_TIME_INFO` | 226 | 2 | Motor calibration time info |
| `IOT_ATTR_MOTOR_TYPE_INFO` | 227 | 2 | Motor type info |

### Sync Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_REVERSE_ONOFF` | 310 | 2 | Reverse on/off logic |
| `IOT_ATTR_SYNC_ONOFF_NORMAL` | 311 | 2 | Sync on/off — normal mode |
| `IOT_ATTR_SYNC_ONOFF_REVERSING` | 312 | 2 | Sync on/off — reversing mode |
| `IOT_ATTR_SYNC_ONOFF_STAIR_SWITCH` | 313 | 2 | Sync on/off — stair switch mode |
| `IOT_ATTR_SYNC_ONOFF_NO_TRIGGER` | 314 | 2 | Sync on/off — no trigger mode |
| `IOT_ATTR_SYNC_ONOFF_ELEMENTS` | 315 | 2 × N (variable) | Sync on/off element list — N big-endian `uint16` element ids. Variable length: take N from the attribute value length, do not assume a fixed size |

### Step Control Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_BRIGHTNESS_UP` | 320 | 2 | Step brightness up |
| `IOT_ATTR_BRIGHTNESS_DOWN` | 321 | 2 | Step brightness down |
| `IOT_ATTR_KELVIN_WARM_UP` | 322 | 2 | Step kelvin warmer |
| `IOT_ATTR_KELVIN_WARM_DOWN` | 323 | 2 | Step kelvin cooler |
| `IOT_ATTR_COLOR_HSV_CHANNEL_UP` | 324 | 2 | Step HSV channel up |
| `IOT_ATTR_COLOR_HSV_CHANNEL_DOWN` | 325 | 2 | Step HSV channel down |
| `IOT_ATTR_TEMP_UP` | 326 | 2 | Step temperature up |
| `IOT_ATTR_TEMP_DOWN` | 327 | 2 | Step temperature down |
| `IOT_ATTR_UP_OTHER_ATTR` | 328 | 2 | Step up generic attr (value: attr + step) |
| `IOT_ATTR_DOWN_OTHER_ATTR` | 329 | 2 | Step down generic attr (value: attr + step) |

### Notification & Trigger Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_PUSH_NOTIFICATION` | 330 | 2 | Push notification trigger |
| `IOT_ATTR_TRIGGER_ACTION_CFG` | 331 | 2 | Trigger action configuration |
| `IOT_ATTR_TRIGGER_ZONE_CFG` | 332 | 2 | Trigger zone configuration |

### Setting / Working-Params Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_SETTING_LOCK_BUTTON` | 61952 (0xF200) | 4 | Lock button setting (2 × 2 bytes) |
| `IOT_ATTR_STT_WORKING_PARAMS` | 61984 (0xF220) | 2 | Device working parameters |
| `IOT_ATTR_SETTING_PRESENCE_ZONE_SENSITIVE` | 61986 (0xF222) | 4 | Presence zone sensitivity setting |
| `IOT_ATTR_SETTING_PRODUCT_REPLACE` | 62624 (0xF4A0) | 22 | A part was replaced: new-model flag (2 B, `0001` = a different model), expire time (4 B), product id (16 B ASCII) |

### Device Information Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_INFO_PRODUCT_USAGE_TIME` | 45568 (0xB200) | 24 | Part lifetime: product id (16 B ASCII), expire time (4 B), used time (4 B, cumulative) |
| `IOT_ATTR_INFO_WATER_CONSUMED` | 45569 (0xB201) | 4 | Total water consumed, 32-bit |
| `IOT_ATTR_INFO_CURRENT_ISSUES_STATUS` | 46096 (0xB410) | 4 | Current issues, 32-bit mask |

### Deprecated Attributes

These are kept only for backward compatibility with old automation data. Do not use in new code.

| Constant | Value | Description |
|---|---|---|
| `IOT_ATTR_SINGLE_PRESS_EVT_DEPRECATED` | 90 | Replaced by `IOT_ATTR_BUTTON_PRESS_EVT` |
| `IOT_ATTR_LONG_PRESS_EVT_DEPRECATED` | 91 | Replaced by `IOT_ATTR_BUTTON_PRESS_EVT` |
| `IOT_ATTR_DOUBLE_PRESS_EVT_DEPRECATED` | 92 | Replaced by `IOT_ATTR_BUTTON_PRESS_EVT` |
| `IOT_ATTR_ONOFF_SWITCH_ROGO_V1_DEPRECATED` | 800 | Legacy V1 on/off switch attribute |
| `IOT_ATTR_ONOFF_SWITCH_ROGO_V2_DEPRECATED` | 801 | Legacy V2 on/off switch attribute |
| `IOT_ATTR_CMD_FORCE_RESET_MESH_DEVICE_DEPRECATED` | 462 | Legacy mesh device force-reset command |

---

## History entries: a different width

**An attribute is not the same width in every message.** A state report carries the full value
(the **Size** column above). A history (record) entry keeps **at most two** 16-bit fields plus
a timestamp, to save flash on the device — and nothing in either message says which width it
is using. Never derive one from the other:

| you are building or reading | width comes from |
|---|---|
| a state value | `IOT_ContractAttrSize(attrId)` |
| a history entry | the rule below — 1 or 2 fields |

```
fields N      = IOT_ContractAttrSize(attrId) / 2
history count = min(N, 2)                    -- exception: OPEN_CLOSE_CTL keeps 1
```

With sdkcontract, Record sizes every entry by this rule for you, registered attributes
included (they keep up to 2 fields). If you write your own Record, apply the rule exactly: the
obvious `IOT_ContractAttrSize() / 2` is wrong for `OPEN_CLOSE_CTL` and unbounded for wide
attributes.
When you reply to a history query with `IOT_CoreRequestRecordResponse()`, pass this as
`valueCount` and supply the fields in natural order (`value1`, `value2`) — core writes them in
the order the cloud expects.

Attributes whose history entry is **not** simply `state + 2` bytes:

| attribute | id | state | fields kept | history entry | why |
|---|---|---|---|---|---|
| `OPEN_CLOSE_CTL` | 2 | 4 B | 1 | **4 B** | the state is `[command][position %]`; history keeps only the command |
| `COLOR_HSV` | 31 | 6 B | 2 | **6 B** | capped at two fields |
| `COLOR_HSL` | 32 | 6 B | 2 | **6 B** | capped at two fields |
| `PRESENCE_MULTI_ZONE_EVT` | 71 | 24 B | 2 | **6 B** | capped at two fields |
| `INFO_PRODUCT_USAGE_TIME` | 0xB200 | 24 B | 2 | **6 B** | capped at two fields |
| `SETTING_PRODUCT_REPLACE` | 0xF4A0 | 22 B | 2 | **6 B** | capped at two fields |
| `AC` | 257 | 10 B | 2 | **6 B** | capped at two fields |

The other two-field attributes (`BRIGHTNESS_KELVIN` 30, `TEMP_HUMID_EVT` 50,
`MOTION_LUX_EVT` 52, `SETTING_LOCK_BUTTON` 0xF200, `SETTING_PRESENCE_ZONE_SENSITIVE` 0xF222,
`INFO_WATER_CONSUMED` 0xB201, `INFO_CURRENT_ISSUES_STATUS` 0xB410)
have a 4-byte state and a 6-byte history entry. Every other attribute has one field: a 2-byte
state and a 4-byte history entry.

`OPEN_CLOSE_CTL` is the trap: state and history are both 4 bytes, but not the same 4 bytes. A
reader that assumes a history entry carries the position percentage reads the timestamp as a
position.

`TEMP_HUMID_EVT` (50) is recorded as two histories, filed under `TEMP_EVT` (49) and
`HUMID_EVT` (48); nothing is recorded under 50 itself.

---

## Attribute Default Values

| Constant | Value | Used For |
|---|---|---|
| `IOT_ATTR_DEFAULT_TEMP` | 240 | Default temperature field of `IOT_ATTR_AC` only. `IOT_ATTR_TEMP_SET` defaults to 0 |
| `IOT_ATTR_DEFAULT_BRIGHTNESS` | 100 | Default brightness |
| `IOT_ATTR_DEFAULT_KELVIN` | 4000 | Default color temperature |

---

## Attribute Value Enums

### `iot_attr_onoff_t` — ONOFF (attr 1)

| Constant | Value | Meaning |
|---|---|---|
| `IOT_OFF` | 0 | Off |
| `IOT_ON` | 1 | On |

### `iot_attr_openclose_action_t` — OPEN_CLOSE_CTL (attr 2)

| Constant | Value | Meaning |
|---|---|---|
| `IOT_CLOSE` | 0 | Close |
| `IOT_OPEN` | 1 | Open |
| `IOT_STOP` | 2 | Stop movement |
| `IOT_MOVING` | 3 | Currently moving |

### `iot_attr_lock_t` — LOCK_UNLOCK (attr 3)

| Constant | Value | Meaning |
|---|---|---|
| `IOT_LOCK` | 0 | Locked |
| `IOT_UNLOCK` | 1 | Unlocked |

### `iot_attr_startstop_t` — START_STOP (attr 4)

| Constant | Value | Meaning |
|---|---|---|
| `IOT_STOPPED` | 0 | Stopped |
| `IOT_STARTED` | 1 | Started |
| `IOT_RUNNING` | 2 | Running |

### `iot_attr_mode_t` — MODE / AC mode (attr 17)

| Constant | Value | Meaning |
|---|---|---|
| `IOT_AC_AUTO` | 0 | Auto mode |
| `IOT_AC_COOLING` | 1 | Cooling |
| `IOT_AC_DRY` | 2 | Dry (dehumidify) |
| `IOT_AC_HEATING` | 3 | Heating |
| `IOT_AC_FAN` | 4 | Fan only |

### `iot_attr_fan_swing_t` — FAN_SWING (attr 18)

| Constant | Value | Meaning |
|---|---|---|
| `IOT_SWING_AUTO` | 0 | Auto swing |
| `IOT_SWING_OFF` | 255 | Swing off |

### `iot_attr_fan_speed_t` — FAN_SPEED (attr 19)

| Constant | Value | Meaning |
|---|---|---|
| `IOT_FAN_AUTO` | 0 | Auto speed |
| `IOT_FAN_LOW` | 1 | Low |
| `IOT_FAN_NORMAL` | 2 | Normal |
| `IOT_FAN_HIGH` | 3 | High |
| `IOT_FAN_MAX` | 4 | Maximum |
| `IOT_FAN_CUSTOM` | 255 | Custom (vendor-defined) |

### `iot_attr_button_press_t` — BUTTON_PRESS_EVT (attr 60)

| Constant | Value | Meaning |
|---|---|---|
| `IOT_PRESS_SINGLE` | 0 | Single press |
| `IOT_PRESS_LONG` | 1 | Long press |
| `IOT_PRESS_DOUBLE` | 2 | Double press |

# Attribute Reference

Attribute ids, device types and value encodings recognised by the cloud and the mobile app.

**These constants are not declared by `iotcore`** — attribute semantics belong to your SDK, not
to the protocol engine. Core moves attribute values through unchanged: it hands you the `attrId`
and the raw value bytes in an event, and sends back whatever bytes you report. Nothing in
`iotcore.a` includes an attribute header, and no core API takes these constants as a type.

`sdk/defines/IOT_AttrDefines.{h,c}` ships them as **reference material** so you do not have to
transcribe the ids by hand. Copy it into your own SDK and keep only what your devices expose —
it is a starting point, not an API you are bound to. The `.c` shows the three helpers every SDK
ends up needing (value size, default value, and whether a change is worth a push notification);
of those, only the sizes and defaults are protocol-relevant — `IOT_AttrIsNotifyWorthy()` is our
product's policy, included to show where yours goes.

This document remains the definitive list.

The **Size** column is the value length core expects on the wire for that attribute. Get it
wrong and the value is misparsed — the id alone does not carry a length. Multi-byte values are
**big-endian**. Attributes not listed here default to 1 byte.

Only implement the attributes your device type actually exposes; the rest can be ignored.

**Adding your own:** define it in your copy of the header, but never reuse a value listed
here — including the deprecated ones, which stay reserved so old automation data keeps
parsing. Ids are one flat namespace and a duplicate fails *silently*: the value is parsed as
the other attribute, at that attribute's size. Give any non-1-byte attribute a size entry
too. A custom id is only fully useful once the cloud and app recognise it — agree it with us
first.

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
| `IOT_DEVTYPE_GATEWAY` | 192 | Mesh gateway |

---

## Attribute IDs (`IOT_ATTR_*`)

### Action / Control Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_NONE` | 0 | 1 | No attribute (placeholder) |
| `IOT_ATTR_ONOFF` | 1 | 2 | On/off state — see `iot_attr_onoff_t` |
| `IOT_ATTR_OPEN_CLOSE_CTL` | 2 | 2 | Open/close/stop command — see `iot_attr_openclose_action_t` |
| `IOT_ATTR_LOCK_UNLOCK` | 3 | 1 | Lock/unlock command — see `iot_attr_lock_t` |
| `IOT_ATTR_BATTERY` | 9 | 1 | Battery level (0–100) |
| `IOT_ATTR_POSITION` | 11 | 1 | Position (0–100 %) |
| `IOT_ATTR_MODE` | 17 | 1 | Operating mode (AC) — see `iot_attr_mode_t` |
| `IOT_ATTR_FAN_SWING` | 18 | 1 | Fan swing — see `iot_attr_fan_swing_t` |
| `IOT_ATTR_FAN_SPEED` | 19 | 1 | Fan speed — see `iot_attr_fan_speed_t` |
| `IOT_ATTR_TEMP_SET` | 20 | 2 | Target temperature setpoint |
| `IOT_ATTR_CAMERA_STREAMING` | 22 | 1 | Camera streaming state |
| `IOT_ATTR_CALLING` | 23 | 1 | Calling / intercom state |
| `IOT_ATTR_ALARM` | 24 | 1 | Alarm state |

### Light Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_BRIGHTNESS` | 28 | 2 | Brightness level (0–1000) |
| `IOT_ATTR_KELVIN` | 29 | 2 | Color temperature in Kelvin |
| `IOT_ATTR_BRIGHTNESS_KELVIN` | 30 | 4 | Combined brightness + kelvin (2 bytes each) |
| `IOT_ATTR_COLOR_HSV` | 31 | 6 | HSV color (H, S, V — 2 bytes each) |
| `IOT_ATTR_COLOR_HSL` | 32 | 1 | HSL color |
| `IOT_ATTR_COLOR_HSV_CHANNEL` | 33 | 1 | HSV channel selector |
| `IOT_ATTR_LEVEL` | 36 | 1 | Generic level |

### Sensor / Event Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_HUMID_EVT` | 48 | 2 | Humidity event reading |
| `IOT_ATTR_TEMP_EVT` | 49 | 2 | Temperature event reading |
| `IOT_ATTR_TEMP_HUMID_EVT` | 50 | 4 | Combined temp + humidity (2 bytes each) |
| `IOT_ATTR_OPEN_CLOSE_EVT` | 51 | 1 | Door/contact open-close event |
| `IOT_ATTR_MOTION_LUX_EVT` | 52 | 1 | Combined motion + lux event |
| `IOT_ATTR_MOTION_EVT` | 53 | 1 | Motion detected event |
| `IOT_ATTR_LUX_EVT` | 54 | 2 | Lux level event |
| `IOT_ATTR_SMOKE_EVT` | 55 | 1 | Smoke detected event |
| `IOT_ATTR_WALL_MOUNTED_EVT` | 56 | 1 | Wall-mounted button event |
| `IOT_ATTR_MOTION_SENSITIVE_LV` | 57 | 1 | Motion sensitivity level |
| `IOT_ATTR_BUTTON_PRESS_EVT` | 60 | 1 | Button press type — see `iot_attr_button_press_t` |
| `IOT_ATTR_KNOB_EVT` | 61 | 1 | Knob rotation event |
| `IOT_ATTR_LEVEL_EVT` | 62 | 1 | Level event |
| `IOT_ATTR_EVT_PRESENCE` | 70 | 1 | Presence detection event |
| `IOT_ATTR_PRESENCE_MULTI_ZONE_EVT` | 71 | 5 | Multi-zone presence event (1 byte per zone, 5 zones) |

### Complex / IR Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_IR` | 81 | 1 | IR command (protocol-based) |
| `IOT_ATTR_IR_KEY` | 82 | 1 | IR key code |
| `IOT_ATTR_IR_RAW` | 83 | 1 | IR raw signal |
| `IOT_ATTR_AC` | 257 | 6 | AC full state: power(1) + mode(1) + temp(2) + fan(1) + swing(1) |
| `IOT_ATTR_SMART_TEMPERATURE` | 1024 | 1 | Smart automation temperature trigger |

### Device Info Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_VENDOR` | 225 | 1 | Vendor identifier |
| `IOT_ATTR_MOTOR_CALIB_TIME_INFO` | 226 | 1 | Motor calibration time info |
| `IOT_ATTR_MOTOR_TYPE_INFO` | 227 | 1 | Motor type info |

### Sync Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_REVERSE_ONOFF` | 310 | 1 | Reverse on/off logic |
| `IOT_ATTR_SYNC_ONOFF_NORMAL` | 311 | 1 | Sync on/off — normal mode |
| `IOT_ATTR_SYNC_ONOFF_REVERSING` | 312 | 1 | Sync on/off — reversing mode |
| `IOT_ATTR_SYNC_ONOFF_STAIR_SWITCH` | 313 | 1 | Sync on/off — stair switch mode |
| `IOT_ATTR_SYNC_ONOFF_NO_TRIGGER` | 314 | 1 | Sync on/off — no trigger mode |
| `IOT_ATTR_SYNC_ONOFF_ELEMENTS` | 315 | 2 × N (variable) | Sync on/off element list — N big-endian `uint16` element ids. Variable length: take N from the attribute value length, do not assume a fixed size |

### Step Control Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_BRIGHTNESS_UP` | 320 | 1 | Step brightness up |
| `IOT_ATTR_BRIGHTNESS_DOWN` | 321 | 1 | Step brightness down |
| `IOT_ATTR_KELVIN_WARM_UP` | 322 | 1 | Step kelvin warmer |
| `IOT_ATTR_KELVIN_WARM_DOWN` | 323 | 1 | Step kelvin cooler |
| `IOT_ATTR_COLOR_HSV_CHANNEL_UP` | 324 | 1 | Step HSV channel up |
| `IOT_ATTR_COLOR_HSV_CHANNEL_DOWN` | 325 | 1 | Step HSV channel down |
| `IOT_ATTR_TEMP_UP` | 326 | 1 | Step temperature up |
| `IOT_ATTR_TEMP_DOWN` | 327 | 1 | Step temperature down |
| `IOT_ATTR_UP_OTHER_ATTR` | 328 | 1 | Step up generic attr (value: attr + step) |
| `IOT_ATTR_DOWN_OTHER_ATTR` | 329 | 1 | Step down generic attr (value: attr + step) |

### Notification & Trigger Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_PUSH_NOTIFICATION` | 330 | 1 | Push notification trigger |
| `IOT_ATTR_TRIGGER_ACTION_CFG` | 331 | 1 | Trigger action configuration |
| `IOT_ATTR_TRIGGER_ZONE_CFG` | 332 | 1 | Trigger zone configuration |

### Setting / Working-Params Attributes

| Constant | Value | Size (bytes) | Description |
|---|---|---|---|
| `IOT_ATTR_SETTING_LOCK_BUTTON` | 61952 (0xF200) | 4 | Lock button setting (2 × 2 bytes) |
| `IOT_ATTR_STT_WORKING_PARAMS` | 61984 (0xF220) | 2 | Device working parameters |
| `IOT_ATTR_SETTING_PRESENCE_ZONE_SENSITIVE` | 61986 (0xF222) | 1 | Presence zone sensitivity setting |

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

## Attribute Default Values

| Constant | Value | Used For |
|---|---|---|
| `IOT_ATTR_DEFAULT_TEMP` | 240 | Default AC / temperature setpoint |
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

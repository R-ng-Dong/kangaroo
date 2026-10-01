#pragma once

/**
 * @file IOT_AttrDefines.h
 * @brief REFERENCE attribute IDs, device type constants, and attribute value enums.
 *
 * ---------------------------------------------------------------------------
 * THIS IS REFERENCE MATERIAL, NOT PART OF THE PROTOCOL ENGINE.
 *
 * Attribute semantics belong to YOUR SDK, not to iotcore. Core moves attribute
 * values through unchanged: it hands you an `attrId` and the raw value bytes in
 * an event, and sends back whatever bytes you report. Nothing in `iotcore.a`
 * includes this header, and no core API takes these constants as a type.
 *
 * Copy this file into your own SDK and keep only the attributes your devices
 * actually expose. It is provided so you do not have to transcribe the ids by
 * hand — the definitive list, with sizes and value encodings, is
 * `docs/core/Attribute_Reference.md`.
 *
 * The ids and value encodings here must match what the cloud and the mobile app
 * expect; the C names are ours and yours to rename freely.
 *
 * ADDING YOUR OWN ATTRIBUTES
 *
 * Define them here — but NEVER reuse a value that already appears below. Ids are
 * one flat namespace, so a duplicate fails silently rather than loudly: the value
 * is parsed as the OTHER attribute, at that attribute's size, corrupting the rest
 * of the message when the sizes differ. The deprecated ids at the end are still
 * reserved (old automation data parses through them), and the list is grouped by
 * purpose rather than sorted, so scanning one block does not prove a number is
 * free. If your attribute is not 1 byte, add a case to IOT_AttrGetSize() as well —
 * the default is 1 and nothing warns you.
 *
 * A custom id only becomes fully useful once the cloud and the app know it too;
 * agree it with us before relying on it end to end.
 * ---------------------------------------------------------------------------
 *
 * Contents:
 *   - IOT_ATTR_*    — attribute IDs
 *   - IOT_DEVTYPE_* — device type constants for an element's type field
 *   - iot_attr_*_t  — attribute value enums (IOT_ON/IOT_OFF, fan speeds, modes, etc.)
 *
 * Attribute sizes: call IOT_AttrGetSize(attrId). The size matters — an attribute
 * id does not carry a length, so a wrong size misparses the value. Multi-byte
 * values are big-endian.
 * Common sizes: ONOFF=2, BRIGHTNESS=2, KELVIN=2, FAN_SPEED=1, MODE=1,
 *               POSITION=1, COLOR_HSV=6, TEMP_HUMID_EVT=4.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include "IOT_ErrorManager.h"

// ============================================================================
// Attribute IDs
// ============================================================================

// Action attributes
#define IOT_ATTR_NONE                   0
#define IOT_ATTR_ONOFF                  1
#define IOT_ATTR_OPEN_CLOSE_CTL         2
#define IOT_ATTR_LOCK_UNLOCK            3

#define IOT_ATTR_BATTERY                9
#define IOT_ATTR_POSITION               11

#define IOT_ATTR_MODE                   17
#define IOT_ATTR_FAN_SWING              18
#define IOT_ATTR_FAN_SPEED              19
#define IOT_ATTR_TEMP_SET               20
#define IOT_ATTR_CAMERA_STREAMING       22
#define IOT_ATTR_CALLING                23
#define IOT_ATTR_ALARM                  24

// Light attributes
#define IOT_ATTR_BRIGHTNESS             28
#define IOT_ATTR_KELVIN                 29
#define IOT_ATTR_BRIGHTNESS_KELVIN      30
#define IOT_ATTR_COLOR_HSV              31
#define IOT_ATTR_COLOR_HSL              32
#define IOT_ATTR_COLOR_HSV_CHANNEL      33
#define IOT_ATTR_LEVEL                  36

// Event attributes
#define IOT_ATTR_HUMID_EVT              48
#define IOT_ATTR_TEMP_EVT               49
#define IOT_ATTR_TEMP_HUMID_EVT         50
#define IOT_ATTR_OPEN_CLOSE_EVT         51
#define IOT_ATTR_MOTION_LUX_EVT         52
#define IOT_ATTR_MOTION_EVT             53
#define IOT_ATTR_LUX_EVT                54
#define IOT_ATTR_SMOKE_EVT              55
#define IOT_ATTR_WALL_MOUNTED_EVT       56
#define IOT_ATTR_MOTION_SENSITIVE_LV    57
#define IOT_ATTR_BUTTON_PRESS_EVT       60
#define IOT_ATTR_KNOB_EVT               61
#define IOT_ATTR_LEVEL_EVT              62
#define IOT_ATTR_EVT_PRESENCE           70
#define IOT_ATTR_PRESENCE_MULTI_ZONE_EVT 71

// Device info attributes
#define IOT_ATTR_MOTOR_CALIB_TIME_INFO  226
#define IOT_ATTR_MOTOR_TYPE_INFO        227
#define IOT_ATTR_VENDOR                 225

// Sync attributes
#define IOT_ATTR_REVERSE_ONOFF              310
#define IOT_ATTR_SYNC_ONOFF_NORMAL          311
#define IOT_ATTR_SYNC_ONOFF_REVERSING       312
#define IOT_ATTR_SYNC_ONOFF_STAIR_SWITCH    313
#define IOT_ATTR_SYNC_ONOFF_NO_TRIGGER      314
// Sync-on-off element list: value = N x uint16 (big-endian) element ids. Carried
// in the primary attr-value of a subSmartType-71 bind (not a device attribute) —
// see "Sync On/Off" in docs/core/API_Document.md.
#define IOT_ATTR_SYNC_ONOFF_ELEMENTS        315

// Step control attributes
#define IOT_ATTR_BRIGHTNESS_UP          320
#define IOT_ATTR_BRIGHTNESS_DOWN        321
#define IOT_ATTR_KELVIN_WARM_UP         322
#define IOT_ATTR_KELVIN_WARM_DOWN       323
#define IOT_ATTR_COLOR_HSV_CHANNEL_UP   324
#define IOT_ATTR_COLOR_HSV_CHANNEL_DOWN 325
#define IOT_ATTR_TEMP_UP                326
#define IOT_ATTR_TEMP_DOWN              327
#define IOT_ATTR_UP_OTHER_ATTR          328 // value: attr + step
#define IOT_ATTR_DOWN_OTHER_ATTR        329 // value: attr + step

// Notification & trigger
#define IOT_ATTR_PUSH_NOTIFICATION      330
#define IOT_ATTR_TRIGGER_ACTION_CFG     331
#define IOT_ATTR_TRIGGER_ZONE_CFG       332

// Complex attributes
#define IOT_ATTR_IR                     81
#define IOT_ATTR_IR_KEY                 82
#define IOT_ATTR_IR_RAW                 83
#define IOT_ATTR_AC                     257

#define IOT_ATTR_SMART_TEMPERATURE      1024

// Setting / working-params attributes (0xF2xx range)
#define IOT_ATTR_SETTING_LOCK_BUTTON            61952  // 0xF200
#define IOT_ATTR_STT_WORKING_PARAMS             61984  // 0xF220
#define IOT_ATTR_SETTING_PRESENCE_ZONE_SENSITIVE 61986 // 0xF222

// Deprecated (keep for old automation compatibility)
#define IOT_ATTR_SINGLE_PRESS_EVT_DEPRECATED            90
#define IOT_ATTR_LONG_PRESS_EVT_DEPRECATED              91
#define IOT_ATTR_DOUBLE_PRESS_EVT_DEPRECATED            92
#define IOT_ATTR_ONOFF_SWITCH_ROGO_V1_DEPRECATED        800
#define IOT_ATTR_ONOFF_SWITCH_ROGO_V2_DEPRECATED        801
#define IOT_ATTR_CMD_FORCE_RESET_MESH_DEVICE_DEPRECATED 462

// ============================================================================
// Attribute Default Values
// ============================================================================

#define IOT_ATTR_DEFAULT_TEMP           240
#define IOT_ATTR_DEFAULT_BRIGHTNESS     100
#define IOT_ATTR_DEFAULT_KELVIN         4000

// ============================================================================
// Attribute Value Types & Constants
// ============================================================================

// 1 - ONOFF
typedef enum
{
    IOT_OFF = 0,
    IOT_ON = 1
} iot_attr_onoff_t;

// 2 - OPEN_CLOSE_CTL
typedef enum
{
    IOT_CLOSE = 0,
    IOT_OPEN,
    IOT_STOP,
    IOT_MOVING
} iot_attr_openclose_action_t;

// 3 - LOCK_UNLOCK
typedef enum
{
    IOT_LOCK = 0,
    IOT_UNLOCK
} iot_attr_lock_t;

// 4 - START_STOP
typedef enum
{
    IOT_STOPPED = 0,
    IOT_STARTED,
    IOT_RUNNING
} iot_attr_startstop_t;

// 17 - MODE (AC)
typedef enum
{
    IOT_AC_AUTO = 0,
    IOT_AC_COOLING,
    IOT_AC_DRY,
    IOT_AC_HEATING,
    IOT_AC_FAN
} iot_attr_mode_t;

// 18 - FAN_SWING
typedef enum
{
    IOT_SWING_AUTO = 0,
    IOT_SWING_OFF = 255
} iot_attr_fan_swing_t;

// 19 - FAN_SPEED
typedef enum
{
    IOT_FAN_AUTO = 0,
    IOT_FAN_LOW,
    IOT_FAN_NORMAL,
    IOT_FAN_HIGH,
    IOT_FAN_MAX,
    IOT_FAN_CUSTOM = 255
} iot_attr_fan_speed_t;

// 60 - BUTTON_PRESS_EVT
typedef enum
{
    IOT_PRESS_SINGLE = 0,
    IOT_PRESS_LONG,
    IOT_PRESS_DOUBLE
} iot_attr_button_press_t;

// ============================================================================
// Device Types
// ============================================================================

#define IOT_DEVTYPE_ALL                 0
#define IOT_DEVTYPE_LIGHT               2
#define IOT_DEVTYPE_SWITCH              3
#define IOT_DEVTYPE_PLUG                4
#define IOT_DEVTYPE_CURTAINS            5
#define IOT_DEVTYPE_DOORLOCK            6
#define IOT_DEVTYPE_DOORBELL            8
#define IOT_DEVTYPE_MEDIA_BOX           10
#define IOT_DEVTYPE_CAMERA              13
#define IOT_DEVTYPE_AC                  16
#define IOT_DEVTYPE_TV                  17
#define IOT_DEVTYPE_FAN                 18
#define IOT_DEVTYPE_MOTOR_CONTROLLER    19
#define IOT_DEVTYPE_BUTTON_DASH         20
#define IOT_DEVTYPE_SWITCH_SCENE        21
#define IOT_DEVTYPE_TEMP_SENSOR         30
#define IOT_DEVTYPE_DOOR_SENSOR         31
#define IOT_DEVTYPE_SMOKE_SENSOR        32
#define IOT_DEVTYPE_MOTION_LUX_SENSOR   33
#define IOT_DEVTYPE_MOTION_SENSOR       34
#define IOT_DEVTYPE_LUX_SENSOR          35
#define IOT_DEVTYPE_DUST_SENSOR         36
#define IOT_DEVTYPE_PRESENCE_SENSOR     38
#define IOT_DEVTYPE_AC_CONTROLLER       96
#define IOT_DEVTYPE_IR_CONTROLLER       99
#define IOT_DEVTYPE_GATE                100
#define IOT_DEVTYPE_GATEWAY             192

// ============================================================================
// Attribute Utilities
// ============================================================================

uint16_t IOT_AttrGetSize(uint16_t attrId);
iot_err_t IOT_AttrFillDefault(uint16_t attrId, uint8_t *buffer);

/* Example policy, NOT protocol: whether a value is worth a push notification.
 * The thresholds in the reference implementation are ours — replace them with
 * your own product's rules. */
bool IOT_AttrIsNotifyWorthy(uint16_t attrId, uint8_t *attrValue, uint16_t attrSize);

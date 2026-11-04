/**
 * @file IOT_AttrDefines.c
 * @brief REFERENCE implementation of the attribute helpers — copy and adapt.
 *
 * Nothing in `iotcore.a` calls these. They exist so you can see the shape of the
 * three things every SDK ends up needing: how long an attribute's value is, what
 * it should read when unset, and whether a change is worth telling the user about.
 *
 * The sizes and defaults below are protocol-relevant and must match
 * `docs/core/Attribute_Reference.md`. IOT_AttrIsNotifyWorthy() is NOT — it is our
 * product's policy, included only as an example of where yours goes.
 *
 * ---------------------------------------------------------------------------
 * SIZES COUNT 16-BIT FIELDS, NOT BYTES.
 *
 * Every size is `2 * N`, where N is the number of 16-bit fields in the value, and
 * the default is `2 * 1` — a single-field attribute is 2 bytes, never 1. Values
 * are big-endian, so a one-field value of 1 is {0x00, 0x01}.
 *
 * An earlier revision of this file had a default of 1 and three explicit entries
 * half-sized, which misstated roughly 37 attributes at once. It went unnoticed
 * because an encoder and decoder built from the SAME wrong table agree with each
 * other perfectly; only a peer built to the correct sizes can tell. The failure
 * that exposed it was the mobile app crashing on a gate device, reading a 4-byte
 * value out of a 2-byte block that happened to be last in the frame.
 *
 * If you copied the older revision, re-check your table against
 * `docs/core/Attribute_Reference.md`, and write any test from that document
 * rather than from your own table.
 * ---------------------------------------------------------------------------
 */

#include "defines/IOT_AttrDefines.h"

uint16_t IOT_AttrGetSize(uint16_t attrId)
{
    switch (attrId)
    {
    case IOT_ATTR_ONOFF:                return 2 * 1;
    case IOT_ATTR_AC:                   return 2 * 5; // power, mode, temp, fan, swing
    case IOT_ATTR_TEMP_SET:             return 2 * 1;
    case IOT_ATTR_BRIGHTNESS:           return 2 * 1;
    case IOT_ATTR_KELVIN:               return 2 * 1;
    case IOT_ATTR_BRIGHTNESS_KELVIN:    return 2 * 2;
    case IOT_ATTR_COLOR_HSV:            return 2 * 3;
    /* Sizes as published in docs/core/Attribute_Reference.md. */
    case IOT_ATTR_COLOR_HSL:            return 2 * 3; // hue + saturation + lightness
    case IOT_ATTR_MOTION_LUX_EVT:       return 2 * 2; // motion + lux
    /*
     * FAN_SPEED (19) deliberately has no case here, so it is one field. The attribute
     * documentation describes a second field (a custom speed, used when the first is
     * 255), but the app never sends 255, and a device in the field that reads a missing
     * second field sees 0 -- a valid speed (Auto) -- so widening it would fail silently.
     * The width is written into the device record at join, so a firmware update alone
     * would not change it on an existing device.
     */
    case IOT_ATTR_LUX_EVT:              return 2 * 1;
    case IOT_ATTR_TEMP_EVT:             return 2 * 1;
    case IOT_ATTR_HUMID_EVT:            return 2 * 1;
    case IOT_ATTR_TEMP_HUMID_EVT:       return 2 * 2;
    case IOT_ATTR_OPEN_CLOSE_CTL:       return 2 * 2;
    case IOT_ATTR_SETTING_LOCK_BUTTON:  return 2 * 2;
    case IOT_ATTR_PRESENCE_MULTI_ZONE_EVT:         return 4 * 2 + 2 * 4 * 2;
    case IOT_ATTR_EVT_PRESENCE_STATE:   return 2 * 1;
    /* The power attributes (75..78) deliberately have no case here: they are one field
     * each, matching docs/core/Attribute_Reference.md. */
    case IOT_ATTR_SETTING_PRESENCE_ZONE_SENSITIVE: return 2 + 2 * 1;
    case IOT_ATTR_STT_WORKING_PARAMS:              return 2 * 1;

    /* IOT_ATTR_BUTTON_PRESS_EVT and the other single-field attributes fall through
     * here deliberately, rather than being listed as 1. */
    default:                            return 2 * 1;
    }
}

bool IOT_AttrIsKnown(uint16_t attrId)
{
    switch (attrId)
    {
    /* Action attributes */
    case IOT_ATTR_ONOFF:
    case IOT_ATTR_OPEN_CLOSE_CTL:
    case IOT_ATTR_LOCK_UNLOCK:
    case IOT_ATTR_BATTERY:
    case IOT_ATTR_POSITION:
    case IOT_ATTR_MODE:
    case IOT_ATTR_FAN_SWING:
    case IOT_ATTR_FAN_SPEED:
    case IOT_ATTR_TEMP_SET:
    case IOT_ATTR_CAMERA_STREAMING:
    case IOT_ATTR_CALLING:
    case IOT_ATTR_ALARM:
    /* Light */
    case IOT_ATTR_BRIGHTNESS:
    case IOT_ATTR_KELVIN:
    case IOT_ATTR_BRIGHTNESS_KELVIN:
    case IOT_ATTR_COLOR_HSV:
    case IOT_ATTR_COLOR_HSL:
    case IOT_ATTR_COLOR_HSV_CHANNEL:
    case IOT_ATTR_LEVEL:
    /* Sensor events */
    case IOT_ATTR_HUMID_EVT:
    case IOT_ATTR_TEMP_EVT:
    case IOT_ATTR_TEMP_HUMID_EVT:
    case IOT_ATTR_OPEN_CLOSE_EVT:
    case IOT_ATTR_MOTION_LUX_EVT:
    case IOT_ATTR_MOTION_EVT:
    case IOT_ATTR_LUX_EVT:
    case IOT_ATTR_SMOKE_EVT:
    case IOT_ATTR_WALL_MOUNTED_EVT:
    case IOT_ATTR_MOTION_SENSITIVE_LV:
    case IOT_ATTR_BUTTON_PRESS_EVT:
    case IOT_ATTR_KNOB_EVT:
    case IOT_ATTR_LEVEL_EVT:
    case IOT_ATTR_EVT_PRESENCE:
    case IOT_ATTR_PRESENCE_MULTI_ZONE_EVT:
    case IOT_ATTR_EVT_PRESENCE_STATE:
    case IOT_ATTR_POWER_VOLTAGE:
    case IOT_ATTR_POWER_CONSUMED:
    case IOT_ATTR_POWER_CONSUMED_INSTANCE:
    case IOT_ATTR_POWER_CURRENT_INSTANCE:
    /* Info */
    case IOT_ATTR_VENDOR:
    case IOT_ATTR_MOTOR_CALIB_TIME_INFO:
    case IOT_ATTR_MOTOR_TYPE_INFO:
    /* Sync / bind carriers */
    case IOT_ATTR_REVERSE_ONOFF:
    case IOT_ATTR_SYNC_ONOFF_NORMAL:
    case IOT_ATTR_SYNC_ONOFF_REVERSING:
    case IOT_ATTR_SYNC_ONOFF_STAIR_SWITCH:
    case IOT_ATTR_SYNC_ONOFF_NO_TRIGGER:
    case IOT_ATTR_SYNC_ONOFF_ELEMENTS:
    /* Step controls */
    case IOT_ATTR_BRIGHTNESS_UP:
    case IOT_ATTR_BRIGHTNESS_DOWN:
    case IOT_ATTR_KELVIN_WARM_UP:
    case IOT_ATTR_KELVIN_WARM_DOWN:
    case IOT_ATTR_COLOR_HSV_CHANNEL_UP:
    case IOT_ATTR_COLOR_HSV_CHANNEL_DOWN:
    case IOT_ATTR_TEMP_UP:
    case IOT_ATTR_TEMP_DOWN:
    case IOT_ATTR_UP_OTHER_ATTR:
    case IOT_ATTR_DOWN_OTHER_ATTR:
    /* Trigger / notification */
    case IOT_ATTR_PUSH_NOTIFICATION:
    case IOT_ATTR_TRIGGER_ACTION_CFG:
    case IOT_ATTR_TRIGGER_ZONE_CFG:
    /* IR / AC */
    case IOT_ATTR_IR:
    case IOT_ATTR_IR_KEY:
    case IOT_ATTR_IR_RAW:
    case IOT_ATTR_AC:
    /* Smart */
    case IOT_ATTR_SMART_TEMPERATURE:
    /* Settings */
    case IOT_ATTR_SETTING_LOCK_BUTTON:
    case IOT_ATTR_STT_WORKING_PARAMS:
    case IOT_ATTR_SETTING_PRESENCE_ZONE_SENSITIVE:
    /* Deprecated, but defined — meeting one is not a discovery. */
    case IOT_ATTR_SINGLE_PRESS_EVT_DEPRECATED:
    case IOT_ATTR_LONG_PRESS_EVT_DEPRECATED:
    case IOT_ATTR_DOUBLE_PRESS_EVT_DEPRECATED:
    case IOT_ATTR_ONOFF_SWITCH_ROGO_V1_DEPRECATED:
    case IOT_ATTR_ONOFF_SWITCH_ROGO_V2_DEPRECATED:
    case IOT_ATTR_CMD_FORCE_RESET_MESH_DEVICE_DEPRECATED:
        return true;

    /* IOT_ATTR_NONE (0) falls through here deliberately: it is the absence of an
     * attribute, and a 0 in an element's attribute list is a malformed frame. */
    default:
        return false;
    }
}

iot_err_t IOT_AttrFillDefault(uint16_t attrId, uint8_t *buffer)
{
    switch (attrId)
    {
    case IOT_ATTR_AC:
        /* Five 16-BIT FIELDS, big-endian: power, mode, temp, fan, swing — 2*5 = 10
         * bytes, matching IOT_AttrGetSize(). This used to write five consecutive
         * BYTES, which was right while AC was hand-summed as 6; after the size was
         * corrected the default temperature landed in the MODE field's high byte, so
         * a freshly initialised AC element read mode = 0xF000 and temp = 0. */
        memset(buffer, 0, 2 * 5); /* power, mode, fan, swing all default to 0 */
        buffer[4] = (IOT_ATTR_DEFAULT_TEMP >> 8) & 0xFF;
        buffer[5] = IOT_ATTR_DEFAULT_TEMP & 0xFF;
        break;
    case IOT_ATTR_TEMP_SET:
        buffer[0] = IOT_ATTR_DEFAULT_TEMP >> 8;
        buffer[1] = IOT_ATTR_DEFAULT_TEMP & 0xFF;
        break;
    case IOT_ATTR_BRIGHTNESS:
        buffer[0] = IOT_ATTR_DEFAULT_BRIGHTNESS >> 8;
        buffer[1] = IOT_ATTR_DEFAULT_BRIGHTNESS & 0xFF;
        break;
    case IOT_ATTR_KELVIN:
        buffer[0] = IOT_ATTR_DEFAULT_KELVIN >> 8;
        buffer[1] = IOT_ATTR_DEFAULT_KELVIN & 0xFF;
        break;
    case IOT_ATTR_BRIGHTNESS_KELVIN:
        buffer[0] = IOT_ATTR_DEFAULT_BRIGHTNESS >> 8;
        buffer[1] = IOT_ATTR_DEFAULT_BRIGHTNESS & 0xFF;
        buffer[2] = IOT_ATTR_DEFAULT_KELVIN >> 8;
        buffer[3] = IOT_ATTR_DEFAULT_KELVIN & 0xFF;
        break;
    case IOT_ATTR_COLOR_HSV:
        buffer[0] = 0 >> 8;
        buffer[1] = 0 & 0xFF;
        buffer[2] = IOT_ATTR_DEFAULT_BRIGHTNESS >> 8;
        buffer[3] = IOT_ATTR_DEFAULT_BRIGHTNESS & 0xFF;
        buffer[4] = IOT_ATTR_DEFAULT_BRIGHTNESS >> 8;
        buffer[5] = IOT_ATTR_DEFAULT_BRIGHTNESS & 0xFF;
        break;
    default:
        memset(buffer, 0, IOT_AttrGetSize(attrId));
        break;
    }
    return IOT_OK;
}

/* Example policy only — replace the thresholds with your own product's rules. */
bool IOT_AttrIsNotifyWorthy(uint16_t attrId, uint8_t *attrValue, uint16_t attrSize)
{
    if (attrValue == NULL || attrSize == 0)
    {
        return false;
    }

    /* Values are big-endian, so the LAST byte carries the meaning: a 2-byte value
     * is {0x00, v}, and attrValue[0] is always 0x00.
     *
     * Read the significant byte here rather than requiring every caller to extract
     * it first. Testing attrValue[0] instead works only while these attributes are
     * 1 byte; at the correct size of 2 it reads every smoke alarm as clear and
     * every healthy battery as low. */
    const uint8_t significant = attrValue[attrSize - 1];

    switch (attrId)
    {
    case IOT_ATTR_SMOKE_EVT:
        return (significant == 1);
    case IOT_ATTR_WALL_MOUNTED_EVT:
        return (significant == 1);
    case IOT_ATTR_BATTERY:
        return (significant <= 20);
    default:
        return false;
    }
}

bool IOT_AttrResolveSyncOnOffCarrier(uint16_t *attrId)
{
    if (attrId == NULL)
    {
        return false;
    }

    if (*attrId != IOT_ATTR_SYNC_ONOFF_NORMAL && *attrId != IOT_ATTR_SYNC_ONOFF_STAIR_SWITCH)
    {
        return false;
    }

    *attrId = IOT_ATTR_ONOFF;
    return true;
}

uint8_t IOT_AttrRecordValueCount(uint16_t attrId)
{
    switch (attrId)
    {
    case IOT_ATTR_APP_TDS_STATUS:
        return 1;
    /* These product attributes carry 32-bit cumulative counters. The record format
     * stores at most two 16-bit fields, which preserves the leading 4 bytes. */
    case IOT_ATTR_APP_WATER_CONSUMPTION:
        return 2;
    case IOT_ATTR_APP_ENERGY:
        return 2;
    /* The one exception to the derivation. The wire value is command + position
     * percentage; a record keeps only the command. */
    case IOT_ATTR_OPEN_CLOSE_CTL:
        return 1;
    default:
        break;
    }

    const uint16_t size = IOT_AttrGetSize(attrId);
    const uint16_t count = (uint16_t) (size / 2);

    /* Never 0. entryCount divides by (valueCount + 1), so a 0 would make a reader loop on
     * a zero-width entry. An undeclared attribute is sized 2 * 1 anyway, so this only
     * fires if a declared size is ever odd or zero. */
    if (count == 0)
    {
        return 1;
    }

    /*
     * Never more than 2, and this bound is memory safety rather than taste.
     *
     * The record wire format has no entry wider than two values, so the append path holds
     * `uint16_t values[2]` and an instance holds `lastValue[2]`. The derivation alone
     * returns 3 for COLOR_HSV, 5 for AC and 12 for PRESENCE_MULTI_ZONE_EVT -- and a
     * partner registering a rule for any of them would overrun both, through a documented
     * public API, with no diagnostic anywhere.
     *
     * Clamping here rather than at the call site because this function's contract already
     * says "1 or 2": the header promised it, the code did not. Recording the leading two
     * fields of a wider attribute is lossy, so IOT_RecordAppend() warns when it happens --
     * silence here, visibility there.
     */
    return (count > 2) ? 2 : (uint8_t) count;
}

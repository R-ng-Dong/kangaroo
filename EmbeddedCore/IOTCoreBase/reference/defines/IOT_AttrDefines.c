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
 */

#include "defines/IOT_AttrDefines.h"

uint16_t IOT_AttrGetSize(uint16_t attrId)
{
    switch (attrId)
    {
    case IOT_ATTR_ONOFF:                return 2;
    case IOT_ATTR_AC:                   return 1 + 1 + 2 + 1 + 1; // power, mode, temp, fan, swing
    case IOT_ATTR_TEMP_SET:             return 2;
    case IOT_ATTR_BRIGHTNESS:           return 2;
    case IOT_ATTR_KELVIN:               return 2;
    case IOT_ATTR_BRIGHTNESS_KELVIN:    return 2 * 2;
    case IOT_ATTR_COLOR_HSV:            return 2 * 3;
    case IOT_ATTR_LUX_EVT:             return 2;
    case IOT_ATTR_TEMP_EVT:            return 2;
    case IOT_ATTR_HUMID_EVT:           return 2;
    case IOT_ATTR_TEMP_HUMID_EVT:      return 2 * 2;
    case IOT_ATTR_OPEN_CLOSE_CTL:      return 1 * 2;
    case IOT_ATTR_SETTING_LOCK_BUTTON:            return 2 * 2;
    case IOT_ATTR_BUTTON_PRESS_EVT:               return 1;
    case IOT_ATTR_PRESENCE_MULTI_ZONE_EVT:        return 1 * 5;
    case IOT_ATTR_SETTING_PRESENCE_ZONE_SENSITIVE: return 1;
    case IOT_ATTR_STT_WORKING_PARAMS:             return 2 * 1;
    /* IOT_ATTR_SYNC_ONOFF_ELEMENTS (315) is deliberately absent: its value is a
     * variable-length element list, so take N from the value length instead. */
    default:                            return 1;
    }
}

iot_err_t IOT_AttrFillDefault(uint16_t attrId, uint8_t *buffer)
{
    switch (attrId)
    {
    case IOT_ATTR_AC:
        buffer[0] = 0;   // power off
        buffer[1] = 0;   // mode
        buffer[2] = IOT_ATTR_DEFAULT_TEMP; // temp
        buffer[3] = 0;   // fan
        buffer[4] = 0;   // swing
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

    switch (attrId)
    {
    case IOT_ATTR_SMOKE_EVT:
        return (attrSize >= 1 && attrValue[0] == 1);
    case IOT_ATTR_WALL_MOUNTED_EVT:
        return (attrSize >= 1 && attrValue[0] == 1);
    case IOT_ATTR_BATTERY:
        return (attrSize >= 1 && attrValue[0] <= 20);
    default:
        return false;
    }
}

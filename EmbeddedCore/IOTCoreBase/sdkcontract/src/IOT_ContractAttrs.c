/*
 * The attribute table. Rogo's standard attributes are the switch statements below; a product's
 * own attributes come from IOT_ContractRegisterAttrs().
 */
#include "IOT_ContractAttrsInternal.h"

#include <string.h>

#include "IOT_Log.h"
#include "IOT_Memory.h"

static const char *TAG = "ContractAttrs";

/* ---- the product's own attributes (IOT_ContractRegisterAttrs()) ---- */

typedef struct
{
    uint16_t id;
    uint8_t fieldCount;
    bool restore;
    uint8_t defaultValue[IOT_CONTRACT_ATTR_FIELDS_MAX * 2];
} registered_attr_t;

static registered_attr_t *s_registered = NULL;
static uint16_t s_registeredCount = 0;
static bool s_frozen = false;

static const registered_attr_t *findRegistered(uint16_t attrId)
{
    for (uint16_t i = 0; i < s_registeredCount; i++)
    {
        if (s_registered[i].id == attrId)
            return &s_registered[i];
    }
    return NULL;
}

/* Unknown ids already warned about this boot, so one stray id does not warn on every report. A
 * small ring: an id that falls out of it warns once more, which is better than never. */
#define WARNED_RING 8
static uint16_t s_warned[WARNED_RING];
static uint8_t s_warnedCount = 0;
static uint8_t s_warnedNext = 0;

static bool alreadyWarned(uint16_t attrId)
{
    for (uint8_t i = 0; i < s_warnedCount; i++)
    {
        if (s_warned[i] == attrId)
            return true;
    }
    return false;
}

void IOT_ContractAttrMarkWarned(uint16_t attrId)
{
    if (alreadyWarned(attrId))
        return;
    s_warned[s_warnedNext] = attrId;
    s_warnedNext = (uint8_t) ((s_warnedNext + 1) % WARNED_RING);
    if (s_warnedCount < WARNED_RING)
        s_warnedCount++;
}

/*
 * Sizes count 16-bit fields: every size is 2 * N. One field is the default, so only multi-field
 * attributes are listed. The cloud, the app and every other device size values by the same rules,
 * so a size here is part of the contract: never change one on this side alone.
 */
static uint16_t standardSize(uint16_t attrId)
{
    switch (attrId)
    {
    //RD_NOTE
    case IOT_ATTR_APP_TDS_STATUS:
        return 2*1;
    /* These product attributes carry 32-bit cumulative counters. The record format
     * stores at most two 16-bit fields, which preserves the leading 4 bytes. */
    // case IOT_ATTR_APP_WATER_CONSUMPTION:
    //     return 2*2;
    case IOT_ATTR_APP_ENERGY:
        return 2*2;

    case IOT_ATTR_ONOFF:                return 2 * 1;
    case IOT_ATTR_AC:                   return 2 * 5; // power, mode, temp, fan, swing
    case IOT_ATTR_TEMP_SET:             return 2 * 1;
    case IOT_ATTR_BRIGHTNESS:           return 2 * 1;
    case IOT_ATTR_KELVIN:               return 2 * 1;
    case IOT_ATTR_BRIGHTNESS_KELVIN:    return 2 * 2;
    case IOT_ATTR_COLOR_HSV:            return 2 * 3;
    /* Three fields and two fields. */
    case IOT_ATTR_COLOR_HSL:            return 2 * 3; // hue + saturation + lightness
    case IOT_ATTR_MOTION_LUX_EVT:       return 2 * 2; // motion + lux
    /* IOT_ATTR_FAN_SPEED is one field (the default), as devices already in the field send it. */
    case IOT_ATTR_LUX_EVT:              return 2 * 1;
    case IOT_ATTR_TEMP_EVT:             return 2 * 1;
    case IOT_ATTR_HUMID_EVT:            return 2 * 1;
    case IOT_ATTR_TEMP_HUMID_EVT:       return 2 * 2;
    case IOT_ATTR_OPEN_CLOSE_CTL:       return 2 * 2;
    case IOT_ATTR_SETTING_LOCK_BUTTON:  return 2 * 2;
    case IOT_ATTR_PRESENCE_MULTI_ZONE_EVT:         return 4 * 2 + 2 * 4 * 2;
    case IOT_ATTR_EVT_PRESENCE_STATE:   return 2 * 1;
    /* The power attributes are one field (the default). */
    case IOT_ATTR_SETTING_PRESENCE_ZONE_SENSITIVE: return 2 + 2 * 1;
    case IOT_ATTR_STT_WORKING_PARAMS:              return 2 * 1;
    /* Water purifier: filter / part lifetime and replacement, consumption, issue mask. */
    case IOT_ATTR_SETTING_PRODUCT_REPLACE:         return 2 * 11; // new-model flag 2 + expire time 4 + product id 16
    case IOT_ATTR_INFO_PRODUCT_USAGE_TIME:         return 2 * 12; // product id 16 + expire time 4 + used time 4
    case IOT_ATTR_INFO_WATER_CONSUMED:             return 2 * 2;
    case IOT_ATTR_INFO_CURRENT_ISSUES_STATUS:      return 2 * 2;

    /* One field: every attribute not listed above (IOT_ATTR_BUTTON_PRESS_EVT among them). */
    default:                            return 2 * 1;
    }
}

static bool standardKnown(uint16_t attrId)
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
    case IOT_ATTR_SETTING_PRODUCT_REPLACE:
    /* Device information */
    case IOT_ATTR_INFO_PRODUCT_USAGE_TIME:
    case IOT_ATTR_INFO_WATER_CONSUMED:
    case IOT_ATTR_INFO_CURRENT_ISSUES_STATUS:
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
    const registered_attr_t *own = standardKnown(attrId) ? NULL : findRegistered(attrId);
    if (own != NULL)
    {
        memcpy(buffer, own->defaultValue, (size_t) own->fieldCount * 2);
        return IOT_OK;
    }
    switch (attrId)
    {
    case IOT_ATTR_AC:
        /* Five 16-bit fields, big-endian: power, mode, temp, fan, swing (10 bytes, as
         * IOT_AttrGetSize()). Only the temperature field has a non-zero default. */
        memset(buffer, 0, 2 * 5); /* power, mode, fan, swing all default to 0 */
        buffer[4] = (IOT_ATTR_DEFAULT_TEMP >> 8) & 0xFF;
        buffer[5] = IOT_ATTR_DEFAULT_TEMP & 0xFF;
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

bool IOT_AttrIsNotifyWorthy(uint16_t attrId, uint8_t *attrValue, uint16_t attrSize)
{
    if (attrValue == NULL || attrSize == 0)
    {
        return false;
    }

    /* Values are big-endian, so the last byte carries the meaning: a 2-byte value is
     * {0x00, v}. Reading it here lets callers pass the whole value. */
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

uint16_t IOT_AttrGetSize(uint16_t attrId)
{
    if (standardKnown(attrId))
        return standardSize(attrId);
    const registered_attr_t *own = findRegistered(attrId);
    if (own != NULL)
        return (uint16_t) (own->fieldCount * 2);
    if (attrId != IOT_ATTR_NONE && !alreadyWarned(attrId))
    {
        IOT_ContractAttrMarkWarned(attrId);
        IOT_LOGW(TAG, "attribute 0x%04x (%u) is neither standard nor registered: sized as one field "
                      "(2 bytes); register it with IOT_ContractRegisterAttrs()",
                 attrId, (unsigned) attrId);
    }
    return standardSize(attrId);
}

bool IOT_AttrIsKnown(uint16_t attrId)
{
    return standardKnown(attrId) || findRegistered(attrId) != NULL;
}

/* Controls a user sets whose state the power cut took away, which a boot restore replays. Left out
 * on purpose:
 *  - a door lock: it must never unlock itself after a power cut;
 *  - IOT_ATTR_OPEN_CLOSE_CTL: a command (open, close, stop), not a state;
 *  - IOT_ATTR_POSITION: a curtain stays where it is when the power goes, and the stored position
 *    may be a target it never reached -- replaying it can only move a motor unasked;
 *  - IOT_ATTR_AC, IOT_ATTR_MODE, IOT_ATTR_TEMP_SET: an air conditioner keeps its own state, and
 *    replaying it drives the unit (an IR burst) at every reboot.
 * A product that wants any of these back restores it itself. */
static bool standardRestorable(uint16_t attrId)
{
    switch (attrId)
    {
    case IOT_ATTR_ONOFF:
    case IOT_ATTR_FAN_SWING:
    case IOT_ATTR_FAN_SPEED:
    case IOT_ATTR_BRIGHTNESS:
    case IOT_ATTR_KELVIN:
    case IOT_ATTR_BRIGHTNESS_KELVIN:
    case IOT_ATTR_COLOR_HSV:
    case IOT_ATTR_COLOR_HSL:
    case IOT_ATTR_COLOR_HSV_CHANNEL:
    case IOT_ATTR_LEVEL:
    case IOT_ATTR_MOTION_SENSITIVE_LV:
    case IOT_ATTR_SETTING_LOCK_BUTTON:
    case IOT_ATTR_STT_WORKING_PARAMS:
    case IOT_ATTR_SETTING_PRESENCE_ZONE_SENSITIVE:
        return true;
    default:
        return false;
    }
}

bool IOT_AttrIsRestorable(uint16_t attrId)
{
    if (standardKnown(attrId))
        return standardRestorable(attrId);
    const registered_attr_t *own = findRegistered(attrId);
    return own != NULL && own->restore;
}

void IOT_ContractAttrsFreeze(void)
{
    s_frozen = true;
}

/* One entry's own checks: range and width. */
static iot_err_t checkEntry(const IOT_ContractAttrDef_t *d)
{
    if (d->id < IOT_CONTRACT_ATTR_ID_MIN || d->id > IOT_CONTRACT_ATTR_ID_MAX)
    {
        IOT_LOGE(TAG, "attribute 0x%04x not registered: a product's own ids are 0x%04x-0x%04x", d->id,
                 IOT_CONTRACT_ATTR_ID_MIN, IOT_CONTRACT_ATTR_ID_MAX);
        return IOT_ERR_INVALID_ARG;
    }
    if (d->fieldCount == 0 || d->fieldCount > IOT_CONTRACT_ATTR_FIELDS_MAX)
    {
        IOT_LOGE(TAG, "attribute 0x%04x not registered: fieldCount %u, must be 1-%u", d->id,
                 (unsigned) d->fieldCount, (unsigned) IOT_CONTRACT_ATTR_FIELDS_MAX);
        return IOT_ERR_INVALID_ARG;
    }
    return IOT_OK;
}

iot_err_t IOT_ContractRegisterAttrs(const IOT_ContractAttrDef_t *defs, uint16_t count)
{
    if (count == 0)
        return IOT_OK;
    if (s_frozen)
    {
        IOT_LOGE(TAG, "attribute 0x%04x not registered: register before IOT_ContractStart()",
                 defs != NULL ? defs[0].id : 0);
        return IOT_ERR_INVALID_STATE;
    }
    if (defs == NULL)
    {
        IOT_LOGE(TAG, "no attributes to register (NULL, count %u)", (unsigned) count);
        return IOT_ERR_INVALID_ARG;
    }

    /* All or nothing: check the whole call before anything is added. */
    for (uint16_t i = 0; i < count; i++)
    {
        iot_err_t err = checkEntry(&defs[i]);
        if (err != IOT_OK)
            return err;
        bool dup = findRegistered(defs[i].id) != NULL;
        for (uint16_t j = 0; j < i && !dup; j++)
            dup = (defs[j].id == defs[i].id);
        if (dup)
        {
            IOT_LOGE(TAG, "attribute 0x%04x not registered: registered twice", defs[i].id);
            return IOT_ERR_ALREADY_EXISTS;
        }
    }

    registered_attr_t *grown = (registered_attr_t *) Mem_SafeRealloc(
        s_registered, sizeof(registered_attr_t) * (size_t) (s_registeredCount + count), TAG, "attr table");
    if (grown == NULL)
    {
        IOT_LOGE(TAG, "attribute 0x%04x not registered: no memory", defs[0].id);
        return IOT_ERR_NO_MEM;
    }
    s_registered = grown;
    for (uint16_t i = 0; i < count; i++)
    {
        registered_attr_t *r = &s_registered[s_registeredCount + i];
        memset(r, 0, sizeof(*r));
        r->id = defs[i].id;
        r->fieldCount = defs[i].fieldCount;
        r->restore = defs[i].restore;
        if (defs[i].defaultValue != NULL)
            memcpy(r->defaultValue, defs[i].defaultValue, (size_t) defs[i].fieldCount * 2);
    }
    s_registeredCount = (uint16_t) (s_registeredCount + count);
    IOT_LOGI(TAG, "%u attribute(s) of the product registered", (unsigned) count);
    return IOT_OK;
}

uint16_t IOT_ContractAttrSize(uint16_t attrId)
{
    return IOT_AttrGetSize(attrId);
}

bool IOT_ContractAttrIsKnown(uint16_t attrId)
{
    return IOT_AttrIsKnown(attrId);
}

#pragma once

/**
 * @file IOT_ContractAttrs.h
 * @brief The attribute table: attribute ids, their value constants, device types.
 *
 * This is the cloud's attribute vocabulary, shared by every device, the cloud and the app. It
 * ships with the contract and is not edited: a product that needs attributes of its own
 * registers them with IOT_ContractRegisterAttrs() (ids 0xF700-0xFEFF).
 *
 *   - IOT_ATTR_*    -- attribute ids
 *   - IOT_DEVTYPE_* -- element types
 *   - iot_attr_*_t  -- attribute value constants (IOT_ON/IOT_OFF, fan speeds, modes, ...)
 *
 * Sizes count 16-bit fields: an attribute of N fields carries 2*N bytes. Ask the table rather
 * than assuming: IOT_ContractAttrSize().
 */

#include <stdbool.h>
#include <stddef.h> /* NULL: a registered attribute with no default */
#include <stdint.h>

#include "IOT_ErrorManager.h"

#ifdef __cplusplus
extern "C" {
#endif

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
#define IOT_ATTR_EVT_PRESENCE_STATE     72
/* Power block (0x004B..0x004E). */
#define IOT_ATTR_POWER_VOLTAGE          75   /* 0x004B */
#define IOT_ATTR_POWER_CONSUMED         76   /* 0x004C — cumulative energy */
#define IOT_ATTR_POWER_CONSUMED_INSTANCE 77  /* 0x004D — instantaneous power */
#define IOT_ATTR_POWER_CURRENT_INSTANCE 78   /* 0x004E — electrical current */

/* Product attributes: Record keeps the leading 32-bit cumulative counter. */
#define IOT_ATTR_APP_TDS_STATUS        160
// #define IOT_ATTR_APP_WATER_CONSUMPTION 45569
#define IOT_ATTR_APP_ENERGY            63233

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
// Sync-on-off element list: a list of element ids used by a sync-on-off automation (not a
// device attribute).
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
#define IOT_ATTR_SETTING_PRODUCT_REPLACE        62624  // 0xF4A0, 11 fields: new-model flag, expire time, product id

// Device information attributes (0xB0xx-0xB4xx range)
#define IOT_ATTR_INFO_PRODUCT_USAGE_TIME        45568  // 0xB200, 12 fields: product id, expire time, used time
#define IOT_ATTR_INFO_WATER_CONSUMED            45569  // 0xB201, 2 fields: 32-bit total
#define IOT_ATTR_INFO_CURRENT_ISSUES_STATUS     46096  // 0xB410, 2 fields: 32-bit issue mask

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
#define IOT_DEVTYPE_WATER_PURIFIER      112
#define IOT_DEVTYPE_WATER_FILTER_CARTRIDGE 113
#define IOT_DEVTYPE_GATEWAY             192

// ============================================================================
// Your own attributes
// ============================================================================

#define IOT_CONTRACT_ATTR_ID_MIN     0xF700 /* first id a product may register */
#define IOT_CONTRACT_ATTR_ID_MAX     0xFEFF /* last id a product may register */
#define IOT_CONTRACT_ATTR_FIELDS_MAX 8      /* widest value: 8 fields, 16 bytes */

/** One attribute of your own. */
typedef struct
{
    uint16_t id;                 /* IOT_CONTRACT_ATTR_ID_MIN..IOT_CONTRACT_ATTR_ID_MAX */
    uint8_t fieldCount;          /* 16-bit fields in its value: 1..IOT_CONTRACT_ATTR_FIELDS_MAX */
    const uint8_t *defaultValue; /* fieldCount * 2 bytes, big-endian; NULL: all zeros */
    bool restore;                /* true: a control the user sets, replayed through apply at boot
                                    (see IOT_ContractRestoreConfigure()); false (default): never */
} IOT_ContractAttrDef_t;

/**
 * @brief Register your product's own attributes. Optional; call before IOT_ContractStart().
 *
 * The standard attributes ship with the contract; register only ids of your own, in
 * 0xF700-0xFEFF, the same ids the cloud has for your product. The contract copies @p defs,
 * so the array may live on the stack. Each call adds to what earlier calls registered.
 *
 * A registered attribute is sized fieldCount * 2 bytes, starts at defaultValue, is never a
 * push-notification trigger, and keeps up to two fields per history entry.
 *
 * All or nothing: if any entry is refused, nothing in the call is registered, and an error
 * naming the id is logged.
 *
 * Blocks: no. Safe from a timer callback: no. Task: the boot task (app_main), before
 * IOT_ContractStart().
 *
 * @param defs  Your attributes.
 * @param count Number of entries; 0 does nothing.
 * @return IOT_OK;
 *         IOT_ERR_INVALID_ARG    defs is NULL with count > 0, an id outside 0xF700-0xFEFF, or a
 *                                fieldCount of 0 or more than IOT_CONTRACT_ATTR_FIELDS_MAX;
 *         IOT_ERR_ALREADY_EXISTS an id given twice, or registered by an earlier call;
 *         IOT_ERR_INVALID_STATE  called after IOT_ContractStart();
 *         IOT_ERR_NO_MEM         no memory for the copy.
 */
iot_err_t IOT_ContractRegisterAttrs(const IOT_ContractAttrDef_t *defs, uint16_t count);

// ============================================================================
// Reading the table
// ============================================================================

/**
 * @brief Size in bytes of an attribute's value (2 per 16-bit field).
 *
 * Standard attributes first, then the ones registered with IOT_ContractRegisterAttrs(). An id
 * that is neither is sized as one field (2 bytes) and logs a warning once.
 *
 * Blocks: no. Safe from a timer callback: yes. Task: any.
 */
uint16_t IOT_ContractAttrSize(uint16_t attrId);

/**
 * @brief Is this attribute id known: in the standard table or registered?
 *
 * IOT_ATTR_NONE (0) is not known: it is the absence of an attribute. The *_DEPRECATED ids are
 * known.
 *
 * Blocks: no. Safe from a timer callback: yes. Task: any.
 */
bool IOT_ContractAttrIsKnown(uint16_t attrId);

#ifdef __cplusplus
}
#endif

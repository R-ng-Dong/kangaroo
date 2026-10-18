#ifndef DEVICE_ELEMENT_MANAGER_H_
#define DEVICE_ELEMENT_MANAGER_H_

#include <string.h>
#include "common.h"

#define DEVICE_ELEMENT_MAX_ATTRIBUTE_VALUE_LENGTH 24U

#define MAX_NUM_ELEMENT 5
#define MAX_NUM_ATTRIBUTE 5

#define ELEMENT_ID_1 0x0001
#define ELEMENT_ID_2 0x0002
#define ELEMENT_ID_3 0x0003
#define ELEMENT_ID_4 0x0004
#define ELEMENT_ID_5 0x0005

#define ATTR_ONOFF 1
#define ATTR_TEMP_SET 20
#define ATTR_INFOMATION_PRODUCT_USAGE_TIME 45568
#define ATTR_INFOMATION_WATER_CONSUMER 45569
#define ATTR_EVENT_TDS_STATUS 160
#define ATTR_FILTER_SETTING 62624
#define ATTR_INFO_CURRENT_ISSUES_STATUS 46096

#define ATTR_ERROR_CODE 63232


typedef struct
{
    UI16 id;
    const char *name;
    UI8 value_length;
    _bool has_value;
    UI8 value[DEVICE_ELEMENT_MAX_ATTRIBUTE_VALUE_LENGTH];
} DeviceElementAttributeInfo;

typedef struct
{
    UI16 id;
    const char *name;
    const DeviceElementAttributeInfo *attributes;
    UI8 attribute_count;
} DeviceElementInfo;

UI8 DeviceElementGetCount(void);
const DeviceElementInfo *DeviceElementGetByIndex(UI8 index);
const DeviceElementInfo *DeviceElementGetById(UI16 element_id);
const DeviceElementAttributeInfo *DeviceElementGetAttribute(UI16 element_id, UI16 attribute_id);
_bool DeviceElementSetAttributeValue(UI16 element_id, UI16 attribute_id, const UI8 *value, UI8 value_length);
_bool DeviceElementSetAttributeValueOffset(UI16 element_id, UI16 attribute_id, UI8 offset, const UI8 *value, UI8 value_length);
_bool DeviceElementGetAttributeValue(UI16 element_id, UI16 attribute_id, UI8 *value_out, UI8 value_capacity, UI8 *value_length_out);
void DeviceElementResetAttributeValues(void);
const UI16 IndexToElementId(UI8 idx);
const UI8 ElementIdToIndex(UI16 elem_id);

#endif

#include "device_element_manager.h"

#define ATTRIBUTE(id_, name_, length_) { .id = (id_), .name = (name_), .value_length = (length_), .has_value = D_FALSE, .value = {0} }
#define ARRAY_COUNT(array_) (sizeof(array_) / sizeof((array_)[0]))

static DeviceElementAttributeInfo s_element_1_attributes[] = {
    ATTRIBUTE(62624, "filter_setting", 22),
    ATTRIBUTE(45568, "filter_lifetime", 24),
    ATTRIBUTE(45569, "water_flow", 4),
    ATTRIBUTE(1, "on_off", 2),
    ATTRIBUTE(63232, "error_code", 4),
};

static DeviceElementAttributeInfo s_element_2_attributes[] = {
    ATTRIBUTE(62624, "filter_setting", 22),
    ATTRIBUTE(45568, "filter_lifetime", 24),
    ATTRIBUTE(160, "tds_input", 2),
};

static DeviceElementAttributeInfo s_element_3_attributes[] = {
    ATTRIBUTE(62624, "filter_setting", 22),
    ATTRIBUTE(45568, "filter_lifetime", 24),
    ATTRIBUTE(160, "tds_output", 2),
};

static DeviceElementAttributeInfo s_element_4_attributes[] = {
    ATTRIBUTE(62624, "filter_setting", 22),
    ATTRIBUTE(45568, "filter_lifetime", 24),
    ATTRIBUTE(20, "hot_tank_temperature_setting", 2),
    ATTRIBUTE(1, "onoff_hot_tank", 2),
};

static DeviceElementAttributeInfo s_element_5_attributes[] = {
    ATTRIBUTE(62624, "filter_setting", 22),
    ATTRIBUTE(45568, "filter_lifetime", 24),
    ATTRIBUTE(20, "cold_tank_temperature_setting", 2),
    ATTRIBUTE(1, "onoff_cold_tank", 2),
};

static DeviceElementInfo s_elements[] = {
    { .id = ELEMENT_ID_1, .name = "element_1", .attributes = s_element_1_attributes, .attribute_count = ARRAY_COUNT(s_element_1_attributes) },
    { .id = ELEMENT_ID_2, .name = "element_2", .attributes = s_element_2_attributes, .attribute_count = ARRAY_COUNT(s_element_2_attributes) },
    { .id = ELEMENT_ID_3, .name = "element_3", .attributes = s_element_3_attributes, .attribute_count = ARRAY_COUNT(s_element_3_attributes) },
    { .id = ELEMENT_ID_4, .name = "element_4", .attributes = s_element_4_attributes, .attribute_count = ARRAY_COUNT(s_element_4_attributes) },
    { .id = ELEMENT_ID_5, .name = "element_5", .attributes = s_element_5_attributes, .attribute_count = ARRAY_COUNT(s_element_5_attributes) },
};

UI8 DeviceElementGetCount(void)
{
    return ARRAY_COUNT(s_elements);
}

const DeviceElementInfo *DeviceElementGetByIndex(UI8 index)
{
    return (index < ARRAY_COUNT(s_elements)) ? &s_elements[index] : NULL;
}

const DeviceElementInfo *DeviceElementGetById(UI16 element_id)
{
    for (UI8 i = 0U; i < ARRAY_COUNT(s_elements); ++i)
    {
        if (s_elements[i].id == element_id)
        {
            return &s_elements[i];
        }
    }
    return NULL;
}

const DeviceElementAttributeInfo *DeviceElementGetAttribute(UI16 element_id, UI16 attribute_id)
{
    const DeviceElementInfo *element = DeviceElementGetById(element_id);
    if (element == NULL)
    {
        return NULL;
    }

    for (UI8 i = 0U; i < element->attribute_count; ++i)
    {
        if (element->attributes[i].id == attribute_id)
        {
            return &element->attributes[i];
        }
    }
    return NULL;
}

_bool DeviceElementSetAttributeValue(UI16 element_id, UI16 attribute_id, const uint8_t *value, UI8 value_length)
{
    if (value == NULL)
    {
        return D_FALSE;
    }

    const DeviceElementInfo *element = DeviceElementGetById(element_id);
    if (element == NULL)
    {
        return D_FALSE;
    }

    DeviceElementAttributeInfo *attributes = (DeviceElementAttributeInfo *)element->attributes;
    for (UI8 i = 0U; i < element->attribute_count; ++i)
    {
        DeviceElementAttributeInfo *entry = &attributes[i];
        if (entry->id == attribute_id)
        {
            if (value_length != entry->value_length || value_length > sizeof(entry->value))
            {
                return D_FALSE;
            }
            memcpy(entry->value, value, value_length);
            entry->has_value = D_TRUE;
            return D_TRUE;
        }
    }
    return D_FALSE;
}

_bool DeviceElementSetAttributeValueOffset(UI16 element_id, UI16 attribute_id, UI8 offset, const uint8_t *value, UI8 value_length)
{
    if (value == NULL)
    {
        return D_FALSE;
    }

    const DeviceElementInfo *element = DeviceElementGetById(element_id);
    if (element == NULL)
    {
        return D_FALSE;
    }

    DeviceElementAttributeInfo *attributes = (DeviceElementAttributeInfo *)element->attributes;
    for (UI8 i = 0U; i < element->attribute_count; ++i)
    {
        DeviceElementAttributeInfo *entry = &attributes[i];
        if (entry->id == attribute_id)
        {
            if ((offset > entry->value_length) ||
                (value_length > (entry->value_length - offset)))
            {
                return D_FALSE;
            }
            if (value_length > 0U)
            {
                memcpy(entry->value + offset, value, value_length);
            }
            entry->has_value = D_TRUE;
            return D_TRUE;
        }
    }
    return D_FALSE;
}

_bool DeviceElementGetAttributeValue(UI16 element_id, UI16 attribute_id, uint8_t *value_out, UI8 value_capacity, UI8 *value_length_out)
{
    if (value_out == NULL || value_length_out == NULL)
    {
        return D_FALSE;
    }

    const DeviceElementInfo *element = DeviceElementGetById(element_id);
    if (element == NULL)
    {
        return D_FALSE;
    }

    const DeviceElementAttributeInfo *attributes = element->attributes;
    for (UI8 i = 0U; i < element->attribute_count; ++i)
    {
        const DeviceElementAttributeInfo *entry = &attributes[i];
        if (entry->id == attribute_id)
        {
            if (!entry->has_value || value_capacity != entry->value_length)
            {
                return D_FALSE;
            }
            memcpy(value_out, entry->value, entry->value_length);
            *value_length_out = entry->value_length;
            return D_TRUE;
        }
    }
    return D_FALSE;
}

void DeviceElementResetAttributeValues(void)
{
    for (UI8 i = 0U; i < ARRAY_COUNT(s_elements); ++i)
    {
        DeviceElementAttributeInfo *attributes = (DeviceElementAttributeInfo *)s_elements[i].attributes;
        for (UI8 j = 0U; j < s_elements[i].attribute_count; ++j)
        {
            memset(attributes[j].value, 0, sizeof(attributes[j].value));
            attributes[j].has_value = D_FALSE;
        }
    }
}

const UI16 IndexToElementId(UI8 idx)
{
    return idx + 1;
}

const UI8 ElementIdToIndex(UI16 elem_id)
{
    if(elem_id == 0x0000) return 0;
    return elem_id - 1;
}

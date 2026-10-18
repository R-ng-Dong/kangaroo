#ifndef EVENT_BITS_H
#define EVENT_BITS_H

#include "common.h"

typedef enum
{
    EVENT_INLET_WATER_QUALITY_WARNING = 0,
    EVENT_OUTLET_WATER_QUALITY_FAULT,
    EVENT_WATER_LEAK, // unuse
    EVENT_TANK_LOW, // unuse
    EVENT_RO_PUMP_RUNTIME_OVER_60_MIN,
    EVENT_FILTER1_LIFE_70,
    EVENT_FILTER2_LIFE_70,
    EVENT_FILTER3_LIFE_70,
    EVENT_FILTER4_LIFE_70,
    EVENT_FILTER5_LIFE_70,
    EVENT_FILTER1_LIFE_90,
    EVENT_FILTER2_LIFE_90,
    EVENT_FILTER3_LIFE_90,
    EVENT_FILTER4_LIFE_90,
    EVENT_FILTER5_LIFE_90,
    EVENT_OUTPUT_WATER_QUALITY_WARNING,
    EVENT_HEATER_SENSOR_FAULT,
    EVENT_HEATER_RUNTIME_OVER_1H,
    EVENT_COOLER_SENSOR_FAULT,
    EVENT_COOLER_RUNTIME_OVER_2H,

    EVENT_MAX_NUM
}EventID;

typedef void (*EventManagerCallback)(EventID event_id, void *data);

_bool EventManagerInit(EventManagerCallback cb);
_bool EventManagerPost(EventID event_id, const void *event_data, UI16 event_data_size);

#endif

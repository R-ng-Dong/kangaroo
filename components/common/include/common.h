#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>


#define HOT_TANK_POWER_W       450 // W/h
#define COLD_TANK_POWER_W       84 // W/h
#define MCU_IOT_POWER_W          1 // W/h
#define PUMP_VALVE_POWER_W      26 // W/h
#define ELECTROLYSIS_POWER_W    38 // W/h

#define SECONDS_PER_HOUR      3600 // 1 hour = 60x60 s
#define MINUTES_PER_DAY       1440

#define D_TRUE true
#define D_FALSE false

#define UNUSED(x) ((void)(x))

typedef int INT;

typedef uint8_t   UI8;
typedef uint16_t  UI16;
typedef uint32_t  UI32;
typedef uint64_t  UI64;

typedef int8_t    SI8;
typedef int16_t   SI16;
typedef int32_t   SI32;
typedef int64_t   SI64;

typedef bool _bool;

typedef enum
{
    WATER_PURIFIER_TYPE_STANDARD = 0,
    WATER_PURIFIER_TYPE_HOT_COLD,
}WaterPurifierType;

typedef enum {
    SYS_STATE_BOOT = 0,
    SYS_STATE_IDLE,
    SYS_STATE_RUNNING,
    SYS_STATE_ERROR,
    SYS_STATE_MAINTENANCE
} system_state_t;

#endif

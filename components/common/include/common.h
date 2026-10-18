#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>

#define WATER_PURIFIER_TYPE_STANDARD   0
#define WATER_PURIFIER_TYPE_HOT_COLD   1

#define DEVICE_TYPE WATER_PURIFIER_TYPE_STANDARD

#define D_TRUE true
#define D_FALSE false

#define UNUSED(x) ((void)(x))

typedef uint8_t   UI8;
typedef uint16_t  UI16;
typedef uint32_t  UI32;
typedef uint64_t  UI64;

typedef int8_t    SI8;
typedef int16_t   SI16;
typedef int32_t   SI32;
typedef int64_t   SI64;

typedef bool _bool;

typedef enum {
    SYS_STATE_BOOT = 0,
    SYS_STATE_IDLE,
    SYS_STATE_RUNNING,
    SYS_STATE_ERROR,
    SYS_STATE_MAINTENANCE
} system_state_t;

#endif

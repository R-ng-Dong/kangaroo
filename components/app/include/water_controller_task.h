#ifndef WATER_CONTROLLER_TASK_H
#define WATER_CONTROLLER_TASK_H

#include "water_controller.h"
#include "uart_protocol.h"

typedef enum
{
    WATER_CONTROL_SEND_FEATURE_HOT,
    WATER_CONTROL_SEND_FEATURE_COLD,
    WATER_CONTROL_SEND_FILTER,
    WATER_CONTROL_SEND_RO_FLUSHING, 
} WaterControlCommand_t;

typedef enum
{
    WATER_CONTROL_VERIFY_NONE,
    WATER_CONTROL_VERIFY_SETTING_BIT,
    WATER_CONTROL_VERIFY_HOT_TEMPERATURE,
    WATER_CONTROL_VERIFY_COLD_TEMPERATURE,
    WATER_CONTROL_VERIFY_FILTER_TIME,
    WATER_CONTROL_VERIFY_RO_STATUS,
} WaterControlVerify_t;

typedef struct
{
    WaterControlCommand_t command;
    WaterControlVerify_t verify1;
    WaterControlVerify_t verify2;
    UI32 value1; // input 1
    UI32 value2; // input 2
    UI32 expected1;
    UI32 expected2;
} WaterControlRequest_t;

_bool WaterControllerTaskInit(void);
_bool WaterControllerTaskEnqueue(const WaterControlRequest_t *request);

#endif

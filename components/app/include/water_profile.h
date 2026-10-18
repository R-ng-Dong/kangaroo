#ifndef WATER_PROFILE_H
#define WATER_PROFILE_H

#include "common.h"

#define BIT_0     (1U << 0)
#define BIT_1     (1U << 1)
#define BIT_2     (1U << 2)
#define BIT_3     (1U << 3)
#define BIT_4     (1U << 4)
#define BIT_5     (1U << 5)
#define BIT_6     (1U << 6)
#define BIT_7     (1U << 7)

#define KGR_MAX_INDEX    8

#define STATUS_BIT(x) (((x) >= 1 && (x) <= KGR_MAX_INDEX) ? (1U << ((x) - 1)) : 0U)

#define INPUT_STATUS(x)   STATUS_BIT(x)
#define OUTPUT_STATUS(x)  STATUS_BIT(x)
#define SWITCH_STATUS(x)  STATUS_BIT(x)
#define LED_STATUS(x)     STATUS_BIT(x)

#define HOT_SWITCH_BIT      BIT_0
#define COLD_SWITCH_BIT     BIT_1
#define ENERGY_SAVING_BIT   BIT_2

// RO status
#define RO_STATUS_PUMP_STOPPED 0x00
#define RO_STATUS_PUMP_RUNNING 0x01
#define RO_STATUS_PUMP_RUNNING_TOO_LONG 0x02
#define RO_STATUS_FLUSHING 0x0B

// PLY status
#define PLY_STATUS_ELECTROLYSIS_STOPPED 0x00
#define PLY_STATUS_ELECTROLYSIS_RUNNING 0x01
#define PLY_STATUS_ELECTROLYSIS_RUNNING_TOO_LONG 0x02

// Hot tank status
#define HOT_TANK_STATUS_HEATER_STOPPED 0x00
#define HOT_TANK_STATUS_HEATER_RUNNING 0x01
#define HOT_TANK_STATUS_HEATER_RUNNING_TOO_LONG 0x02
#define HOT_TANK_STATUS_TEMPERATURE_SENSOR_ERROR 0x03
#define HOT_TANK_STATUS_LOAD_BROKEN 0x04
#define HOT_TANK_STATUS_ENERGY_SAVING 0x0A
#define HOT_TANK_STATUS_HEATER_DISABLED 0x0F

// Cold tank status
#define COLD_TANK_STATUS_COMPRESSOR_STOPPED 0x00
#define COLD_TANK_STATUS_COMPRESSOR_RUNNING 0x01
#define COLD_TANK_STATUS_COMPRESSOR_RUNNING_TOO_LONG 0x02
#define COLD_TANK_STATUS_TEMPERATURE_SENSOR_ERROR 0x03
#define COLD_TANK_STATUS_LOAD_BROKEN 0x04
#define COLD_TANK_STATUS_ENERGY_SAVING 0x0A
#define COLD_TANK_STATUS_COMPRESSOR_DISABLED 0x0F

typedef enum {
    FILTER_TIME_INDEX_1ST = 0,
    FILTER_TIME_INDEX_2ND = 1,
    FILTER_TIME_INDEX_3RD = 2,
    FILTER_TIME_INDEX_RO = 3,
    FILTER_TIME_INDEX_POST = 4
} FilterTimeIndex;

typedef enum {
    RO_STATUS,
    PLY_STATUS,
    HOT_TANK_STATUS,
    COLD_TANK_STATUS
} WaterPurifyStatusType;

typedef enum {
    INPUT_STATUS,
    OUTPUT_STATUS,
    SWITCH_STATUS,
    LED_STATUS
} WaterPurifyInputStatus;

void WaterProfileInit(void);
void WaterProfileRegisterUartCallback(void);

UI32 WaterProfileGetFilterRuntimeSeconds(FilterTimeIndex filter_index);
UI8 WaterProfileGetStatus(WaterPurifyStatusType type);
UI8 WaterProfileGetSetting(void);
UI32 WaterProfileGetDeviceId(void);
UI8 WaterProfileGetInputStatus(WaterPurifyInputStatus input_type, UI8 index);
SI8 WaterProfileGetHotTankTemperature(void);
SI8 WaterProfileGetColdTankTemperature(void);

#endif

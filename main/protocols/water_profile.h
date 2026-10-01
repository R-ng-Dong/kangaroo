#ifndef WATER_PROFILE_H
#define WATER_PROFILE_H

#include "uart_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BIT_0     (1U << 0)
#define BIT_1     (1U << 1)
#define BIT_2     (1U << 2)
#define BIT_3     (1U << 3)
#define BIT_4     (1U << 4)
#define BIT_5     (1U << 5)
#define BIT_6     (1U << 6)
#define BIT_7     (1U << 7)

#define STATUS_BIT(x) (((x) >= 1 && (x) <= 8) ? (1U << ((x) - 1)) : 0U)

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
#define RO_STATUS_PUMP_RUNNING_TOO_LONG 0x02 //ERROR
#define RO_STATUS_FLUSHING 0x0B

// PLY status
#define PLY_STATUS_ELECTROLYSIS_STOPPED 0x00
#define PLY_STATUS_ELECTROLYSIS_RUNNING 0x01
#define PLY_STATUS_ELECTROLYSIS_RUNNING_TOO_LONG 0x02 //ERROR

// Hot tank status
#define HOT_TANK_STATUS_HEATER_STOPPED 0x00
#define HOT_TANK_STATUS_HEATER_RUNNING 0x01
#define HOT_TANK_STATUS_HEATER_RUNNING_TOO_LONG 0x02 //ERROR
#define HOT_TANK_STATUS_TEMPERATURE_SENSOR_ERROR 0x03 //ERROR
#define HOT_TANK_STATUS_LOAD_BROKEN 0x04 //ERROR
#define HOT_TANK_STATUS_ENERGY_SAVING 0x0A
#define HOT_TANK_STATUS_HEATER_DISABLED 0x0F

// Cold tank status
#define COLD_TANK_STATUS_COMPRESSOR_STOPPED 0x00
#define COLD_TANK_STATUS_COMPRESSOR_RUNNING 0x01
#define COLD_TANK_STATUS_COMPRESSOR_RUNNING_TOO_LONG 0x02 //ERROR
#define COLD_TANK_STATUS_TEMPERATURE_SENSOR_ERROR 0x03 //ERROR
#define COLD_TANK_STATUS_LOAD_BROKEN 0x04 //ERROR
#define COLD_TANK_STATUS_ENERGY_SAVING 0x0A
#define COLD_TANK_STATUS_COMPRESSOR_DISABLED 0x0F

typedef enum{
    FILTER_TIME_INDEX_1ST = 0,
    FILTER_TIME_INDEX_2ND,
    FILTER_TIME_INDEX_3RD,
    FILTER_TIME_INDEX_RO,
    FILTER_TIME_INDEX_POST
}FilterTimeIndex;

typedef enum{
    RO_STATUS,
    PLY_STATUS,
    HOT_TANK_STATUS,
    COLD_TANK_STATUS
}WaterPurifyStatusType;

typedef enum{
    INPUT_STATUS,
    OUTPUT_STATUS,
    SWITCH_STATUS,
    LED_STATUS
}WaterPurifyInputStatus;

extern KgrFrameType1 s_frame_type_1; 
extern KgrExternFrameTypeA s_frame_type_A;


void WaterProfileInit(void);

uint32_t WaterProfileGetFilterRuntimeSeconds(FilterTimeIndex filter_index);
uint8_t WaterProfileGetStatus(WaterPurifyStatusType type);
uint8_t WaterProfileGetSetting(void);

esp_err_t WaterProfileResetSystem(void);
esp_err_t WaterProfileSetFilterRuntimeMinutes(FilterTimeIndex filter_index, uint16_t time_m);
esp_err_t WaterProfileSetFeatureHot(uint8_t enabled, uint8_t temperature);
esp_err_t WaterProfileSetFeatureCold(uint8_t enabled, uint8_t temperature);
esp_err_t WaterProfileSetFeatureEnergySaving(uint8_t enabled);
esp_err_t WaterProfileSetTDSValue(uint16_t TDS_inlet, uint16_t TDS_ro);

// FCT test
esp_err_t WaterProfileEnableCommandTest(uint8_t enabled);
esp_err_t WaterProfileSetOutputControl(uint8_t output_idx, uint8_t control_value);
esp_err_t WaterProfileSetLedControl(uint8_t led_idx, uint8_t control_value);
esp_err_t WaterProfileSetDeviceId(uint32_t dev_id);

uint32_t WaterProfileGetDeviceId(void);
uint8_t WaterProfileGetInputStatus(WaterPurifyInputStatus input_type, uint8_t index);

#ifdef __cplusplus
}
#endif

#endif

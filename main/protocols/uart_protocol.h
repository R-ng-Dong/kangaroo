#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include "uart_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BUFFER_SIZE 128

#define KGR_BAUDRATE        115200
#define KGR_TXRX_BUF_SIZE   256

#define KGR_FRAME_HEADER 0x55
#define KGR_DATA_TYPE_1  0x01
#define KGR_DATA_TYPE_2  0x02
#define KGR_DATA_TYPE_3  0x03
#define KGR_DATA_TYPE_A  0x0A

#define KGR_MAX_OUTPUT 8
#define KGR_MAX_LED    8

typedef enum{
    CHECK_DATA = 0,
    HANDLE_DATA
} UartState;

typedef enum{
    HOT_FEATURE = 0x00,
    COLD_FEATURE = 0x01,
    ENERGY_SAVING_FEATURE = 0x02
} WaterPurifyFeature;

typedef enum{
    FIRST_FILTER  = 0x00,
    SECOND_FILTER = 0x01,
    THIRD_FILTER  = 0x02,
    RO_FILTER = 0x03,
    POST_FILTER = 0x04,
    RO_FLUSHING_START = 0x0B
} WaterPurifyFilter;

typedef struct __attribute__((packed))
{
    uint8_t header;
    uint8_t length;
    uint8_t type;
    uint8_t data[MAX_BUFFER_SIZE-3]; // Payload(length-2) + CRC(1)
} KgrDataFormat;

//main control: type 01, sys 1s
typedef struct __attribute__((packed))
{
    uint32_t system_run;            // 4.1 System run time (second)
    uint8_t  ro_status;             // 4.2 RO status
    uint16_t ro_time;               // 4.3 RO run/stop time

    uint8_t  ply_status;            // 4.4 PLY status
    uint16_t ply_time;              // 4.5 PLY run/stop time

    uint8_t  hot_tank_status;       // 4.6 Hot tank status
    uint16_t hot_tank_temperature;  // 4.7 Hot tank temperature
    uint16_t hot_tank_time;         // 4.8 Hot tank run/stop time

    uint8_t  cold_tank_status;      // 4.9 Cold tank status
    uint16_t cold_tank_temperature; // 4.10 Cold tank temperature
    uint16_t cold_tank_time;        // 4.11 Cold tank run/stop time

    uint8_t  setting_val;           // 4.12 Bit-mapped configuration flags

    uint32_t filter_time_1;         // 4.13
    uint32_t filter_time_2;         // 4.14
    uint32_t filter_time_3;         // 4.15
    uint32_t ro_filter_time;        // 4.16
    uint32_t post_filter_time;      // 4.17

    uint32_t system_time;           // 4.18 Total system operating time
    uint32_t total_pump_running_time; // 4.19
    uint32_t total_hot_running_time;  // 4.20
    uint32_t total_cold_running_time; // 4.21

    uint8_t  reserved[3];           // 4.22
} KgrFrameType1;

typedef struct __attribute__((packed))
{
    uint8_t  command; // Command code
    union
    {
        /* Command 0x00: System reset */
        struct __attribute__((packed))
        {
            uint8_t value; // 0x00
            uint8_t raw[2];
        } reset;

        /* Command 0x01: Hot / Cold / Energy Saving On/Off */
        struct __attribute__((packed))
        {
            uint8_t index;      // 0x00 Hot
                                // 0x01 Cold
                                // 0x02 Energy saving
            uint8_t value;      // 0x00 Off
                                // 0x01 On
            uint8_t value2nd; 
                                /*- For Hot (target temperature)
                                80-200(40-100^C): temperature = value/2Other/Off: Use the previous value
                                - For Cold (target temperature)
                                0-40(0-20^C): temperature = value/2
                                Other/Off: Use the previous value
                                - For Energy saving (forced escape)
                                0: Off
                                1: On
                                Other/Off: Don’t care*/
        } control;

        /* Command 0x02: Filter time set */
        struct __attribute__((packed))
        {
            uint8_t index;      // 0x00 1st filter
                                // 0x01 2nd filter
                                // 0x02 3rd filter
                                // 0x03 RO filter
                                // 0x04 Post filter
                                // 0x0B RO flushing start
            uint16_t value;     // 0x00
        } filter;

    } data;
} KgrFrameType2;

typedef struct __attribute__((packed))
{
    uint16_t tds_value1; // TDS Inlet 0-2000 ppm
    uint16_t tds_value2; // TDS RO    0-2000 ppm
    uint16_t tds_value3; // Reserved  0-2000 ppm
    uint16_t tds_value4; // Reserved  0-2000 ppm
} KgrFrameType3;

typedef struct __attribute__((packed))
{
    uint8_t command; 
    union
    {
        struct __attribute__((packed))
        {
            uint8_t value;   
            uint8_t reserved[6];   
        } enable;

        struct __attribute__((packed))
        {
            uint8_t output_index;   
            uint8_t control_value;   
            uint8_t reserved[5];   
        } output_control;

        struct __attribute__((packed))
        {
            uint8_t led_index;
            uint8_t control_value;
            uint8_t reserved[5];   
        } led_control;

        struct __attribute__((packed))
        {
            uint32_t device_id_set;
            uint8_t reserved[3];  
        } id_set;
    } data;
} KgrFrameTypeA;

typedef struct __attribute__((packed))
{
    uint32_t dev_id;
    uint8_t fw_build_ver[8];
    uint8_t input_status;
    uint8_t switch_status;
    uint8_t output_status;
    uint8_t led_status;
    uint16_t adc_raw_value_1;  //0-4095
    uint16_t adc_raw_value_2;  //0-4095
    uint8_t reserved[2];
} KgrExternFrameTypeA;

typedef void (*uart_protocol_event_handler_t)(const KgrDataFormat *frame);

void UartProtocolSetEventHandler(uart_protocol_event_handler_t handler);

esp_err_t InitBoardCommunication(const gpio_num_t tx_pin, const gpio_num_t rx_pin, const uart_port_t port_num);

esp_err_t SendSystemResetCommand(void);
esp_err_t SetFeatureEnable(WaterPurifyFeature feature, uint8_t enabled, uint8_t value);
esp_err_t SetTimeFilter(WaterPurifyFilter filter, uint16_t time);

esp_err_t SetTDSValue(uint16_t TDS_inlet, uint16_t TDS_ro);

//FCT test
esp_err_t EnableCommand(uint8_t enabled);
esp_err_t SetOutputControl(uint8_t output_idx, uint8_t control_value);
esp_err_t SetLedControl(uint8_t led_idx, uint8_t control_value);
esp_err_t SetDeviceId(uint32_t dev_id) ;

#ifdef __cplusplus
}
#endif

#endif /*UART_PROTOCOL_H*/
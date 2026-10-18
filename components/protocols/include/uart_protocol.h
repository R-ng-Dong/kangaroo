#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include "uart_driver.h"

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
    UI8 header;
    UI8 length;
    UI8 type;
    UI8 data[MAX_BUFFER_SIZE-3]; // Payload(length-2) + CRC(1)
} KgrDataFormat;

//main control: type 01, sys 1s
typedef struct __attribute__((packed))
{
    UI32 system_run;            // 4.1 System run time (second)
    UI8  ro_status;             // 4.2 RO status
    UI16 ro_time;               // 4.3 RO run/stop time

    UI8  ply_status;            // 4.4 PLY status
    UI16 ply_time;              // 4.5 PLY run/stop time

    UI8  hot_tank_status;       // 4.6 Hot tank status
    UI16 hot_tank_temperature;  // 4.7 Hot tank temperature
    UI16 hot_tank_time;         // 4.8 Hot tank run/stop time

    UI8  cold_tank_status;      // 4.9 Cold tank status
    UI16 cold_tank_temperature; // 4.10 Cold tank temperature
    UI16 cold_tank_time;        // 4.11 Cold tank run/stop time

    UI8  setting_val;           // 4.12 Bit-mapped configuration flags

    UI32 filter_time_1;         // 4.13
    UI32 filter_time_2;         // 4.14
    UI32 filter_time_3;         // 4.15
    UI32 ro_filter_time;        // 4.16
    UI32 post_filter_time;      // 4.17

    UI32 system_time;           // 4.18 Total system operating time
    UI32 total_pump_running_time; // 4.19
    UI32 total_hot_running_time;  // 4.20
    UI32 total_cold_running_time; // 4.21

    UI8  reserved[3];           // 4.22
} KgrFrameType1;

typedef struct __attribute__((packed))
{
    UI8  command; // Command code
    union
    {
        /* Command 0x00: System reset */
        struct __attribute__((packed))
        {
            UI8 value; // 0x00
            UI8 raw[2];
        } reset;

        /* Command 0x01: Hot / Cold / Energy Saving On/Off */
        struct __attribute__((packed))
        {
            UI8 index;      // 0x00 Hot
                                // 0x01 Cold
                                // 0x02 Energy saving
            UI8 value;      // 0x00 Off
                                // 0x01 On
            UI8 value2nd; 
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
            UI8 index;      // 0x00 1st filter
                                // 0x01 2nd filter
                                // 0x02 3rd filter
                                // 0x03 RO filter
                                // 0x04 Post filter
                                // 0x0B RO flushing start
            UI16 value;     // 0x00
        } filter;

    } data;
} KgrFrameType2;

typedef struct __attribute__((packed))
{
    UI16 tds_value1; // TDS Inlet 0-2000 ppm
    UI16 tds_value2; // TDS RO    0-2000 ppm
    UI16 tds_value3; // Reserved  0-2000 ppm
    UI16 tds_value4; // Reserved  0-2000 ppm
} KgrFrameType3;

typedef struct __attribute__((packed))
{
    UI8 command; 
    union
    {
        struct __attribute__((packed))
        {
            UI8 value;   
            UI8 reserved[6];   
        } enable;

        struct __attribute__((packed))
        {
            UI8 output_index;   
            UI8 control_value;   
            UI8 reserved[5];   
        } output_control;

        struct __attribute__((packed))
        {
            UI8 led_index;
            UI8 control_value;
            UI8 reserved[5];   
        } led_control;

        struct __attribute__((packed))
        {
            UI32 device_id_set;
            UI8 reserved[3];  
        } id_set;
    } data;
} KgrFrameTypeA;

typedef struct __attribute__((packed))
{
    UI32 dev_id;
    UI8 fw_build_ver[8];
    UI8 input_status;
    UI8 switch_status;
    UI8 output_status;
    UI8 led_status;
    UI16 adc_raw_value_1;  //0-4095
    UI16 adc_raw_value_2;  //0-4095
    UI8 reserved[2];
} KgrExternFrameTypeA;

typedef void (*uart_protocol_event_handler_t)(const KgrDataFormat *frame);

void UartProtocolSetEventHandler(uart_protocol_event_handler_t handler);

esp_err_t InitBoardCommunication(const gpio_num_t tx_pin, const gpio_num_t rx_pin, const uart_port_t port_num);

esp_err_t SendSystemResetCommand(void);
esp_err_t SetFeatureEnable(WaterPurifyFeature feature, UI8 enabled, UI8 value);
esp_err_t SetTimeFilter(WaterPurifyFilter filter, UI16 time);

esp_err_t SetTDSValue(UI16 TDS_inlet, UI16 TDS_ro);

//FCT test
esp_err_t EnableCommand(UI8 enabled);
esp_err_t SetOutputControl(UI8 output_idx, UI8 control_value);
esp_err_t SetLedControl(UI8 led_idx, UI8 control_value);
esp_err_t SetDeviceId(UI32 dev_id) ;

#endif /*UART_PROTOCOL_H*/
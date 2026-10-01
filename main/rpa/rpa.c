// #include "network/wifi.h"
#include "network/IOT_NetworkMgmt.h"
#include "rpa.h"
#include <string.h>
#include "esp_log.h"
#include <esp_wifi.h>
#include "driver/gpio.h"

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"

// #include "led_mgmt.h"


#define MAX_RETRY_CONNECT 5

#define RPA_INPUT_PIN       1
#define RPA_OUTPUT_PIN1     0
#define RPA_OUTPUT_PIN2     10
// #define TDS

#define MAX_ADC_SAMPLE 10
#define EXAMPLE_ADC1_CHANNEL2  ADC_CHANNEL_2 // lựa chọn chanel
#define EXAMPLE_ADC1_CHANNEL3  ADC_CHANNEL_3 // lựa chọn chanel

#define TDS1_PULSE1_PIN        10
#define TDS1_PULSE2_PIN        0

#define TDS2_PULSE1_PIN        5
#define TDS2_PULSE2_PIN        4


static const char *TAG = "RPA";

TaskHandle_t xTask_check_input = NULL;
TaskHandle_t xTask_set_output = NULL;

static int8_t rssi_test = 0;
static void adc_oneshot_init(void);

extern void rpa_test_led_rgb(uint8_t red, uint8_t green, uint8_t blue);
bool rpa_is_connected = false;

void rpa_test_disconnect(void)
{
        if(rpa_is_connected)
        {
            rpa_is_connected = false;
            rpa_test_led_rgb(0xff, 0, 0);
        }
}

static int iot_nw_wifi_sta_handler(IOT_NetworkIface nwkInterface,
                              void *arg,
                              IOT_NwkEventStatus_t eventType,
                              void *eventData)
{
    switch (eventType)
    {
    case IOT_NWK_EVENT_STATUS_DISCONNECTED:
    {
        ESP_LOGW(TAG, " Wifi is disconnected");
        if(rpa_is_connected)
        {
            rpa_is_connected = false;
            rpa_test_led_rgb(0xff, 0, 0);
        }

        break;
    }
    case IOT_NWK_EVENT_STATUS_CONNECTED:
    {
        IOT_NwkEventConnectedData_t ipInfo;
        iot_err_t err = IOT_NwkGetIpInfo(nwkInterface, &ipInfo);
        if(err == IOT_OK)
        {
            char ip_str[16];
            sprintf((char *)ip_str,
                    "%d.%d.%d.%d",
                    (int)((ipInfo.ip) & 0xff),
                    (int)((ipInfo.ip >> 8) & 0xff),
                    (int)((ipInfo.ip >> 16) & 0xff),
                    (int)((ipInfo.ip >> 24) & 0xff)
                );
            printf("ip: %s\n", ip_str);
        }

        rssi_test = IOT_NwkGetRssi(nwkInterface);
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s, RSSI: %d", "", "", rssi_test);
        if(!rpa_is_connected)
        {
            rpa_is_connected = true;
            rpa_test_led_rgb(0, 0, 0xff);
        }
        // udp_init_socket();
        break;
    } 
    default:
        break;
    }
    return 0;
}

static void rpa_init_gpio(void){
    gpio_config_t gpio_conf_input = 
    {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << RPA_INPUT_PIN),
        // disable pull
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&gpio_conf_input);

    gpio_config_t gpio_conf_output1 = {
        .pin_bit_mask = 1ULL << TDS1_PULSE1_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_conf_output1);

    gpio_config_t gpio_conf_output2 = {
        .pin_bit_mask = 1ULL << TDS1_PULSE2_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_conf_output2);

    gpio_config_t gpio_conf_output3 = {
        .pin_bit_mask = 1ULL << TDS2_PULSE1_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_conf_output3);

    gpio_config_t gpio_conf_output4 = {
        .pin_bit_mask = 1ULL << TDS2_PULSE2_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_conf_output4);

}

void rpa_set_pin_output1(uint8_t state)
{
    // gpio_set_level(RPA_OUTPUT_PIN1, state);
}
void rpa_set_pin_output2(uint8_t state)
{
    // gpio_set_level(RPA_OUTPUT_PIN2, state);
}

esp_err_t rpa_init_wifi_default(void){
    rpa_init_gpio();
    // led_mgmt_init();
    adc_oneshot_init();
    IOT_NetworkIface ifaces= {.type = WIFI, .number = 0};
    // iot_err_t err = IOT_NwkInit(&ifaces);
    // if (err != IOT_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to init network manager");
    // }
    iot_err_t err = IOT_NwkRegisterEventCb(ifaces, IOT_NWK_EVENT_STATUS_CONNECTED, iot_nw_wifi_sta_handler, NULL);  
    err = IOT_NwkConnectWifi(&ifaces, RPA_WIFI_SSID, RPA_WIFI_PASS, MAX_RETRY_CONNECT);
    // udp_init_socket();

    // vTaskDelay(pdMS_TO_TICKS(5000));
    // int test1, test2;
    // rpa_read_adc(&test1, &test2);
    return (err == IOT_OK) ? ESP_OK : ESP_FAIL;
}

void rd_stop_wifi(void){
    // IOT_WifiDisconnect(0);
}

int8_t rd_get_wifi_rssi_test(void){
    return rssi_test;
}


static adc_oneshot_unit_handle_t adc1_handle;
static void adc_oneshot_init(void)
{
    //-------------ADC1 Init---------------//
    
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHANNEL2, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHANNEL3, &config));

    gpio_set_level(TDS1_PULSE1_PIN, 1);
    gpio_set_level(TDS1_PULSE2_PIN, 0);
    gpio_set_level(TDS2_PULSE1_PIN, 1);
    gpio_set_level(TDS2_PULSE2_PIN, 0);
}

void rpa_read_adc(int *value1, int *value2){
    int value_temp = 0; int value_temp2 = 0;
    int raw_data1 = 0; int raw_data2 = 0;
    for (size_t i = 0; i < MAX_ADC_SAMPLE; i++)
    {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHANNEL2, &raw_data1));
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHANNEL3, &raw_data2));
        // ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHANNEL2, raw_data1);
        value_temp += raw_data1;
        value_temp2 += raw_data2;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    *value1 = value_temp / MAX_ADC_SAMPLE;
    *value2 = value_temp2 / MAX_ADC_SAMPLE;
    printf("value1: %d, value2: %d \n", *value1, *value2);
}

// INPUT
static void task_check_input(void *param)
{   uint8_t old_state = 0;
    uint8_t new_state = 0;
    uint8_t tick = 0;
    while (1)
    {
        if(old_state != new_state)
        {
            test_input_send_msg(new_state);
            if(new_state)
            {
                rpa_test_led_rgb(0, 0xff, 0);
            }else{
                rpa_test_led_rgb(0xff, 0, 0);
            }
            old_state = new_state;
            tick = 0;
        }else{
            tick++;
            if(tick == 5){
                new_state = gpio_get_level(RPA_INPUT_PIN);
                tick = 0;
            } 
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

esp_err_t rpa_init_input_task(void)
{
    if(!xTask_check_input)
        if(xTaskCreate(task_check_input, "task_check_input", 2048*2 , NULL, 10, &xTask_check_input) != pdPASS)
        {
            return ESP_FAIL;
        }
    return ESP_OK;
}

esp_err_t rpa_deinit_input_task(void)
{
    if(xTask_check_input)
    {
        vTaskDelete(xTask_check_input);
        xTask_check_input = NULL;
    }
    return ESP_OK;
}


// OUTPUT
static void task_control(void *param)
{
    uint8_t state1 = 0;
    uint8_t state2 = 0;
    while (1)
    {
        rpa_set_pin_output1(state1); state1 = !state1;
        rpa_set_pin_output2(state2); state2 = !state2;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

esp_err_t rpa_init_output_task(void)
{
    if(!xTask_set_output)
        if(xTaskCreate(task_control, "task_control", 2048 , NULL, 10, &xTask_set_output) != pdPASS)
        {
            return ESP_FAIL;
        }
    return ESP_OK;
}

esp_err_t rpa_deinit_output_task(void)
{
    if(xTask_set_output)
    {
        vTaskDelete(xTask_set_output);
        xTask_set_output = NULL;
    }
    return ESP_OK;
}



// LC8823

#define LED_DATA         8   
#define LED_CLK          21
#define FREQ_LED 25000

#define OFF_STATE    0
#define ON_STATE     1

// static led_t led_obj;
// static void led_mgmt_init(void)
// {
//     lc8823_config_t lc8823_config = {
//         .clk_pin = LED_CLK,
//         .data_pin = LED_DATA,
//         .freq = FREQ_LED};
//     led_obj.index = 0;
//     led_obj.element = 0;
//     led_obj.state = ON_STATE;
//     lc8823_init(&lc8823_config);
//     lc8823_init_frame_buffer(1);
//     lc8823_load_value(&led_obj, ON_STATE, 50, 0x00, 0xFF, 0xB2);
//     lc8823_load_value(&led_obj, OFF_STATE, 20, 0x00, 0xFF, 0xB2);
//     lc8823_update_frame_buffer(0, (led_obj.state) ? &led_obj.value.ON : &led_obj.value.OFF);    
//     lc8823_send_frame_buffer();
// }


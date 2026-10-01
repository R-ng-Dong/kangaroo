#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "esp_adc/adc_continuous.h"


const static char *TAG = "ADC";

#define ADC_GET_CHANNEL(p_data)     ((p_data)->type2.channel) 
#define ADC_GET_DATA(p_data)        ((p_data)->type2.data)

#define ADC_CONV_MODE          ADC_CONV_SINGLE_UNIT_1 // ADC mode
#define ADC_OUTPUT_TYPE        ADC_DIGI_OUTPUT_FORMAT_TYPE2 
#define ADC_RESULT_BYTE_SIZE   SOC_ADC_DIGI_RESULT_BYTES // 4byte

#define ADC_UNIT               ADC_UNIT_1   

#define MAX_STORE_BUFF_SIZE    4096
#define CONV_FRAME_SIZE        256

static adc_continuous_handle_t s_handle = NULL;

esp_err_t AdcContinousInit(adc_channel_t *channel, uint8_t channel_num, uint32_t max_buff_size, uint32_t conv_frame_size, uint32_t freq_hz)
{
    if(channel == NULL || channel_num == 0) 
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = max_buff_size, 
        .conv_frame_size = conv_frame_size, 
    };
    err = adc_continuous_new_handle(&adc_config, &s_handle);
    if(err != ESP_OK)
    {
        return err;
    }

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    for (int i = 0; i < channel_num; i++) {
        adc_pattern[i].atten = ADC_ATTEN_DB_12;
        adc_pattern[i].channel = channel[i] & 0x07;
        adc_pattern[i].unit = ADC_UNIT;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

        ESP_LOGI(TAG, "adc_pattern[%d].atten is :%"PRIx8, i, adc_pattern[i].atten);
        ESP_LOGI(TAG, "adc_pattern[%d].channel is :%"PRIx8, i, adc_pattern[i].channel);
        ESP_LOGI(TAG, "adc_pattern[%d].unit is :%"PRIx8, i, adc_pattern[i].unit);
    }

    adc_continuous_config_t con_cfg = {
        .sample_freq_hz = freq_hz,
        .conv_mode = ADC_CONV_MODE,
        .format = ADC_OUTPUT_TYPE,
        .pattern_num = channel_num,
        .adc_pattern = adc_pattern,
    }; 

    err = adc_continuous_config(s_handle, &con_cfg);
    if(err != ESP_OK)
    {
        return err;
    }
    return err;
}


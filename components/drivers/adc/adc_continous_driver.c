#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "esp_adc/adc_continuous.h"
#include "driver/adc_types_legacy.h"
#include <inttypes.h>
#include "adc_driver.h"

const static char *TAG = "ADC";

#define ADC_GET_CHANNEL(p_data) ((p_data)->type2.channel)
#define ADC_GET_DATA(p_data) ((p_data)->type2.data)

#define ADC_CONV_MODE           ADC_CONV_SINGLE_UNIT_1 // ADC mode
#define ADC_OUTPUT_TYPE         ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define ADC_RESULT_BYTE_SIZE    SOC_ADC_DIGI_RESULT_BYTES // 4byte

#define ADC_UNIT ADC_UNIT_1

#define MAX_STORE_BUFF_SIZE 4096
#define CONV_FRAME_SIZE 256

static adc_continuous_handle_t s_handle = NULL;
static TaskHandle_t s_task_handle;

// static volatile UI32 s_adc_pool_ovf_count = 0;
// static bool IRAM_ATTR adc_pool_ovf_cb( adc_continuous_handle_t handle,const adc_continuous_evt_data_t *edata, void *user_data)
// {
//     s_adc_pool_ovf_count++;
//     return false;
// }

static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    if (NULL != s_task_handle) {
        // Notify that ADC continuous driver has done enough number of conversions
        vTaskNotifyGiveFromISR(s_task_handle, &mustYield);
    }

    return (mustYield == pdTRUE);
}

esp_err_t AdcContinousInit(AdcChannel *channel, UI8 channel_num, UI32 max_buff_size, UI32 conv_frame_size, UI32 freq_hz)
{
    if (NULL == channel || 0 == channel_num)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;

    s_task_handle = NULL;
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = max_buff_size,
        .conv_frame_size = conv_frame_size,
    };
    err = adc_continuous_new_handle(&adc_config, &s_handle);
    if (err != ESP_OK)
    {
        return err;
    }

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    for (int i = 0; i < channel_num; i++)
    {
        adc_pattern[i].atten = ADC_ATTEN_DB_12;
        adc_pattern[i].channel = (adc_channel_t)channel[i] & 0x07;
        adc_pattern[i].unit = ADC_UNIT;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

        ESP_LOGI(TAG, "adc_pattern[%d].atten is :%" PRIx8, i, adc_pattern[i].atten);
        ESP_LOGI(TAG, "adc_pattern[%d].channel is :%" PRIx8, i, adc_pattern[i].channel);
        ESP_LOGI(TAG, "adc_pattern[%d].unit is :%" PRIx8, i, adc_pattern[i].unit);
    }

    adc_continuous_config_t con_cfg = {
        .sample_freq_hz = freq_hz,
        .conv_mode = ADC_CONV_MODE,
        .format = ADC_OUTPUT_TYPE,
        .pattern_num = channel_num,
        .adc_pattern = adc_pattern,
    };

    err = adc_continuous_config(s_handle, &con_cfg);
    if (err != ESP_OK)
    {
        return err;
    }
    adc_continuous_evt_cbs_t cbs = {
        // .on_pool_ovf = adc_pool_ovf_cb,
        .on_conv_done = s_conv_done_cb,
    };
    err = adc_continuous_register_event_callbacks(s_handle, &cbs, NULL);
    ESP_ERROR_CHECK(adc_continuous_start(s_handle));
    return err;
}

esp_err_t AdcReadRawData(UI8 *result, UI16 length, UI32 *ret_num)
{
    if (result == NULL || length == 0 || ret_num == NULL)
        return ESP_ERR_INVALID_ARG;
    s_task_handle = xTaskGetCurrentTaskHandle(); 
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    esp_err_t ret = adc_continuous_read(s_handle, result, length, ret_num, 0);
    s_task_handle = NULL; 
    if (ret == ESP_OK)
    {
        // ESP_LOGI(TAG, "ret_num is %" PRIu32 " bytes", ret_num);
        for (int i = 0; i < ret_num; i += ADC_RESULT_BYTE_SIZE)
        {
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)&result[i];
            UI32 chan_num = ADC_GET_CHANNEL(p);
            UI32 data = ADC_GET_DATA(p);

            if (chan_num < SOC_ADC_CHANNEL_NUM(ADC_UNIT_1))
            {
                ESP_LOGI(TAG, "Channel: %" PRIu32 ", Value: %" PRIu32, chan_num, data);
            }
            else
            {
                ESP_LOGW(TAG, "Invalid data [_%" PRIu32 "_%" PRIu32 "]", chan_num, data);
            }
        }
        // vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "No data, increase timeout or reduce conv_num_each_intr");
   
    }
    return ret;
}

esp_err_t AdcContinousDeinit(void)
{
    if (adc_continuous_stop(s_handle) == ESP_OK)
    {
        return adc_continuous_deinit(s_handle);
    }
    return ESP_FAIL;
}

esp_err_t AdcContinousFlushPool(void)
{
    return adc_continuous_flush_pool(s_handle);
}
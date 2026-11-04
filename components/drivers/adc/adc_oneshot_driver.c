#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "adc_driver.h"

const static char *TAG = "ADC_ONESHOT";

static adc_oneshot_unit_handle_t adc1_handle;

esp_err_t AdcOneshotInit(AdcChannel channel, AdcAtten atten, AdcBitwith bitwith)
{
    esp_err_t err = ESP_OK;
    if (DRV_ADC_CHANNEL_10 < channel) {
        ESP_LOGE(TAG, "Invalid ADC channel");
        return ESP_ERR_INVALID_ARG;
    }
    if (DRV_ADC_ATTEN_DB_12 < atten) {
        ESP_LOGE(TAG, "Invalid ADC attenuation");
        return ESP_ERR_INVALID_ARG;
    }
    if (DRV_ADC_BITWIDTH_13 < bitwith) {
        ESP_LOGE(TAG, "Invalid ADC bit width");
        return ESP_ERR_INVALID_ARG;
    }
    if(!adc1_handle)
    {
        adc_oneshot_unit_init_cfg_t init_config1 = {
            .unit_id = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        err = adc_oneshot_new_unit(&init_config1, &adc1_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize ADC1");
            return err;
        }
    }
    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = (adc_atten_t)atten,
        .bitwidth = (adc_bitwidth_t)bitwith,
    };
    err = adc_oneshot_config_channel(adc1_handle, (adc_channel_t)channel, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC1 channel");
        return err;
    }
    return ESP_OK;
}

esp_err_t AdcOneshotRead(AdcChannel channel, UI16 *raw_data)
{
    if (NULL == raw_data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (NULL == adc1_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    /* ESP-IDF writes an int; passing the caller's UI16 buffer would overwrite memory. */
    int raw_value = 0;
    esp_err_t err = adc_oneshot_read(adc1_handle, (adc_channel_t)channel, &raw_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC1 channel");
        return err;
    }
    if (0 > raw_value || UINT16_MAX < raw_value) {
        return ESP_FAIL;
    }
    *raw_data = (UI16)raw_value;
    return ESP_OK;
}

esp_err_t AdcOneshotDeinit(void)
{
    if (adc1_handle == NULL) {
        return ESP_OK;
    }

    esp_err_t err = adc_oneshot_del_unit(adc1_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize ADC1");
        return err;
    }
    adc1_handle = NULL;
    return ESP_OK;
}

esp_err_t AdcOneshotReconfigure(AdcChannel channel, AdcAtten atten, AdcBitwith bitwith)
{
    adc_oneshot_chan_cfg_t config = {
        .atten = (adc_atten_t)atten,
        .bitwidth = (adc_bitwidth_t)bitwith,
    };
    esp_err_t err = adc_oneshot_config_channel(adc1_handle, (adc_channel_t)channel, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure ADC1 channel");
        return err;
    }
    return ESP_OK;
}



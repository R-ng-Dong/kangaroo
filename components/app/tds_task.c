#include "tds_task.h"

#include "tds_manager.h"
#include "tds.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TDS_TASK_PERIOD_MS 5000U

static const char *TAG = "TDS_TASK";

static void TdsPollingTask(void *arg)
{
    while (1)
    {
        UI16 inlet_ppm = TdsGetValue(TDS_SENSOR_0);
        UI16 ro_ppm = TdsGetValue(TDS_SENSOR_1);
        UI32 now_ms = (UI32)(esp_timer_get_time() / 1000ULL);

        TdsManagerHandleSample(inlet_ppm, ro_ppm, now_ms);
        TdsManagerGetEfficiency(inlet_ppm, ro_ppm);

        ESP_LOGI(TAG,
                 "TDS sample: inlet=%u ppm, ro=%u ppm, now=%lu ms",
                 inlet_ppm,
                 ro_ppm,
                 (unsigned long)now_ms);

        vTaskDelay(pdMS_TO_TICKS(TDS_TASK_PERIOD_MS));
    }
}

void TdsTaskStart(void)
{
    // esp_err_t err = TdsInit(GPIO_NUM_10, GPIO_NUM_0, DRV_ADC_CHANNEL_3, TDS_SENSOR_0);
    // if (err != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to initialize TDS sensor 1: %s", esp_err_to_name(err));
    //     return;
    // }
    esp_err_t err = TdsInit(GPIO_NUM_5, GPIO_NUM_4, DRV_ADC_CHANNEL_2, TDS_SENSOR_1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TDS sensor 2: %s", esp_err_to_name(err));
        return;
    }
    BaseType_t ret = xTaskCreate(TdsPollingTask, "tds_poll", 4096, NULL, 5, NULL);

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create TDS polling task");
    }
}
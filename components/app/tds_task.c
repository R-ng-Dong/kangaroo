#include "tds_task.h"

#include "tds_manager.h"
#include "tds.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TDS_TASK_PERIOD_MS 5000U
#define TDS_WARNING_CHECK_INTERVAL_MS 120000U

static const char *TAG = "TDS_TASK";

TaskHandle_t s_tds_task_handle = NULL;

static void TdsPollingTask(void *arg)
{
    // UI64 inlet_sum = 0U;
    // UI64 ro_sum = 0U;
    // UI32 sample_count = 0U;
    // UI32 warning_window_start_ms = 0U;
    // bool warning_window_started = false;
    ESP_LOGW(TAG, "===>>> TdsPollingTask created sucessfully");

    while (1)
    {
        UI16 inlet_ppm = TdsGetValue(TDS_SENSOR_0);
        UI16 ro_ppm = TdsGetValue(TDS_SENSOR_1);
        UI32 now_ms = (UI32)(esp_timer_get_time() / 1000ULL);

        TdsManagerHandleSample(inlet_ppm, ro_ppm, now_ms);
        TdsManagerCheckWarnings(inlet_ppm, ro_ppm);
#if 0        
        if (!warning_window_started)
        {
            warning_window_start_ms = now_ms;
            warning_window_started = true;
        }

        inlet_sum += inlet_ppm;
        ro_sum += ro_ppm;
        sample_count++;

        if ((now_ms - warning_window_start_ms) >= TDS_WARNING_CHECK_INTERVAL_MS && sample_count > 0U)
        {
            UI16 inlet_average = (UI16)(inlet_sum / sample_count);
            UI16 ro_average = (UI16)(ro_sum / sample_count);
            TdsManagerCheckWarnings(inlet_average, ro_average);
            ESP_LOGI(TAG,
                    "[] TDS sample: inlet=%u ppm, ro=%u ppm",
                    inlet_average,
                    ro_average
                    );

            inlet_sum = 0U;
            ro_sum = 0U;
            sample_count = 0U;
            warning_window_start_ms = now_ms;
        }
#endif
        // TdsManagerGetEfficiency(inlet_ppm, ro_ppm);

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
    TdsPinConfig config[2] =
    {
        {.pulse1_pin = GPIO_NUM_0, .pulse2_pin = GPIO_NUM_10, .adc_channel = DRV_ADC_CHANNEL_3, .sensor_id = TDS_SENSOR_0},
        {.pulse1_pin = GPIO_NUM_4, .pulse2_pin = GPIO_NUM_5, .adc_channel = DRV_ADC_CHANNEL_2, .sensor_id = TDS_SENSOR_1}
    };

    if (s_tds_task_handle != NULL)
    {
        return;
    }

    esp_err_t err = TdsInit(config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TDS : %s", esp_err_to_name(err));
        return;
    }

    BaseType_t ret = xTaskCreate(TdsPollingTask, "tds_poll", 2048*2, NULL, 5, &s_tds_task_handle);

    if (ret != pdPASS)
    {
        s_tds_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create TDS polling task");
        TdsTaskDeinit();
    }
}

void TdsTaskStop(void)
{
    if(s_tds_task_handle)
    {
        vTaskDelete(s_tds_task_handle);
        s_tds_task_handle = NULL;
    }
    TdsTaskDeinit();
}

void TdsTaskContinue(void)
{
    if (s_tds_task_handle != NULL)
    {
        return;
    }

    esp_err_t err = TdsTaskResume();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to resume TDS driver: %s", esp_err_to_name(err));
        return;
    }

    BaseType_t ret = xTaskCreate(TdsPollingTask, "tds_poll", 2048 * 2, NULL, 5,
                                 &s_tds_task_handle);
    if (ret != pdPASS)
    {
        s_tds_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create TDS polling task");
        TdsTaskDeinit();
    }
}


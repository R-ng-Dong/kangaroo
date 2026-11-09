#include "tds_task.h"

#include "tds_manager.h"
#include "tds.h"
#include "water_profile.h"
#include "pump.h"
#include "app.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "utils.h"
#include "flush_cycle_state.h"

#define TDS_TASK_PERIOD_MS 1000U
#define FLUSH_TIME_UPDATE_INTERVAL_S (1 * 60U)
#define TDS_WARNING_CHECK_INTERVAL_MS 120000U
#define MAX_NUM_DETECT_WARNING (TDS_WARNING_CHECK_INTERVAL_MS / TDS_TASK_PERIOD_MS)

#define TDS_AVERAGE_SAMPLE_COUNT 20
#define TDS_TRIM_EDGES 2
#define TDS_MIN_VALID_PPM 3

static const char *TAG = "TDS_TASK";

TaskHandle_t s_tds_task_handle = NULL;
static volatile _bool s_tds_poll_stop_requested = D_FALSE;
static _bool TdsIgnoreValue(void);
static void TdsUpdateFlushTimes(UI32 decrement_s);
static UI16 s_inlet_average_ppm = 0;
static UI16 s_ro_average_ppm = 0;

UI16 TdsInletGet(void)
{
    return s_inlet_average_ppm;
}

UI16 TdsRoGet(void)
{
    return s_ro_average_ppm;
}

static void TdsPollingTask(void *arg)
{
    UI16 inlet_ppm = 0;
    UI16 ro_ppm = 0;
    UI16 inlet_samples[TDS_AVERAGE_SAMPLE_COUNT] = {0};
    UI16 ro_samples[TDS_AVERAGE_SAMPLE_COUNT] = {0};
    UI8 inlet_count = 0;
    UI8 ro_count = 0;

    int64_t last_flush_time_update_us = esp_timer_get_time();

    ESP_LOGW(TAG, "===>>> TdsPollingTask created sucessfully");

    while (!s_tds_poll_stop_requested)
    {
        int64_t now_us = esp_timer_get_time();
        UI32 elapsed_s = (UI32)((now_us - last_flush_time_update_us) / 1000000LL);

        if(elapsed_s >= FLUSH_TIME_UPDATE_INTERVAL_S)
        {
            TdsUpdateFlushTimes(elapsed_s);
            last_flush_time_update_us = esp_timer_get_time();
        }

        if(TdsIgnoreValue())
        {
            ESP_LOGW(TAG, "ignore TDS value");
            inlet_count = 0; ro_count = 0;
            TdsManagerClearAllWarnings();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TDS_TASK_PERIOD_MS));
            continue;
        }
        inlet_ppm = TdsGetValue(TDS_SENSOR_0);
        ro_ppm = TdsGetValue(TDS_SENSOR_1);

        if (inlet_ppm >= TDS_MIN_VALID_PPM && inlet_count < TDS_AVERAGE_SAMPLE_COUNT)
        {
            inlet_samples[inlet_count++] = inlet_ppm;
        }
        if (ro_ppm >= TDS_MIN_VALID_PPM && ro_count < TDS_AVERAGE_SAMPLE_COUNT)
        {
            ro_samples[ro_count++] = ro_ppm;
        }

        _bool average_updated = D_FALSE;
        if (inlet_count == TDS_AVERAGE_SAMPLE_COUNT)
        {
            UI32 inlet_sum = 0;
            MergeSort(inlet_samples, 0, TDS_AVERAGE_SAMPLE_COUNT - 1);
            for (UI8 i = TDS_TRIM_EDGES; i < TDS_AVERAGE_SAMPLE_COUNT - TDS_TRIM_EDGES; i++)
            {
                inlet_sum += inlet_samples[i];
            }
            s_inlet_average_ppm = (UI16)(inlet_sum / (TDS_AVERAGE_SAMPLE_COUNT - 2 * TDS_TRIM_EDGES));
            inlet_count = 0;

            average_updated = D_TRUE;
        }

        if (ro_count == TDS_AVERAGE_SAMPLE_COUNT)
        {
            UI32 ro_sum = 0;
            for (UI8 i = TDS_TRIM_EDGES; i < TDS_AVERAGE_SAMPLE_COUNT - TDS_TRIM_EDGES; i++)
            {
                ro_sum += ro_samples[i];
            }
            s_ro_average_ppm = (UI16)(ro_sum / (TDS_AVERAGE_SAMPLE_COUNT - 2 * TDS_TRIM_EDGES));
            ro_count = 0;

            average_updated = D_TRUE;
        }

        UI32 now_ms = (UI32)(esp_timer_get_time() / 1000ULL);

        // Reporting/state updates use the latest finalized inlet and RO averages.
        if (average_updated)
        {
            TdsManagerHandleSample(s_inlet_average_ppm, s_ro_average_ppm, now_ms);
            ESP_LOGW(TAG,
                     "TDS average updated: inlet=%u ppm, ro=%u ppm, now=%lu ms",
                     s_inlet_average_ppm,
                     s_ro_average_ppm,
                     (unsigned long)now_ms);
        }

        TdsManagerCheckWarnings(s_inlet_average_ppm, s_ro_average_ppm, MAX_NUM_DETECT_WARNING);

        // ESP_LOGI(TAG,
        //          "TDS collecting valid samples: inlet=%u/%u, ro=%u/%u (raw %u/%u ppm)",
        //          inlet_count,
        //          TDS_AVERAGE_SAMPLE_COUNT,
        //          ro_count,
        //          TDS_AVERAGE_SAMPLE_COUNT,
        //          inlet_ppm,
        //          ro_ppm);

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TDS_TASK_PERIOD_MS));
    }

    // Stop the sensor driver only after this task has finished any report or
    // manager operation that may hold application/core locks.
    TdsTaskDeinit();
    s_tds_poll_stop_requested = D_FALSE;
    s_tds_task_handle = NULL;
    vTaskDelete(NULL);
}

static _bool TdsIgnoreValue(void)
{
    WaterPurifierType model =  AppGetModel();
    if(WATER_PURIFIER_TYPE_HOT_COLD == model)
    {
        if(HasMessageType1() && WaterProfileGetStatus(RO_STATUS) != RO_STATUS_PUMP_STOPPED)
        {
            return D_TRUE;
        }
    }else if(WATER_PURIFIER_TYPE_STANDARD == model)
    {
        if(PumpIsActive())
        {
            return D_TRUE;
        }
    }

    FlushCycleState_t state_out;
    _bool ret = FlushCycleStateGet(&state_out);
    if(ret == D_TRUE)
    {
        if(!state_out.drain_done || !state_out.post_filter_flush_done)
        {
            printf("[state] drain_done/post_filter_flush_done: %d/%d \n", state_out.drain_done, state_out.post_filter_flush_done);
            return D_TRUE;
        }
    }
    return D_FALSE;
}

static void TdsUpdateFlushTimes(UI32 decrement_s)
{
    FlushCycleState_t state;
    if (FlushCycleStateGet(&state) != D_TRUE)
    {
        return;
    }

    if (state.drain_time_s > 0U)
    {
        UI32 time_s = (state.drain_time_s > decrement_s)
                          ? state.drain_time_s - decrement_s
                          : 0U;
        (void)FlushCycleStateSetDrainTime(time_s);
    }
    if (state.post_filter_flush_time_s > 0U)
    {
        UI32 time_s = (state.post_filter_flush_time_s > decrement_s)
                          ? state.post_filter_flush_time_s - decrement_s
                          : 0U;
        (void)FlushCycleStateSetPostFilterFlushTime(time_s);
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

    s_tds_poll_stop_requested = D_FALSE;

    esp_err_t err = TdsInit(config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TDS : %s", esp_err_to_name(err));
        return;
    }

    BaseType_t ret = xTaskCreate(TdsPollingTask, "tds_poll", 2048 * 3, NULL, 5, &s_tds_task_handle);

    if (ret != pdPASS)
    {
        s_tds_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create TDS polling task");
        TdsTaskDeinit();
    }
}

void TdsTaskStop(void)
{
    printf("===>>> TDS task stop\n");
    if(s_tds_task_handle)
    {
        s_tds_poll_stop_requested = D_TRUE;
        xTaskNotifyGive(s_tds_task_handle);
        while (s_tds_task_handle != NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return;
    }
    TdsTaskDeinit();
}

void TdsTaskContinue(void)
{
    printf("===>>> TDS task continue\n");
    if (s_tds_task_handle != NULL)
    {
        return;
    }

    s_tds_poll_stop_requested = D_FALSE;

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


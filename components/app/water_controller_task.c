#include "water_controller_task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define WATER_CONTROL_QUEUE_LENGTH 8
#define WATER_CONTROL_RETRY_LIMIT 3
#define WATER_CONTROL_FRAME_TIMEOUT_MS 1500

static const char *TAG = "WATER_CONTROL_TASK";
static QueueHandle_t s_water_control_queue;

__attribute__((weak)) void AppReportHotTankTemperatureToCloud(const UI8 temperature)
{
    printf("[Dummy] AppReportHotTankTemperatureToCloud %d\n", temperature);
}

__attribute__((weak)) void AppReportColdTankTemperatureToCloud(const UI8 temperature)
{
    printf("[Dummy] AppReportColdTankTemperatureToCloud %d\n", temperature);
}


static esp_err_t WaterControllerTaskSend(const WaterControlRequest_t *request)
{
    switch (request->command)
    {
    case WATER_CONTROL_SEND_FEATURE_HOT:
        return SetFeatureEnable(HOT_FEATURE,
                                (UI8)request->value1, (UI8)request->value2);
    case WATER_CONTROL_SEND_FEATURE_COLD:
        return SetFeatureEnable(COLD_FEATURE,
                                (UI8)request->value1, (UI8)request->value2);
    case WATER_CONTROL_SEND_FILTER:
        return SetTimeFilter((WaterPurifyFilter)request->value1, (UI16)request->value2);
    case WATER_CONTROL_SEND_RO_FLUSHING:
        return SetTimeFilter(RO_FLUSHING_START, (UI16)request->value1);;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static UI32 WaterControllerTaskGetSequence(WaterControlVerify_t verify)
{
    (void)verify;
    return WaterProfileGetFrameType1Sequence();
}

static _bool WaterControllerTaskIsConfirmed(const WaterControlRequest_t *request)
{
    _bool confirmed = D_TRUE;

    switch (request->verify1)
    {
    case WATER_CONTROL_VERIFY_SETTING_BIT:
    {
        const UI8 setting_bit = (request->command == WATER_CONTROL_SEND_FEATURE_HOT)
                                    ? HOT_SWITCH_BIT
                                    : COLD_SWITCH_BIT;
        confirmed = ((WaterProfileGetSetting() & setting_bit) == request->expected1);
        printf("confirm %d setting bit: %02x - %02x\n", confirmed, setting_bit, (UI8)request->expected1);
        break;
    }
    case WATER_CONTROL_VERIFY_RO_STATUS:
    {
        confirmed = WaterProfileGetStatus(RO_STATUS) == request->expected1 ? D_TRUE : D_FALSE;
        printf("ro_verify : %d, request->expected1: %02x\n", confirmed, (UI8)request->expected1);
        break;
    }
    case WATER_CONTROL_VERIFY_NONE:
        break;
    default:
        break;
    }

    if (confirmed == D_FALSE)
    {
        return D_FALSE;
    }

    switch (request->verify2)
    {
    case WATER_CONTROL_VERIFY_HOT_TEMPERATURE:
    {
        // _bool check = WaterProfileGetHotTargetTemperature() == (UI8)request->expected2;
        // printf("[Hot tank] check %d, target temp: %d - %d\n", check, WaterProfileGetHotTargetTemperature(), (UI8)request->expected2);
        // return check;
        return D_TRUE;
    }
    case WATER_CONTROL_VERIFY_COLD_TEMPERATURE:
    {
        // _bool check = WaterProfileGetColdTargetTemperature() == (UI8)request->expected2;
        // printf("[Cold tank] check %d, target temp: %d - %d\n", check, WaterProfileGetColdTargetTemperature(), (UI8)request->expected2);
        // return check;
        return D_TRUE;
    }
    case WATER_CONTROL_VERIFY_FILTER_TIME:
        return WaterProfileGetFilterRuntimeSeconds((FilterTimeIndex)request->value1) == request->expected2;
    case WATER_CONTROL_VERIFY_NONE:
    default:
        return D_TRUE;
    }
}

static void WaterControllerTaskWaitForFrame(WaterControlVerify_t verify, UI32 previous_sequence)
{
    TickType_t elapsed = 0U;
    const TickType_t timeout = pdMS_TO_TICKS(WATER_CONTROL_FRAME_TIMEOUT_MS);
    const TickType_t poll_delay = pdMS_TO_TICKS(50U);

    while ((WaterControllerTaskGetSequence(verify) == previous_sequence) && (elapsed < timeout))
    {
        TickType_t delay = poll_delay;
        if ((timeout - elapsed) < delay)
        {
            delay = timeout - elapsed;
        }
        vTaskDelay(delay);
        elapsed += delay;
    }
}

static void WaterControllerTask(void *arg)
{
    WaterControlRequest_t request;
    (void)arg;

    while (1)
    {
        if (xQueueReceive(s_water_control_queue, &request, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        UI8 retries = 0U;
        while (1)
        {
            UI32 sequence = WaterControllerTaskGetSequence(request.verify1);
            esp_err_t result = WaterControllerTaskSend(&request);

            if (request.verify1 == WATER_CONTROL_VERIFY_NONE &&
                request.verify2 == WATER_CONTROL_VERIFY_NONE)
            {
                if (result == ESP_OK)
                {
                    ESP_LOGI(TAG, "Command %u sent; no periodic feedback field is available",
                             (unsigned)request.command);
                    break;
                }

                if (retries >= WATER_CONTROL_RETRY_LIMIT)
                {
                    ESP_LOGE(TAG, "Command %u send failed (%s)",
                             (unsigned)request.command, esp_err_to_name(result));
                    break;
                }

                retries++;
                ESP_LOGW(TAG, "Retry command %u (%u/%u)", (unsigned)request.command,
                         retries, WATER_CONTROL_RETRY_LIMIT);
                continue;
            }

            if (result == ESP_OK && (request.verify1 != WATER_CONTROL_VERIFY_NONE ||
                                     request.verify2 != WATER_CONTROL_VERIFY_NONE))
            {
                WaterControllerTaskWaitForFrame(request.verify1, sequence);
            }

            UI8 has_fresh_frame = WaterControllerTaskGetSequence(request.verify1) != sequence;
            if (result == ESP_OK && has_fresh_frame && WaterControllerTaskIsConfirmed(&request))
            {
                ESP_LOGI(TAG, "Command %u observable state confirmed after %u retry(s)",
                         (unsigned)request.command, retries);
                if(request.verify2 == WATER_CONTROL_VERIFY_HOT_TEMPERATURE)
                {
                    AppReportHotTankTemperatureToCloud((UI8)request.value2/2);
                }
                else if(request.verify2 == WATER_CONTROL_VERIFY_COLD_TEMPERATURE)
                {
                    AppReportColdTankTemperatureToCloud((UI8)request.value2/2);
                }
                break;
            }

            if (retries >= WATER_CONTROL_RETRY_LIMIT)
            {
                ESP_LOGE(TAG, "Command %u failed confirmation (send=%s, retries=%u)",
                         (unsigned)request.command, esp_err_to_name(result), retries);
                break;
            }

            retries++;
            ESP_LOGW(TAG, "Retry command %u (%u/%u)", (unsigned)request.command,
                     retries, WATER_CONTROL_RETRY_LIMIT);
        }
    }
}

_bool WaterControllerTaskInit(void)
{
    if (s_water_control_queue != NULL)
    {
        return D_TRUE;
    }

    s_water_control_queue = xQueueCreate(WATER_CONTROL_QUEUE_LENGTH, sizeof(WaterControlRequest_t));
    if (s_water_control_queue == NULL)
    {
        return D_FALSE;
    }

    if (xTaskCreate(WaterControllerTask, "water_control", 3072, NULL, 5, NULL) != pdPASS)
    {
        vQueueDelete(s_water_control_queue);
        s_water_control_queue = NULL;
        return D_FALSE;
    }
    return D_TRUE;
}

_bool WaterControllerTaskEnqueue(const WaterControlRequest_t *request)
{
    if (s_water_control_queue == NULL || request == NULL)
    {
        return D_FALSE;
    }
    return (xQueueSend(s_water_control_queue, request, 0U) == pdTRUE) ? D_TRUE : D_FALSE;
}

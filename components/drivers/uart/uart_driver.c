#include "uart_driver.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "UART";

/*======================================================
                        UART
========================================================*/

static QueueHandle_t s_uart_queue = NULL;

TaskHandle_t xTask_uart_handle = NULL;

static esp_err_t ConfigureUart(const UartConfig uart_config)
{
    esp_err_t esp_err = ESP_OK;
    const uart_config_t config = {
        .baud_rate = uart_config.baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    esp_err = uart_driver_install(uart_config.uart_num, uart_config.txrx_buff_size * 2, uart_config.txrx_buff_size * 2, 20, &s_uart_queue, 0);
    if (esp_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to install uart driver");
        return ESP_FAIL;
    }
    esp_err = uart_param_config(uart_config.uart_num, &config);
    if (esp_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to config param uart");
        return ESP_FAIL;
    }
    esp_err = uart_set_pin(uart_config.uart_num, uart_config.tx_pin, uart_config.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (esp_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set pin uart");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "UART initialized successfully");
    return ESP_OK;
}

esp_err_t UartWriteBytes(uart_port_t port_num, UI8 *data, size_t size)
{
    if (NULL == data || 0 == size)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (uart_write_bytes(port_num, data, size) != -1)
    {
        return uart_wait_tx_done(port_num, 100 / portTICK_PERIOD_MS); // wait max 100ms
    }
    return ESP_FAIL;
}

int UartReadBytes(uart_port_t port_num, UI8 *data, size_t size)
{
    if (NULL == data)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return uart_read_bytes(port_num, data, size, 20 / portTICK_PERIOD_MS);
}

esp_err_t UartClearBuffer(uart_port_t port_num)
{
    return uart_flush_input(port_num);
}

static void UartTask(void *param)
{
    UartConfig *my_uart = (UartConfig *)param;
    uart_event_t event;
    size_t len_buf = 0;
    size_t rx_count = 0;
    SI64 last_time_rec_uart = 0;

    while (1)
    {
        if (xQueueReceive(s_uart_queue, (void *)&event, 5))
        {
            switch (event.type)
            {
            case UART_DATA:
                if (esp_timer_get_time() - last_time_rec_uart > 1000) // 1000us
                {
                    rx_count = 0;
                    // if(my_uart->data_buffer)
                    //     memset(my_uart->data_buffer, 0, MAX_BUFFER_SIZE);
                    if (my_uart->reset_state)
                        my_uart->reset_state();
                }
                last_time_rec_uart = esp_timer_get_time();

                ESP_ERROR_CHECK(uart_get_buffered_data_len(my_uart->uart_num, (size_t *)&len_buf));
                if (my_uart->data_buffer)
                {
                    len_buf = uart_read_bytes(my_uart->uart_num, my_uart->data_buffer + rx_count, len_buf, portMAX_DELAY);
                    rx_count += len_buf;
                }

                if (my_uart->process_data)
                {
                    my_uart->process_data(my_uart->data_buffer, len_buf);
                }

                break;
            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "HW FIFO Overflow\n");
                UartClearBuffer(my_uart->uart_num);
                xQueueReset(s_uart_queue);
                break;

            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "Ring Buffer Full\n");
                UartClearBuffer(my_uart->uart_num);
                xQueueReset(s_uart_queue);
                break;

            case UART_BREAK:
                // ESP_LOGE(TAG,"UART Break\n");
                break;

            case UART_PARITY_ERR:
                ESP_LOGE(TAG, "Parity Error\n");
                break;

            case UART_FRAME_ERR:
                ESP_LOGE(TAG, "Frame Error\n");
                break;

            default:
                ESP_LOGE(TAG, "Other event: %d\n", event.type);
                break;
            }
        }
    }
}

esp_err_t UartInit(const UartConfig *uart_config)
{
    if(NULL == uart_config) return ESP_ERR_INVALID_ARG;
    if (ConfigureUart(*uart_config) == ESP_OK)
    {
        BaseType_t ret = xTaskCreate(UartTask, "UartTask", 2048, (void *)uart_config, configMAX_PRIORITIES - 3, &xTask_uart_handle);
        if (pdPASS != ret)
        {
            ESP_LOGE(TAG, "Failed to create UART task");
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t UartDeinit(uart_port_t port_num)
{
    if (xTask_uart_handle)
    {
        vTaskDelete(xTask_uart_handle);
        xTask_uart_handle = NULL;
    }
    if (s_uart_queue)
    {
        vQueueDelete(s_uart_queue);
        s_uart_queue = NULL;
    }
    return uart_driver_delete(port_num);
}

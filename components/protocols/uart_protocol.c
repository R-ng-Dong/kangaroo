#include "uart_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "KGR_UART";

static UartConfig s_uart_config;
static KgrDataFormat *s_data_frame_temp;
static UartState s_state;
static UI8 s_data_buffer[MAX_BUFFER_SIZE];

static uart_protocol_event_handler_t s_event_handler = NULL;

static void KGR_HandleData(const KgrDataFormat *frame);

void UartProtocolSetEventHandler(uart_protocol_event_handler_t handler)
{
    s_event_handler = handler;
}

static UI8 crc8_cal(const UI8 *idat, UI8 lenx)
{
    UI8 i, xcrc;
    xcrc = 0;
    for (i = 0; i < lenx; i++)
    {
        xcrc += idat[i];
    }
    return xcrc;
}

static esp_err_t CheckHeader(const KgrDataFormat *data_rec)
{
    if (KGR_FRAME_HEADER == data_rec->header)
    {
        // ESP_LOGI(TAG, "check header done");
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t CheckSum(const UI8 *buff, UI8 data_length)
{
    if (NULL == buff)
        return ESP_ERR_INVALID_ARG;
    UI8 check_sum_temp = buff[data_length + 1];
    if (crc8_cal(&buff[0], data_length + 1) == check_sum_temp)
    {
        // ESP_LOGI(TAG, "Checksum is valid");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Checksum failed");
    return ESP_FAIL;
}

static void ProcessData(const UI8 *data_buff, UI8 length)
{
    if (NULL == data_buff)
        return;
    UI8 offset = 0;
    esp_err_t ret = ESP_OK;
    s_state = CHECK_DATA;
    while (length--)
    {
        s_data_frame_temp = (KgrDataFormat *)(data_buff + offset);

        if (CHECK_DATA == s_state)
        {
            ret = CheckHeader(s_data_frame_temp);
            if (ESP_OK != ret)
            {
                offset++;
                continue;
            }
        }
        ret = CheckSum(&data_buff[offset], s_data_frame_temp->length);
        if (ESP_OK == ret)
        {
            s_state = HANDLE_DATA;
            KGR_HandleData(s_data_frame_temp);
            return;
        }
        else
        {
            offset++;
            s_state = CHECK_DATA;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

static void ResetState(void)
{
    s_state = CHECK_DATA;
    memset(s_data_buffer, 0, MAX_BUFFER_SIZE);
}

static void KGR_HandleData(const KgrDataFormat *frame)
{
    if (HANDLE_DATA != s_state || NULL == frame)
        return;

    // ESP_LOGW(TAG, "frame_type: %02x", frame->type);

    if (NULL != s_event_handler)
    {
        s_event_handler(frame);
    }
    ResetState();
}

esp_err_t InitBoardCommunication(const gpio_num_t tx_pin, const gpio_num_t rx_pin, const uart_port_t port_num)
{
    esp_err_t err_code = ESP_OK;
    s_uart_config.tx_pin = tx_pin;
    s_uart_config.rx_pin = rx_pin;
    s_uart_config.uart_num = port_num;
    s_uart_config.baudrate = KGR_BAUDRATE;
    s_uart_config.txrx_buff_size = KGR_TXRX_BUF_SIZE;
    s_uart_config.data_buffer = s_data_buffer;
    s_uart_config.reset_state = ResetState;
    s_uart_config.process_data = ProcessData;

    err_code = UartInit(&s_uart_config);
    if (err_code != ESP_OK)
    {
        return err_code;
    }
    return err_code;
}

/*====================================================
                MCU SEND TO MAIN BOARD
====================================================*/

/*
    1. Frame header 1
    2. Length       1
    3. Type         1
    4  Payload      Length - 2
    5  CRC          1
*/

static esp_err_t KGR_BuildFrame(UI8 type, const UI8 *payload, UI8 payload_length)
{
    if (NULL == payload || 0 == payload_length)
        return ESP_ERR_INVALID_ARG;
    UI8 data_send[32];
    data_send[0] = KGR_FRAME_HEADER;   // Header
    data_send[1] = payload_length + 2; // Length
    data_send[2] = type;
    memcpy(&data_send[3], payload, payload_length);
    UI8 crc = crc8_cal(data_send, payload_length + 3);
    *(data_send + payload_length + 3) = crc;
    printf("-----------------------\n");
    return UartWriteBytes(s_uart_config.uart_num, data_send, payload_length + 4);
}

esp_err_t SendSystemResetCommand(void)
{
    KgrFrameType2 frame_type2;
    frame_type2.command = 0x00;
    frame_type2.data.reset.value = 0x00;
    frame_type2.data.reset.raw[0] = 0x00;
    frame_type2.data.reset.raw[1] = 0x00;
    return KGR_BuildFrame(KGR_DATA_TYPE_2, (UI8 *)(&frame_type2), sizeof(frame_type2));
}

esp_err_t SetFeatureEnable(WaterPurifyFeature feature, UI8 enabled, UI8 value)
{
    if(0x00 != enabled && 0x01 != enabled) 
        return ESP_ERR_INVALID_ARG;
    KgrFrameType2 frame_type2;
    frame_type2.command = 0x01;
    frame_type2.data.control.index = (UI8)feature;
    frame_type2.data.control.value = enabled;
    frame_type2.data.control.value2nd = value;
    return KGR_BuildFrame(KGR_DATA_TYPE_2, (UI8 *)(&frame_type2), sizeof(frame_type2));
}

esp_err_t SetTimeFilter(WaterPurifyFilter filter, UI16 time)
{
    KgrFrameType2 frame_type2;
    frame_type2.command = 0x02;
    frame_type2.data.filter.index = (UI8)filter;
    frame_type2.data.filter.value = time; 
    return KGR_BuildFrame(KGR_DATA_TYPE_2, (UI8 *)(&frame_type2), sizeof(frame_type2));
}

esp_err_t SetTDSValue(UI16 TDS_inlet, UI16 TDS_ro)
{
    KgrFrameType3 frame_type3 = {0};
    frame_type3.tds_value1 = TDS_inlet;
    frame_type3.tds_value2 = TDS_ro;
    return KGR_BuildFrame(KGR_DATA_TYPE_3, (UI8 *)(&frame_type3), sizeof(frame_type3));
}

esp_err_t EnableCommand(UI8 enabled)
{
    if (enabled) enabled = 0x01;
    KgrFrameTypeA frame_typeA = {0};
    frame_typeA.command = 0x00;
    frame_typeA.data.enable.value = enabled;
    return KGR_BuildFrame(KGR_DATA_TYPE_A, (UI8 *)(&frame_typeA), sizeof(frame_typeA));
}

esp_err_t SetOutputControl(UI8 output_idx, UI8 control_value)
{
    if (KGR_MAX_OUTPUT <= output_idx || 0x01 < control_value)
        return ESP_ERR_INVALID_ARG;
    KgrFrameTypeA frame_typeA = {0};
    frame_typeA.command = 0x01;
    frame_typeA.data.output_control.output_index = output_idx;
    frame_typeA.data.output_control.control_value = control_value;
    return KGR_BuildFrame(KGR_DATA_TYPE_A, (UI8 *)(&frame_typeA), sizeof(frame_typeA));
}

esp_err_t SetLedControl(UI8 led_idx, UI8 control_value)
{
    if (KGR_MAX_LED <= led_idx || 0x01 < control_value)
        return ESP_ERR_INVALID_ARG;
    KgrFrameTypeA frame_typeA = {0};
    frame_typeA.command = 0x02;
    frame_typeA.data.led_control.led_index = led_idx;
    frame_typeA.data.led_control.control_value = control_value;
    return KGR_BuildFrame(KGR_DATA_TYPE_A, (UI8 *)(&frame_typeA), sizeof(frame_typeA));
}

esp_err_t SetDeviceId(UI32 dev_id)
{
    UI8 dev_id_arr[4];
    KgrFrameTypeA frame_typeA = {0};
    dev_id_arr[0] = dev_id & 0xff;
    dev_id_arr[1] = (dev_id >> 8) & 0xff;
    dev_id_arr[2] = (dev_id >> 16) & 0xff;
    dev_id_arr[3] = (dev_id >> 24) & 0xff;
    frame_typeA.command = 0x09;
    frame_typeA.data.id_set.device_id_set = *(UI32 *)dev_id_arr;
    return KGR_BuildFrame(KGR_DATA_TYPE_A, (UI8 *)(&frame_typeA), sizeof(frame_typeA));
}


#include "water_profile.h"

#include <string.h>
#include "esp_log.h"
#include <inttypes.h>

const char *TAG = "KGR";

KgrFrameType1 s_frame_type_1; 
KgrExternFrameTypeA s_frame_type_A;

static void WaterProfileHandleUartFrame(const KgrDataFormat *frame); 

void WaterProfileInit(void) {
    InitBoardCommunication(GPIO_NUM_6, GPIO_NUM_7, UART_NUM_1);
    UartProtocolSetEventHandler(WaterProfileHandleUartFrame);
}

uint32_t WaterProfileGetFilterRuntimeSeconds(FilterTimeIndex filter_index) {
    KgrFrameType1 *data = &s_frame_type_1;
    switch (filter_index)
    {
    case FILTER_TIME_INDEX_1ST:
        return data->filter_time_1;
    case FILTER_TIME_INDEX_2ND:
        return data->filter_time_2;
    case FILTER_TIME_INDEX_3RD:
        return data->filter_time_3;
    case FILTER_TIME_INDEX_RO:
        return data->ro_filter_time;
    case FILTER_TIME_INDEX_POST:
        return data->post_filter_time;
    default:
        break;
    }
    return 0;
}

uint8_t WaterProfileGetStatus(WaterPurifyStatusType type) {
    KgrFrameType1 *data = &s_frame_type_1;
    switch (type)
    {
    case RO_STATUS:
        return data->ro_status;
    case PLY_STATUS:
        return data->ply_status;
    case HOT_TANK_STATUS:
        return data->hot_tank_status;
    case COLD_TANK_STATUS:
        return data->cold_tank_status;
    default:
        break;
    }
    return 0;
}

uint8_t WaterProfileGetSetting(void){
    return s_frame_type_1.setting_val;
}

esp_err_t WaterProfileResetSystem(void)
{
    return SendSystemResetCommand();
}

esp_err_t WaterProfileSetFilterRuntimeMinutes(FilterTimeIndex filter_index, uint16_t time_m)
{
    WaterPurifyFilter filter;
    switch (filter_index)
    {
    case FILTER_TIME_INDEX_1ST:
        filter = FIRST_FILTER;
        break;
    case FILTER_TIME_INDEX_2ND:
        filter = SECOND_FILTER;
        break;
    case FILTER_TIME_INDEX_3RD:
        filter = THIRD_FILTER;
        break;
    case FILTER_TIME_INDEX_RO:
        filter = RO_FILTER;
        break;
    case FILTER_TIME_INDEX_POST:
        filter = POST_FILTER;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return SetTimeFilter(filter, time_m);
}

esp_err_t WaterProfileSetFeatureHot(uint8_t enabled, uint8_t temperature){
    return SetFeatureEnable(HOT_FEATURE, enabled, temperature);
}

esp_err_t WaterProfileSetFeatureCold(uint8_t enabled, uint8_t temperature){
    return SetFeatureEnable(COLD_FEATURE, enabled, temperature);
}

esp_err_t WaterProfileSetFeatureEnergySaving(uint8_t enabled){
    return SetFeatureEnable(ENERGY_SAVING_FEATURE, enabled, enabled);
}

esp_err_t WaterProfileSetTDSValue(uint16_t TDS_inlet, uint16_t TDS_ro){
    return SetTDSValue(TDS_inlet, TDS_ro);
}


// FCT test
esp_err_t WaterProfileEnableCommandTest(uint8_t enabled)
{
    return EnableCommand(enabled);
}

esp_err_t WaterProfileSetOutputControl(uint8_t output_idx, uint8_t control_value)
{
    return SetOutputControl(output_idx, control_value);
}

esp_err_t WaterProfileSetLedControl(uint8_t led_idx, uint8_t control_value)
{
    return SetLedControl(led_idx, control_value);
}

esp_err_t WaterProfileSetDeviceId(uint32_t dev_id)
{
    return SetDeviceId(dev_id);
}

uint32_t WaterProfileGetDeviceId(void)
{
    return s_frame_type_A.dev_id;
}

uint8_t WaterProfileGetInputStatus(WaterPurifyInputStatus input_type, uint8_t index)
{
    if(index > 8) return 0;
    KgrExternFrameTypeA *data = &s_frame_type_A;
    switch (input_type)
    {
    case INPUT_STATUS:
        return data->input_status & INPUT_STATUS(index) ? 1 : 0;
    case OUTPUT_STATUS:
        return data->output_status & OUTPUT_STATUS(index) ? 1 : 0;
    case SWITCH_STATUS:
        return data->switch_status & SWITCH_STATUS(index) ? 1 : 0;
    case LED_STATUS:
        return data->led_status & LED_STATUS(index) ? 1 : 0;
    
    default:
        break;
    }
    return 0;
}

static void WaterProfileHandleUartFrame(const KgrDataFormat *frame) {
    if (frame == NULL) {
        return;
    }

    switch (frame->type) {
        case KGR_DATA_TYPE_1: {
            const KgrFrameType1 *data = (const KgrFrameType1 *)frame->data;
            if (NULL == data || frame->length < sizeof(s_frame_type_1)) {
                break;
            }
            memcpy(&s_frame_type_1, data, sizeof(s_frame_type_1));
            ESP_LOGI(TAG, "============================================");
            ESP_LOGI(TAG, "System run: %" PRIu32 "s, System time: %" PRIu32 "s", s_frame_type_1.system_run, s_frame_type_1.system_time);
            ESP_LOGI(TAG, "RO_stt: 0x%02x, PLY_stt: 0x%02x, Hot tank stt: 0x%02x, Cold tank stt: 0x%02x, setting: 0x%02x ",\
                            s_frame_type_1.ro_status, s_frame_type_1.ply_status, s_frame_type_1.hot_tank_status, s_frame_type_1.cold_tank_status, s_frame_type_1.setting_val);
            ESP_LOGI(TAG, "Filter 1: %" PRIu32 "s, Filter 2: %" PRIu32 "s, Filter 3: %" PRIu32 "s\n  \
                           RO_Filter: %" PRIu32 "s, POST_Filter: %" PRIu32 "s ", \
                           s_frame_type_1.filter_time_1, s_frame_type_1.filter_time_2, s_frame_type_1.filter_time_3, s_frame_type_1.ro_filter_time, s_frame_type_1.post_filter_time);
            ESP_LOGI(TAG, "============================================");

            break;
        }
        case KGR_DATA_TYPE_2: {
            // TODO
            break;
        }
        case KGR_DATA_TYPE_A: {
            const KgrExternFrameTypeA *data = (const KgrExternFrameTypeA *)frame->data;
            if (NULL == data) {
                break;
            }
            memcpy(&s_frame_type_A, data, sizeof(s_frame_type_A));
            ESP_LOGI(TAG, "============================================");
            ESP_LOGI(TAG, "DeviceID: %" PRIu32 " Input Status: 0x%02x, Output Status: 0x%02x, Switch Status: 0x%02x, Led Status: 0x%02x", \
                            s_frame_type_A.dev_id, s_frame_type_A.input_status, s_frame_type_A.output_status, s_frame_type_A.switch_status, s_frame_type_A.led_status);
            ESP_LOGI(TAG, "============================================");
            break;
        }
        default:
            break;
    }
}

// static void AppUartDispatch(const KgrDataFormat *frame) {
//     if (frame == NULL) {
//         return;
//     }

//     WaterProfileHandleUartFrame(frame);
// }

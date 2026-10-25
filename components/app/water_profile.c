#include "water_profile.h"

#include "app_context.h"
#include "uart_protocol.h"

#include <inttypes.h>
#include <string.h>

#include "pin_config.h"

#include "esp_log.h"

#define WATER_PROFILE_TAG "KGR_PROFILE"

static KgrFrameType1 s_frame_type_1;
static KgrExternFrameTypeA s_frame_type_A;
static UI32 s_electrolysis_completed_seconds;
static UI16 s_last_ply_time;
static UI8 s_last_ply_status;
static _bool s_has_ply_sample;

static _bool s_has_mess_type1 = D_FALSE;
static _bool s_has_mess_typeA = D_FALSE;
static volatile UI32 s_frame_type_1_sequence;
static volatile UI32 s_frame_type_A_sequence;

static void WaterProfileHandleUartFrame(const KgrDataFormat *frame);
static void WaterProfileApplyFrameType1(const KgrFrameType1 *data);
static void WaterProfileApplyFrameTypeA(const KgrExternFrameTypeA *data);

void WaterProfileInit(void)
{
    memset(&s_frame_type_1, 0, sizeof(s_frame_type_1));
    memset(&s_frame_type_A, 0, sizeof(s_frame_type_A));
    s_electrolysis_completed_seconds = 0U;
    s_last_ply_time = 0U;
    s_last_ply_status = 0U;
    s_has_ply_sample = D_FALSE;
    s_frame_type_1_sequence = 0U;
    s_frame_type_A_sequence = 0U;
    InitBoardCommunication(PIN_UART_TX, PIN_UART_RX, UART_NUM_1);
    WaterProfileRegisterUartCallback();
}

void WaterProfileRegisterUartCallback(void)
{
    UartProtocolSetEventHandler(WaterProfileHandleUartFrame);
}

UI32 WaterProfileGetFilterRuntimeSeconds(FilterTimeIndex filter_index)
{
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
        return 0U;
    }
}

UI16 WaterProfileGetHotTankTemperatureRaw(void)
{
    return s_frame_type_1.hot_tank_temperature;
}

UI16 WaterProfileGetColdTankTemperatureRaw(void)
{
    return s_frame_type_1.cold_tank_temperature;
}

UI8 WaterProfileGetHotTargetTemperature(void)
{
    return s_frame_type_1.hot_target_temperature;
}

UI8 WaterProfileGetColdTargetTemperature(void)
{
    return s_frame_type_1.cold_target_temperature;
}

UI8 WaterProfileGetStatus(WaterPurifyStatusType type)
{
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
        return 0U;
    }
}

SI8 WaterProfileGetHotTankTemperature(void)
{
    SI8 temperature = (s_frame_type_1.hot_tank_temperature / 100) - 50;
    return temperature;
}

SI8 WaterProfileGetColdTankTemperature(void)
{
    SI8 temperature = (s_frame_type_1.cold_tank_temperature / 100) - 50;
    return temperature;
}

UI8 WaterProfileGetSetting(void)
{
    return s_frame_type_1.setting_val;
}

UI32 WaterProfileGetFrameType1Sequence(void)
{
    return s_frame_type_1_sequence;
}

UI32 WaterProfileGetFrameTypeASequence(void)
{
    return s_frame_type_A_sequence;
}

UI32 WaterProfileGetDeviceId(void)
{
    return s_frame_type_A.dev_id;
}

UI8 WaterProfileGetInputStatus(WaterPurifyInputStatus input_type, UI8 index)
{
    /* MLN input indices are one-based and include index 8. */
    if (0U == index || KGR_MAX_INDEX < index)
    {
        return 0U;
    }
    KgrExternFrameTypeA *data = &s_frame_type_A;
    switch (input_type)
    {
    case INPUT_STATUS:
        return (data->input_status & INPUT_STATUS(index)) ? 1U : 0U;
    case OUTPUT_STATUS:
        return (data->output_status & OUTPUT_STATUS(index)) ? 1U : 0U;
    case SWITCH_STATUS:
        return (data->switch_status & SWITCH_STATUS(index)) ? 1U : 0U;
    case LED_STATUS:
        return (data->led_status & LED_STATUS(index)) ? 1U : 0U;
    default:
        return 0U;
    }
}

_bool HasMessageTypeA(void)
{
    return s_has_mess_typeA;
}

_bool HasMessageType1(void)
{
    return s_has_mess_type1;
}

UI32 WaterProfileGetSystemRunSeconds(void)
{
    return s_frame_type_1.system_run;
}

UI32 WaterProfileGetSystemTimeSeconds(void)
{
    return s_frame_type_1.system_time;
}

UI16 WaterProfileGetAdcRaw1(void)
{
    return s_frame_type_A.adc_raw_value_1;
}

UI16 WaterProfileGetAdcRaw2(void)
{
    return s_frame_type_A.adc_raw_value_2;
}
    
UI16 WaterProfileGetVersionFirmware(void)
{
    return s_frame_type_A.fw_build_ver[0] << 8 | s_frame_type_A.fw_build_ver[1];
}

static void WaterProfileApplyFrameType1(const KgrFrameType1 *data)
{
    if (data == NULL)
    {
        return;
    }
    s_has_mess_type1 = D_TRUE;

    memcpy(&s_frame_type_1, data, sizeof(s_frame_type_1));
    s_frame_type_1_sequence++;

    if (s_has_ply_sample && (s_last_ply_status != 0U) && (s_frame_type_1.ply_status == 0U))
    {
        s_electrolysis_completed_seconds += s_last_ply_time;
    }

    s_last_ply_time = s_frame_type_1.ply_time;
    s_last_ply_status = s_frame_type_1.ply_status;
    s_has_ply_sample = D_TRUE;

    WaterProfileState_t state = {0};
    state.ro_status = s_frame_type_1.ro_status;
    state.ply_status = s_frame_type_1.ply_status;
    state.hot_tank_status = s_frame_type_1.hot_tank_status;
    state.cold_tank_status = s_frame_type_1.cold_tank_status;
    state.setting_val = s_frame_type_1.setting_val;
    state.input_status = s_frame_type_A.input_status;
    state.output_status = s_frame_type_A.output_status;
    state.switch_status = s_frame_type_A.switch_status;
    state.led_status = s_frame_type_A.led_status;

    WaterPurifierRuntime_t runtime = {0};
    runtime.total_pump_runtime = s_frame_type_1.total_pump_running_time;
    runtime.total_hot_tank_runtime = s_frame_type_1.total_hot_running_time;
    runtime.total_cold_tank_runtime = s_frame_type_1.total_cold_running_time;
    runtime.total_electrolysis_runtime = s_electrolysis_completed_seconds;
    if (s_frame_type_1.ply_status != 0U)
    {
        runtime.total_electrolysis_runtime += s_frame_type_1.ply_time;
    }

    AppContextSetWaterState(&state);
    AppContextSetRuntime(&runtime);

    SI8 hot_temp = (s_frame_type_1.hot_tank_temperature / 100) - 50;
    SI8 cold_temp = (s_frame_type_1.cold_tank_temperature / 100) - 50;

    #if 0
    ESP_LOGI(WATER_PROFILE_TAG,
             "System run=%" PRIu32 "s, time=%" PRIu32 "s, STATUS [ro=0x%02x, ply=0x%02x, hot=0x%02x, cold=0x%02x, setting=0x%02x], TEMPERATURE [hot: %d *C, cold: %d *C]",
             s_frame_type_1.system_run,
             s_frame_type_1.system_time,
             s_frame_type_1.ro_status,
             s_frame_type_1.ply_status,
             s_frame_type_1.hot_tank_status,
             s_frame_type_1.cold_tank_status,
             s_frame_type_1.setting_val,
             hot_temp, cold_temp);
    ESP_LOGI(WATER_PROFILE_TAG, 
        "[Target Tempterature]: hot_tank: %d, cold_tank: %d", s_frame_type_1.hot_target_temperature, s_frame_type_1.cold_target_temperature);
    ESP_LOGI(WATER_PROFILE_TAG,
             "Filter1=%" PRIu32 "s, Filter2=%" PRIu32 "s, Filter3=%" PRIu32 "s, RO=%" PRIu32 "s, POST=%" PRIu32 "s",
             s_frame_type_1.filter_time_1,
             s_frame_type_1.filter_time_2,
             s_frame_type_1.filter_time_3,
             s_frame_type_1.ro_filter_time,
             s_frame_type_1.post_filter_time);
    #endif
}

static void WaterProfileApplyFrameTypeA(const KgrExternFrameTypeA *data)
{
    if (NULL == data)
    {
        return;
    }
    s_has_mess_typeA = D_TRUE;
    memcpy(&s_frame_type_A, data, sizeof(s_frame_type_A));
    s_frame_type_A_sequence++;


    ESP_LOGI(WATER_PROFILE_TAG,
             "DeviceID=%" PRIu32 ", input=0x%02x, output=0x%02x, switch=0x%02x, led=0x%02x",
             s_frame_type_A.dev_id,
             s_frame_type_A.input_status,
             s_frame_type_A.output_status,
             s_frame_type_A.switch_status,
             s_frame_type_A.led_status);
}

static void WaterProfileHandleUartFrame(const KgrDataFormat *frame)
{
    if (NULL == frame)
    {
        return;
    }

    switch (frame->type)
    {
    case KGR_DATA_TYPE_1:
    {
        const KgrFrameType1 *data = (const KgrFrameType1 *)frame->data;
        if ((NULL == data) || (frame->length < sizeof(KgrFrameType1)))
        {
            ESP_LOGW(WATER_PROFILE_TAG, "Invalid KGR_DATA_TYPE_1 length: %u", frame->length);
            return;
        }
        WaterProfileApplyFrameType1(data);
        break;
    }
    case KGR_DATA_TYPE_A:
    {
        const KgrExternFrameTypeA *data = (const KgrExternFrameTypeA *)frame->data;
        if ((NULL == data) || (frame->length < sizeof(KgrExternFrameTypeA)))
        {
            ESP_LOGW(WATER_PROFILE_TAG, "Invalid KGR_DATA_TYPE_A length: %u", frame->length);
            return;
        }
        WaterProfileApplyFrameTypeA(data);
        break;
    }
    case KGR_DATA_TYPE_2:
    default:
        break;
    }
}

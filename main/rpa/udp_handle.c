#include <string.h>
#include "udp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "rpa.h"
#include "water_profile.h"

// #include "IOT_AppManager.h"

static const char *TAG = "UDP_HANDLE";

static int pcba_handler(cJSON *reqJson, cJSON *respJson);
static int do_handle_pcba(cJSON *reqJson, cJSON *data);
static int uart_test_handler(cJSON *reqJson, cJSON *respJson);
static int do_handle_uart_test(cJSON *reqJson, cJSON *data);

void udp_init_callback(void)
{
    udp_register_callback_func(PROCESS_PCBA, pcba_handler);
    udp_register_callback_func(PROCESS_TEST_UART, uart_test_handler);
}

char *get_mac_wifi(void)
{
    static char mac_wifi[16] = {0};
    uint8_t mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    sprintf(mac_wifi, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("mac wifi: %s\n", mac_wifi);
    return mac_wifi;
}


static int pcba_handler(cJSON *reqJson, cJSON *respJson)
{
    ESP_LOGW(TAG, "----------- PCBA PROCESS -----------");
    int rsp = CODE_OK;
    cJSON *data_param = cJSON_CreateObject();
    if (data_param)
    {
        rsp = do_handle_pcba(reqJson, data_param);
    }
    else
    {
        return CODE_NOT_RESPONSE;
    }
    cJSON_AddStringToObject(respJson, "process", PROCESS_PCBA);
    cJSON_AddNumberToObject(data_param, "code", rsp);
    cJSON_AddStringToObject(data_param, KEY_MAC_WIFI, get_mac_wifi());
    cJSON_AddStringToObject(data_param, KEY_TYPE, "response");
    cJSON_AddItemToObject(respJson, "data", data_param);
    return rsp == CODE_NOT_RESPONSE ? CODE_NOT_RESPONSE : CODE_OK;
}

static int uart_test_handler(cJSON *reqJson, cJSON *respJson)
{
    ESP_LOGW(TAG, "----------- UART PROCESS -----------");
    int rsp = CODE_OK;
    cJSON *data_param = cJSON_CreateObject();
    if (data_param)
    {
        rsp = do_handle_uart_test(reqJson, data_param);
    }
    else
    {
        return CODE_NOT_RESPONSE;
    }
    cJSON_AddStringToObject(respJson, "process", PROCESS_TEST_UART);
    cJSON_AddNumberToObject(data_param, "code", rsp);
    cJSON_AddStringToObject(data_param, KEY_MAC_WIFI, get_mac_wifi());
    cJSON_AddStringToObject(data_param, KEY_TYPE, "response");
    cJSON_AddItemToObject(respJson, "data", data_param);
    return rsp == CODE_NOT_RESPONSE ? CODE_NOT_RESPONSE : CODE_OK;
}


static int do_handle_pcba(cJSON *reqJson, cJSON *data)
{
    if (cJSON_HasObjectItem(reqJson, KEY_COMMAND) && cJSON_HasObjectItem(reqJson, KEY_TYPE) && cJSON_HasObjectItem(reqJson, "mac"))
    {
        char *cmd = "";
        char *type = "";
        char *mac = "";
        cJSON *jCmd = cJSON_GetObjectItem(reqJson, KEY_COMMAND);
        cJSON *jType = cJSON_GetObjectItem(reqJson, KEY_TYPE);
        cJSON *jMac = cJSON_GetObjectItem(reqJson, "mac");
        if (cJSON_IsString(jCmd))
        {
            cmd = jCmd->valuestring;
        }
        if (cJSON_IsString(jType))
        {
            type = jType->valuestring;
        }
        if (cJSON_IsString(jMac))
        {
            mac = jMac->valuestring;
        }
        // printf("cmd: %s, type: %s\n\n", cmd, type);
        char *mac_real = get_mac_wifi();
        if (strcmp(mac, mac_real) != 0 && strcmp(mac, "alldevice") != 0)
        {
            ESP_LOGE(TAG, "Unknown mac: %s, mac-real: %s", mac, mac_real);
            return CODE_NOT_RESPONSE;
        }
        if (strcmp(type, "request") != 0)
        {
            ESP_LOGE(TAG, "Unknown type: %s", type);
            return CODE_NOT_RESPONSE;
        }
        if (strcmp(cmd, "wifiReport") == 0)
        {
            rpa_deinit_input_task();
            rpa_deinit_output_task();
            char rssi[8] = {0};
            sprintf(rssi, "%d", rd_get_wifi_rssi_test());
            cJSON_AddStringToObject(data, KEY_COMMAND, "wifiReport");
            cJSON_AddStringToObject(data, KEY_SSID, RPA_WIFI_SSID);
            cJSON_AddStringToObject(data, KEY_RSSI, rssi);
            return CODE_OK;
        }
        else if (strcmp(cmd, "adc") == 0)
        {
            rpa_deinit_input_task();
            rpa_deinit_output_task();
            int raw_adc1 = 0; int raw_adc2 = 0;
            rpa_read_adc(&raw_adc1, &raw_adc2);
            cJSON_AddStringToObject(data, KEY_COMMAND, "adc");
            cJSON_AddNumberToObject(data, "raw1", raw_adc1);
            cJSON_AddNumberToObject(data, "raw2", raw_adc2);
            return CODE_OK;
        }
        else if (strcmp(cmd, "input") == 0)
        {
            rpa_deinit_output_task();
            if(rpa_init_input_task() != ESP_OK)
            {
                return CODE_ERR;
            }
            return CODE_OK;
        }
        else if (strcmp(cmd, "output") == 0)
        {
            rpa_deinit_input_task();
            cJSON_AddStringToObject(data, KEY_COMMAND, "output");
            if(rpa_init_output_task() != ESP_OK)
            {
                return CODE_ERR;
            }
            return CODE_OK;
        }
        else if (strcmp(cmd, "lc8823") == 0)
        {
            rpa_deinit_input_task();
            rpa_deinit_output_task();
            cJSON_AddStringToObject(data, KEY_COMMAND, "lc8823");
            cJSON *rgbJson = cJSON_GetObjectItem(reqJson, "rgb");
            if (cJSON_IsNumber(rgbJson)){
                uint32_t rgbVal = rgbJson->valueint;
                uint8_t red = (rgbVal>>16) & 0xff;
                uint8_t green = (rgbVal>>8) & 0xff;
                uint8_t blue = (rgbVal) & 0xff;
                rpa_test_led_rgb(red, green, blue);
                return CODE_OK;
            }
            return CODE_ERR;
        }


        else
        {
            ESP_LOGE(TAG, "Unknown command: %s", cmd);
            return CODE_NOT_RESPONSE;
        }
    }
    return CODE_NOT_RESPONSE;
}

static int do_handle_uart_test(cJSON *reqJson, cJSON *data)
{
    if (cJSON_HasObjectItem(reqJson, KEY_COMMAND) && cJSON_HasObjectItem(reqJson, KEY_TYPE) && cJSON_HasObjectItem(reqJson, "mac"))
    {
        char *cmd = "";
        char *type = "";
        char *mac = "";
        cJSON *jCmd = cJSON_GetObjectItem(reqJson, KEY_COMMAND);
        cJSON *jType = cJSON_GetObjectItem(reqJson, KEY_TYPE);
        cJSON *jMac = cJSON_GetObjectItem(reqJson, "mac");
        if (cJSON_IsString(jCmd))
        {
            cmd = jCmd->valuestring;
        }
        if (cJSON_IsString(jType))
        {
            type = jType->valuestring;
        }
        if (cJSON_IsString(jMac))
        {
            mac = jMac->valuestring;
        }
        // printf("cmd: %s, type: %s\n\n", cmd, type);
        char *mac_real = get_mac_wifi();
        if (strcmp(mac, mac_real) != 0 && strcmp(mac, "alldevice") != 0)
        {
            ESP_LOGE(TAG, "Unknown mac: %s, mac-real: %s", mac, mac_real);
            return CODE_NOT_RESPONSE;
        }
        if (strcmp(type, "request") != 0)
        {
            ESP_LOGE(TAG, "Unknown type: %s", type);
            return CODE_NOT_RESPONSE;
        }
        //===================================================================
        rpa_deinit_input_task();
        rpa_deinit_output_task();
        
        esp_err_t err_code = 0;
        if (strcmp(cmd, "reset") == 0)
        {
            err_code = WaterProfileResetSystem();
            return err_code == ESP_OK ? CODE_OK : CODE_ERR;
        }else if (strcmp(cmd, "setMode") == 0)
        {
            if(cJSON_HasObjectItem(reqJson, "mode")){
                cJSON *jMode =  cJSON_GetObjectItem(reqJson, "mode");
                if (cJSON_IsNumber(jMode)){
                    if(jMode->valueint == 0){
                        err_code = WaterProfileSetFeatureHot(1, 100);
                        return err_code == ESP_OK ? CODE_OK : CODE_ERR;
                    }else if(jMode->valueint == 1)
                    {
                        err_code = WaterProfileSetFeatureCold(1, 100);
                        return err_code == ESP_OK ? CODE_OK : CODE_ERR;
                    }else if(jMode->valueint == 2)
                    {
                        err_code = WaterProfileSetFeatureEnergySaving(1);
                        return err_code == ESP_OK ? CODE_OK : CODE_ERR;
                    }
                }
            }
        }else if (strcmp(cmd, "setFilterTime") == 0)
        {
            if(cJSON_HasObjectItem(reqJson, "filterIdx") && cJSON_HasObjectItem(reqJson, "time"))
            {
                cJSON *jIndex =  cJSON_GetObjectItem(reqJson, "filterIdx");
                cJSON *jTime =  cJSON_GetObjectItem(reqJson, "time");
                if (cJSON_IsNumber(jIndex) && cJSON_IsNumber(jTime))
                {
                    err_code = WaterProfileSetFilterRuntimeMinutes(jIndex->valueint, jTime->valueint);
                    return err_code == ESP_OK ? CODE_OK : CODE_ERR;
                }
            }
        }else if (strcmp(cmd, "testMode") == 0)
        {
            static bool first_test = true;
            if(!first_test) return CODE_NOT_RESPONSE;
            first_test = false;
            err_code = WaterProfileEnableCommandTest(1);
            return err_code == ESP_OK ? CODE_OK : CODE_ERR;

        }else if (strcmp(cmd, "setDevId") == 0)
        {
            if(cJSON_HasObjectItem(reqJson, "setDevId")){
                cJSON *jDevId =  cJSON_GetObjectItem(reqJson, "setDevId");
                if (cJSON_IsNumber(jDevId)){
                    err_code = WaterProfileSetDeviceId(jDevId->valueint);
                    return err_code == ESP_OK ? CODE_OK : CODE_ERR;
                }
            }
        }else if (strcmp(cmd, "getAll") == 0)
        {
            KgrFrameType1 *frame1 = &s_frame_type_1;
            KgrExternFrameTypeA *frameA = &s_frame_type_A;
            cJSON_AddNumberToObject(data, "system_run", frame1->system_run);
            cJSON_AddNumberToObject(data, "system_time", frame1->system_time);
            cJSON_AddNumberToObject(data, "setting", frame1->setting_val);
            cJSON_AddNumberToObject(data, "filter_time_1", frame1->filter_time_1);
            cJSON_AddNumberToObject(data, "filter_time_2", frame1->filter_time_2);
            cJSON_AddNumberToObject(data, "filter_time_3", frame1->filter_time_3);
            cJSON_AddNumberToObject(data, "filter_time_ro", frame1->ro_filter_time);
            cJSON_AddNumberToObject(data, "filter_time_post", frame1->post_filter_time);
            cJSON_AddNumberToObject(data, "dev_id", frameA->dev_id);
            cJSON_AddNumberToObject(data, "output_stt", frameA->output_status);
            cJSON_AddNumberToObject(data, "led_stt", frameA->led_status);

            return CODE_OK;
        }

        else
        {
            ESP_LOGE(TAG, "Unknown command: %s", cmd);
            return CODE_NOT_RESPONSE;
        }
    }
    return CODE_NOT_RESPONSE;
}
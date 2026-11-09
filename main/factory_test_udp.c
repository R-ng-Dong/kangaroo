#include "factory_test_udp.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include "cJSON.h"
#include "common.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app.h"
#include "led_manager.h"
#include "pin_config.h"
#include "pump.h"
#include "tds.h"
#include "water_controller.h"
#include "water_profile.h"
#include "network/wifi.h"
#include "factory_test_ble.h"

#define TEST_UDP_PORT 8181
#define TEST_UDP_MAX_REQUEST 1024
// #define FACTORY_WIFI_SSID "Kangaroo2026"
// #define FACTORY_WIFI_PASSWORD "Kangaroo2026"

#define FACTORY_WIFI_SSID "DigitalR&D"
#define FACTORY_WIFI_PASSWORD "DigitalRD@2804"

static const char *s_tag = "FACTORY_TEST_UDP";
static TaskHandle_t s_task;
static esp_event_handler_instance_t s_factory_wifi_ip_handler;

static void FactoryWifiGotIp(void *arg, esp_event_base_t event_base, int32_t event_id,
                             void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;
    const ip_event_got_ip_t *event = event_data;
    ESP_LOGI(s_tag, "Factory Wi-Fi ready, IP: " IPSTR, IP2STR(&event->ip_info.ip));
}

static esp_err_t FactoryWifiConnect(void)
{
    if (NULL == s_factory_wifi_ip_handler)
    {
        esp_err_t err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            FactoryWifiGotIp, NULL,
                                                            &s_factory_wifi_ip_handler);
        if (ESP_OK != err)
            return err;
    }

    wifi_ap_record_t ap = {0};
    if (ESP_OK == esp_wifi_sta_get_ap_info(&ap) &&
        0 == strcmp((const char *)ap.ssid, FACTORY_WIFI_SSID))
    {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip = {0};
        if (NULL != netif && ESP_OK == esp_netif_get_ip_info(netif, &ip) && 0U != ip.ip.addr)
            ESP_LOGI(s_tag, "Factory Wi-Fi ready, IP: " IPSTR, IP2STR(&ip.ip));
        else
            ESP_LOGI(s_tag, "Connected to factory Wi-Fi; waiting for IP");
        return ESP_OK;
    }

    /* Stop the existing connection before changing the test SSID. */
    IOT_WifiSetAutoReconnect(false);
    esp_err_t err = esp_wifi_stop();
    if (ESP_ERR_WIFI_NOT_STARTED == err)
        err = ESP_OK;
    if (ESP_OK == err)
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, FACTORY_WIFI_SSID, sizeof(FACTORY_WIFI_SSID) - 1U);
    memcpy(config.sta.password, FACTORY_WIFI_PASSWORD, sizeof(FACTORY_WIFI_PASSWORD) - 1U);
    if (ESP_OK == err)
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (ESP_OK == err)
        err = esp_wifi_start();
    if (ESP_OK == err)
        err = esp_wifi_connect();
    IOT_WifiSetAutoReconnect(true);
    memset(&config, 0, sizeof(config));
    if (ESP_OK == err)
        ESP_LOGI(s_tag, "Connecting to factory Wi-Fi: %s", FACTORY_WIFI_SSID);
    else
        ESP_LOGE(s_tag, "Factory Wi-Fi connection failed: %s", esp_err_to_name(err));
    return err;
}

static UI8 ReadUnsigned(const cJSON *params, const char *name, UI32 maximum, UI32 *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(params, name);
    if (!cJSON_IsNumber(item) || 0 > item->valuedouble || maximum < item->valuedouble ||
        item->valuedouble != (double)(UI32)item->valuedouble)
    {
        return 0U;
    }
    *out = (UI32)item->valuedouble;
    return 1U;
}

static void SetResult(cJSON *data, UI8 success, const char *error)
{
    cJSON_AddNumberToObject(data, "code", success ? 0 : -1);
    if (0U == success && NULL != error)
    {
        cJSON_AddStringToObject(data, "error", error);
    }
}

static void SendResponse(int socket_fd, const struct sockaddr_in *peer, socklen_t peer_length,
                         cJSON *response)
{
    char *text = cJSON_PrintUnformatted(response);
    if (NULL != text)
    {
        size_t length = strlen(text);
        char peer_ip[INET_ADDRSTRLEN];
        const char *ip = inet_ntop(AF_INET, &peer->sin_addr, peer_ip, sizeof(peer_ip));
        ssize_t sent = sendto(socket_fd, text, length, 0, (const struct sockaddr *)peer, peer_length);
        if (sent == (ssize_t)length)
        {
            ESP_LOGI(s_tag, "UDP TX -> %s:%u (%u bytes): %s", ip ? ip : "?",
                     (unsigned)ntohs(peer->sin_port), (unsigned)length, text);
        }
        else
        {
            ESP_LOGE(s_tag, "UDP TX failed -> %s:%u", ip ? ip : "?",
                     (unsigned)ntohs(peer->sin_port));
        }
        cJSON_free(text);
    }
}

static void TestAdc(cJSON *data)
{
    /* Keep the TDS task running; these are its latest calculated ppm values. */
    cJSON_AddNumberToObject(data, "raw1", TdsGetValue(TDS_SENSOR_0));
    cJSON_AddNumberToObject(data, "raw2", TdsGetValue(TDS_SENSOR_1));
    cJSON_AddStringToObject(data, "source", "tds_sensor");
    cJSON_AddStringToObject(data, "unit", "ppm");
    SetResult(data, 1U, NULL);
}

// static void TestBle(cJSON *data)
// {
//     if (!FactoryTestBleIsAdvertising())
//     {
//         SetResult(data, 0U, "ble_advertise_failed");
//         return;
//     }
//     cJSON_AddBoolToObject(data, "advertising", 1);
//     cJSON_AddStringToObject(data, "name", FactoryTestBleGetName());
//     SetResult(data, 1U, NULL);
// }

static void TestWifi(cJSON *data)
{
    wifi_ap_record_t ap = {0};
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (ESP_OK != err)
    {
        cJSON_AddNumberToObject(data, "code", -1);
        cJSON_AddBoolToObject(data, "connected", 0);
        cJSON_AddStringToObject(data, "error", "wifi_not_connected");
        return;
    }

    cJSON_AddNumberToObject(data, "code", 0);
    cJSON_AddBoolToObject(data, "connected", 1);
    char ssid[sizeof(ap.ssid) + 1];
    memcpy(ssid, ap.ssid, sizeof(ap.ssid));
    ssid[sizeof(ap.ssid)] = '\0';
    cJSON_AddStringToObject(data, "ssid", ssid);
    cJSON_AddNumberToObject(data, "rssi", ap.rssi);
    cJSON_AddNumberToObject(data, "channel", ap.primary);

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {0};
    if (NULL != netif && ESP_OK == esp_netif_get_ip_info(netif, &ip))
    {
        char ip_text[16];
        snprintf(ip_text, sizeof(ip_text), IPSTR, IP2STR(&ip.ip));
        cJSON_AddStringToObject(data, "ip", ip_text);
    }
}

static void HandlePcbaCommand(const char *method, const cJSON *params, cJSON *data)
{
    if (0 == strcmp(method, "scanDevWifi"))
    {
        TestWifi(data);
        return;
    }
    if (0 == strcmp(method, "getTypeDev"))
    {
        cJSON_AddNumberToObject(data, "type", AppGetModel());
        SetResult(data, 1U, NULL);
        return;
    }
    // if (0 == strcmp(method, "scanDevBle"))
    // {
    //     TestBle(data);
    //     return;
    // }

    if (0 == strcmp(method, "testAdc") || 0 == strcmp(method, "adc"))
    {
        TestAdc(data);
    }
    else if (0 == strcmp(method, "testInput") || 0 == strcmp(method, "input"))
    {
        /* PumpInit applies the board's active-low polarity to this live GPIO reading. */
        cJSON_AddBoolToObject(data, "pumpActive", D_TRUE == PumpIsActive());
        SetResult(data, 1U, NULL);
    }
    else if (0 == strcmp(method, "output"))
    {
        UI32 index, state;
        if (!ReadUnsigned(params, "index", 7, &index) ||
            !ReadUnsigned(params, "state", 1, &state))
        {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        cJSON_AddNumberToObject(data, "index", index);
        cJSON_AddNumberToObject(data, "state", state);
        SetResult(data, D_TRUE == WaterControllerSetOutputControl((UI8)index, (UI8)state),
                  "uart_send_failed");
    }
    else if (0 == strcmp(method, "testLed"))
    {
        const cJSON *color = cJSON_GetObjectItemCaseSensitive(params, "color");
        if (!cJSON_IsString(color) || NULL == color->valuestring ||
            (0 != strcmp(color->valuestring, "R") && 0 != strcmp(color->valuestring, "G") &&
             0 != strcmp(color->valuestring, "B")))
        {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        UI8 red = 0 == strcmp(color->valuestring, "R") ? 255U : 0U;
        UI8 green = 0 == strcmp(color->valuestring, "G") ? 255U : 0U;
        UI8 blue = 0 == strcmp(color->valuestring, "B") ? 255U : 0U;
        SetResult(data, ESP_OK == LedManagerSetRgb(red, green, blue), "led_failed");
    }
    else if (0 == strcmp(method, "lc8823"))
    {
        UI32 rgb;
        if (!ReadUnsigned(params, "rgb", 0xFFFFFF, &rgb))
        {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        cJSON_AddNumberToObject(data, "rgb", rgb);
        SetResult(data, ESP_OK == LedManagerSetRgb((UI8)(rgb >> 16), (UI8)(rgb >> 8), (UI8)rgb),
                  "led_failed");
    }
    else
    {
        SetResult(data, 0U, "unknown_command");
    }
}

static UI8 ReadUartBitmask(WaterPurifyInputStatus type)
{
    UI8 mask = 0U;
    /* MLN getters number inputs 1..8; the response uses bit 0 for input 1. */
    for (UI8 index = 1U; index <= KGR_MAX_INDEX; ++index)
    {
        if (0U != WaterProfileGetInputStatus(type, index))
        {
            mask |= (UI8)(1U << (index - 1U));
        }
    }
    return mask;
}

static void AddUartProfileData(cJSON *data)
{
    /* Return the latest frames decoded by the normal UART1 receiver. */
    if (D_TRUE == HasMessageType1())
    {
        cJSON_AddNumberToObject(data, "system_run", WaterProfileGetSystemRunSeconds());
        cJSON_AddNumberToObject(data, "system_time", WaterProfileGetSystemTimeSeconds());
        cJSON_AddNumberToObject(data, "ro_status", WaterProfileGetStatus(RO_STATUS));
        cJSON_AddNumberToObject(data, "ply_status", WaterProfileGetStatus(PLY_STATUS));
        cJSON_AddNumberToObject(data, "hot_tank_status", WaterProfileGetStatus(HOT_TANK_STATUS));
        cJSON_AddNumberToObject(data, "cold_tank_status", WaterProfileGetStatus(COLD_TANK_STATUS));
        cJSON_AddNumberToObject(data, "setting", WaterProfileGetSetting());
        cJSON_AddNumberToObject(data, "hot_tank_temperature", WaterProfileGetHotTankTemperature());
        cJSON_AddNumberToObject(data, "cold_tank_temperature", WaterProfileGetColdTankTemperature());
        cJSON_AddNumberToObject(data, "hot_target_temperature", WaterProfileGetHotTargetTemperature());
        cJSON_AddNumberToObject(data, "cold_target_temperature", WaterProfileGetColdTargetTemperature());
        cJSON_AddNumberToObject(data, "filter_time_1", WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_1ST));
        cJSON_AddNumberToObject(data, "filter_time_2", WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_2ND));
        cJSON_AddNumberToObject(data, "filter_time_3", WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_3RD));
        cJSON_AddNumberToObject(data, "filter_time_ro", WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_RO));
        cJSON_AddNumberToObject(data, "filter_time_post", WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_POST));
    }
    if (D_TRUE == HasMessageTypeA())
    {
        cJSON_AddNumberToObject(data, "dev_id", WaterProfileGetDeviceId());
        cJSON_AddNumberToObject(data, "input_stt", ReadUartBitmask(INPUT_STATUS));
        cJSON_AddNumberToObject(data, "output_stt", ReadUartBitmask(OUTPUT_STATUS));
        cJSON_AddNumberToObject(data, "switch_stt", ReadUartBitmask(SWITCH_STATUS));
        cJSON_AddNumberToObject(data, "led_stt", ReadUartBitmask(LED_STATUS));
        cJSON_AddNumberToObject(data, "raw1", WaterProfileGetAdcRaw1());
        cJSON_AddNumberToObject(data, "raw2", WaterProfileGetAdcRaw2());
    }
}

static void AddDeviceVersionData(cJSON *data, UI8 frame_type_A_seen)
{
    const char *product_id = WATER_PURIFIER_TYPE_HOT_COLD == AppGetModel()
                                 ? APP_PRODUCT_ID_HOT_COLD
                                 : APP_PRODUCT_ID_STANDARD;
    cJSON_AddStringToObject(data, "productId", product_id);
    cJSON_AddStringToObject(data, "firmwareVersion", CONFIG_APP_PROJECT_VER);

    if (frame_type_A_seen)
    {
        const UI8 *firmware_version = WaterProfileGetFirmwareVersionByte();
        char version[sizeof("255.255")];
        snprintf(version, sizeof(version), "%u.%u",
                 (unsigned int)firmware_version[0],
                 (unsigned int)firmware_version[1]);
        cJSON_AddStringToObject(data, "mainboardVersion", version);
    }
}

static void HandleUartTestCommand(const char *method, const cJSON *params, cJSON *data)
{
    if (0 == strcmp(method, "reset"))
    {
        SetResult(data, D_TRUE == WaterControllerResetSystem(), "uart_send_failed");
    }
    else if (0 == strcmp(method, "setMode"))
    {
        UI32 mode;
        if (!ReadUnsigned(params, "mode", 2, &mode))
        {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        UI8 result = 0U == mode   ? WaterControllerSetFeatureHot(1, 100)
                     : 1U == mode ? WaterControllerSetFeatureCold(1, 20)
                                  : WaterControllerSetFeatureEnergySaving(1);
        cJSON_AddNumberToObject(data, "mode", mode);
        SetResult(data, D_TRUE == result, "uart_send_failed");
    }
    else if (0 == strcmp(method, "setFilterTime"))
    {
        UI32 index, minutes;
        if (!ReadUnsigned(params, "filterIdx", FILTER_TIME_INDEX_POST, &index) ||
            !ReadUnsigned(params, "time", UINT16_MAX, &minutes))
        {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        cJSON_AddNumberToObject(data, "filterIdx", index);
        cJSON_AddNumberToObject(data, "time", minutes);
        SetResult(data,
                  D_TRUE ==
                      WaterControllerSetFilterRuntimeMinutes((FilterTimeIndex)index, (UI16)minutes),
                  "uart_send_failed");
    }
    else if (0 == strcmp(method, "testMode"))
    {
        cJSON_AddNumberToObject(data, "mode", 1);
        SetResult(data, D_TRUE == WaterControllerEnableCommandTest(1), "uart_send_failed");
    }
    else if (0 == strcmp(method, "setDevId"))
    {
        UI32 device_id;
        if (!ReadUnsigned(params, "devId", UINT32_MAX, &device_id) &&
            !ReadUnsigned(params, "setDevId", UINT32_MAX, &device_id))
        {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        cJSON_AddNumberToObject(data, "devId", device_id);
        SetResult(data, D_TRUE == WaterControllerSetDeviceId(device_id), "uart_send_failed");
    }
    else if (0 == strcmp(method, "getAllState") || 0 == strcmp(method, "getAllStatus") ||
             0 == strcmp(method, "getAll"))
    {
        UI8 frame_type_1_seen = D_TRUE == HasMessageType1();
        UI8 frame_type_A_seen = D_TRUE == HasMessageTypeA();
        UI8 received = frame_type_1_seen || frame_type_A_seen;
        AddDeviceVersionData(data, frame_type_A_seen);
        cJSON_AddBoolToObject(data, "uartReceived", received);
        cJSON_AddBoolToObject(data, "frameType1Seen", frame_type_1_seen);
        cJSON_AddBoolToObject(data, "frameTypeASeen", frame_type_A_seen);
        cJSON_AddNumberToObject(data, "frameType1Sequence", WaterProfileGetFrameType1Sequence());
        cJSON_AddNumberToObject(data, "frameTypeASequence", WaterProfileGetFrameTypeASequence());
        if (received)
        {
            cJSON_AddStringToObject(data, "profileSource", "uart_profile");
            AddUartProfileData(data);
        }
        SetResult(data, received, "no_uart_data");
    }
    else
    {
        SetResult(data, 0U, "unknown_command");
    }
}

static void HandleRequest(int socket_fd, const char *request, size_t length,
                          const struct sockaddr_in *peer, socklen_t peer_length)
{
    cJSON *root = cJSON_ParseWithLength(request, length);
    if (NULL == root)
    {
        return;
    }
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    const cJSON *mac = cJSON_GetObjectItemCaseSensitive(params, "mac");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    UI32 request_id;
    if (!cJSON_IsString(method) || !cJSON_IsObject(params) ||
        (NULL != id && 0U == ReadUnsigned(root, "id", UINT32_MAX, &request_id)) ||
        (NULL != mac && !cJSON_IsString(mac)))
    {
        cJSON_Delete(root);
        return;
    }

    /* BLE is scanned by the PC; this disabled UDP method must not produce a reply. */
    if (0 == strcmp(method->valuestring, "scanDevBle"))
    {
        cJSON_Delete(root);
        return;
    }

    UI8 address[6] = {0};
    if (ESP_OK != esp_read_mac(address, ESP_MAC_WIFI_STA))
    {
        cJSON_Delete(root);
        return;
    }
    char own_mac[13];
    snprintf(own_mac, sizeof(own_mac), "%02x%02x%02x%02x%02x%02x", address[0], address[1],
             address[2], address[3], address[4], address[5]);
    if (NULL != mac && 0 != strcmp(mac->valuestring, "alldevice") &&
        0 != strcmp(mac->valuestring, own_mac))
    {
        cJSON_Delete(root);
        return;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON *reply_params = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    if (NULL == response || NULL == reply_params || NULL == data)
    {
        cJSON_Delete(response);
        cJSON_Delete(reply_params);
        cJSON_Delete(data);
        cJSON_Delete(root);
        return;
    }
    cJSON_AddStringToObject(data, "mac", own_mac);
    const char *method_name = method->valuestring;
    char response_method[32];
    if (0 == strcmp(method_name, "scanDevWifi"))
    {
        snprintf(response_method, sizeof(response_method), "scanDevWifiRsp");
    }
    else if (strlen(method_name) + 4U < sizeof(response_method))
    {
        snprintf(response_method, sizeof(response_method), "%sRsp", method_name);
    }
    else
    {
        snprintf(response_method, sizeof(response_method), "unknownRsp");
    }
    if (0 == strcmp(method_name, "scanDevWifi") || 0 == strcmp(method_name, "getTypeDev") ||
        0 == strcmp(method_name, "testAdc") || 0 == strcmp(method_name, "testInput") ||
        0 == strcmp(method_name, "testLed") || 0 == strcmp(method_name, "adc") ||
        0 == strcmp(method_name, "input") || 0 == strcmp(method_name, "output") ||
        0 == strcmp(method_name, "lc8823"))
    {
        HandlePcbaCommand(method_name, params, data);
    }
    else
    {
        HandleUartTestCommand(method_name, params, data);
    }
    cJSON *code = cJSON_DetachItemFromObjectCaseSensitive(data, "code");
    cJSON *error = cJSON_DetachItemFromObjectCaseSensitive(data, "error");
    cJSON_AddStringToObject(response, "method", response_method);
    UI8 success = NULL != code && 0 == code->valueint;
    UI8 include_code = !success || 0 == strcmp(method_name, "output") ||
                       0 == strcmp(method_name, "testLed") || 0 == strcmp(method_name, "lc8823") ||
                       0 == strcmp(method_name, "reset") || 0 == strcmp(method_name, "getAllState") ||
                       0 == strcmp(method_name, "getAllStatus") ||
                       0 == strcmp(method_name, "getAll");
    if (include_code)
        cJSON_AddItemToObject(reply_params, "code", NULL != code ? code : cJSON_CreateNumber(-1));
    else
        cJSON_Delete(code);
    if (NULL != error)
        cJSON_AddItemToObject(reply_params, "error", error);
    if (success && (0 == strcmp(method_name, "reset") || 0 == strcmp(method_name, "setMode") ||
                    0 == strcmp(method_name, "testMode") || 0 == strcmp(method_name, "setFilterTime") ||
                    0 == strcmp(method_name, "setDevId")))
        cJSON_DeleteItemFromObjectCaseSensitive(data, "mac");
    if (success && (0 == strcmp(method_name, "testLed") || 0 == strcmp(method_name, "reset")))
        cJSON_Delete(data);
    else
        cJSON_AddItemToObject(reply_params, "data", data);
    cJSON_AddItemToObject(response, "params", reply_params);
    SendResponse(socket_fd, peer, peer_length, response);
    cJSON_Delete(response);
    cJSON_Delete(root);
}

static void FactoryTestUdpTask(void *arg)
{
    (void)arg;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (0 > socket_fd)
    {
        ESP_LOGE(s_tag, "Cannot create UDP socket");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    const struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_port = htons(TEST_UDP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (0 > bind(socket_fd, (const struct sockaddr *)&local, sizeof(local)))
    {
        ESP_LOGE(s_tag, "Cannot bind UDP port %d", TEST_UDP_PORT);
        close(socket_fd);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(s_tag, "Listening on UDP port %d", TEST_UDP_PORT);
    char request[TEST_UDP_MAX_REQUEST];
    for (;;)
    {
        struct sockaddr_in peer = {0};
        socklen_t peer_length = sizeof(peer);
        ssize_t length = recvfrom(socket_fd, request, sizeof(request), 0, (struct sockaddr *)&peer,
                                  &peer_length);
        if (0 < length)
        {
            char peer_ip[INET_ADDRSTRLEN];
            const char *ip = inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
            ESP_LOGI(s_tag, "UDP RX <- %s:%u (%u bytes): %.*s", ip ? ip : "?",
                     (unsigned)ntohs(peer.sin_port), (unsigned)length, (int)length, request);
            HandleRequest(socket_fd, request, (size_t)length, &peer, peer_length);
        }
        else if (0 > length)
        {
            ESP_LOGE(s_tag, "UDP receive failed");
            break;
        }
    }
    close(socket_fd);
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t FactoryTestUdpStart(void)
{
    if (NULL != s_task)
    {
        return ESP_OK;
    }
    /* The PC scans this device's BLE advertisement; UDP only reports its status. */
    // esp_err_t ble_err = FactoryTestBleStartAdvertising();
    // if (ESP_OK != ble_err)
    //     ESP_LOGE(s_tag, "Factory BLE advertising unavailable: %s", esp_err_to_name(ble_err));
    esp_err_t err = FactoryWifiConnect();
    if (ESP_OK != err)
        return err;
    PumpInit(PUMP_PIN, PUMP_ACTIVE_LOW);
    if (pdPASS != xTaskCreate(FactoryTestUdpTask, "factory_test_udp", 4096, NULL, 5, &s_task))
    {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

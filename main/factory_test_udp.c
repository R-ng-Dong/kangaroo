#include "factory_test_udp.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "ble/IOT_BleMgmt.h"
#include "cJSON.h"
#include "common.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "adc_driver.h"
#include "led_manager.h"
#include "pin_config.h"
#include "pump.h"
#include "tds_task.h"
#include "water_controller.h"
#include "water_profile.h"
#include "uart_driver.h"

#define TEST_UDP_PORT 8181
#define TEST_UDP_MAX_REQUEST 1024
#define BLE_SCAN_MS 3000
#define UART_TEST_BUFFER_SIZE 1024
#define UART_TEST_CHUNK_SIZE 256
/* Use the same TDS wiring as production; each request samples one pulse polarity. */
#define FACTORY_ADC_SETTLE_US 100U
#define FACTORY_TDS_0_PULSE_1 GPIO_NUM_0
#define FACTORY_TDS_0_PULSE_2 GPIO_NUM_10
#define FACTORY_TDS_1_PULSE_1 GPIO_NUM_4
#define FACTORY_TDS_1_PULSE_2 GPIO_NUM_5
#define FACTORY_TDS_0_ADC DRV_ADC_CHANNEL_3
#define FACTORY_TDS_1_ADC DRV_ADC_CHANNEL_2

static const char *s_tag = "FACTORY_TEST_UDP";
static TaskHandle_t s_task;
static volatile UI32 s_ble_count;
static volatile int s_ble_best_rssi;
static TaskHandle_t s_uart_test_task;
/* ADC and pulse pins are configured once, on the first factory ADC request. */
static UI8 s_adc_ready;

static UI8 ReadUnsigned(const cJSON *params, const char *name, UI32 maximum, UI32 *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(params, name);
    if (!cJSON_IsNumber(item) || 0 > item->valuedouble || maximum < item->valuedouble ||
        item->valuedouble != (double)(UI32)item->valuedouble) {
        return 0U;
    }
    *out = (UI32)item->valuedouble;
    return 1U;
}

static void SetResult(cJSON *data, UI8 success, const char *error) {
    cJSON_AddNumberToObject(data, "code", success ? 0 : -1);
    if (0U == success && NULL != error) {
        cJSON_AddStringToObject(data, "error", error);
    }
}

static void SendResponse(int socket_fd, const struct sockaddr_in *peer, socklen_t peer_length,
                         cJSON *response) {
    char *text = cJSON_PrintUnformatted(response);
    if (NULL != text) {
        sendto(socket_fd, text, strlen(text), 0, (const struct sockaddr *)peer, peer_length);
        cJSON_free(text);
    }
}

static esp_err_t FactoryAdcStopPulse(void) {
    /* Clear every pulse pin even if an earlier GPIO write fails. */
    esp_err_t first_err = gpio_set_level(FACTORY_TDS_0_PULSE_1, 0);
    esp_err_t err = gpio_set_level(FACTORY_TDS_0_PULSE_2, 0);
    if (ESP_OK == first_err) {
        first_err = err;
    }
    err = gpio_set_level(FACTORY_TDS_1_PULSE_1, 0);
    if (ESP_OK == first_err) {
        first_err = err;
    }
    err = gpio_set_level(FACTORY_TDS_1_PULSE_2, 0);
    if (ESP_OK == first_err) {
        first_err = err;
    }
    return first_err;
}

static esp_err_t FactoryAdcInit(void) {
    /* Release the TDS task's GPIO and ADC resources before taking factory samples. */
    TdsTaskStop();

    gpio_config_t pulse_config = {
        .pin_bit_mask = (1ULL << FACTORY_TDS_0_PULSE_1) | (1ULL << FACTORY_TDS_0_PULSE_2) |
                        (1ULL << FACTORY_TDS_1_PULSE_1) | (1ULL << FACTORY_TDS_1_PULSE_2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&pulse_config);
    if (ESP_OK != err) {
        return err;
    }
    err = FactoryAdcStopPulse();
    if (ESP_OK != err) {
        return err;
    }

    /* Both probes use the production ADC channels with 12 dB attenuation and 12-bit reads. */
    err = AdcOneshotInit(FACTORY_TDS_0_ADC, DRV_ADC_ATTEN_DB_12, DRV_ADC_BITWIDTH_12);
    if (ESP_OK == err) {
        err = AdcOneshotInit(FACTORY_TDS_1_ADC, DRV_ADC_ATTEN_DB_12, DRV_ADC_BITWIDTH_12);
    }
    if (ESP_OK != err) {
        AdcOneshotDeinit();
        return err;
    }
    s_adc_ready = 1U;
    return ESP_OK;
}

static void TestAdc(cJSON *data) {
    if (0U == s_adc_ready && ESP_OK != FactoryAdcInit()) {
        SetResult(data, 0U, "adc_init_failed");
        return;
    }

    /* Pulse 1 high and pulse 2 low on each probe yields one raw value per probe. */
    esp_err_t err = gpio_set_level(FACTORY_TDS_0_PULSE_2, 0);
    if (ESP_OK == err) {
        err = gpio_set_level(FACTORY_TDS_1_PULSE_2, 0);
    }
    if (ESP_OK == err) {
        err = gpio_set_level(FACTORY_TDS_0_PULSE_1, 1);
    }
    if (ESP_OK == err) {
        err = gpio_set_level(FACTORY_TDS_1_PULSE_1, 1);
    }
    UI16 raw1 = 0U;
    UI16 raw2 = 0U;
    if (ESP_OK == err) {
        /* Allow the pulse voltage to settle before reading either ADC channel. */
        esp_rom_delay_us(FACTORY_ADC_SETTLE_US);
        err = AdcOneshotRead(FACTORY_TDS_0_ADC, &raw1);
    }
    if (ESP_OK == err) {
        err = AdcOneshotRead(FACTORY_TDS_1_ADC, &raw2);
    }
    /* Remove DC drive after the reads, including when a GPIO or ADC call fails. */
    esp_err_t stop_err = FactoryAdcStopPulse();
    if (ESP_OK == err) {
        err = stop_err;
    }
    if (ESP_OK != err) {
        SetResult(data, 0U, "adc_read_failed");
        return;
    }
    cJSON_AddNumberToObject(data, "raw1", raw1);
    cJSON_AddNumberToObject(data, "raw2", raw2);
    SetResult(data, 1U, NULL);
}

static void TestUartTask(void *arg) {
    (void)arg;
    UI8 bytes[UART_TEST_CHUNK_SIZE];
    for (;;) {
        int received = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes), portMAX_DELAY);
        if (received < 0) {
            ESP_LOGE(s_tag, "Factory UART read failed");
            break;
        }
        int offset = 0;
        while (offset < received) {
            int written = uart_write_bytes(UART_NUM_1, bytes + offset, received - offset);
            if (written <= 0) {
                ESP_LOGE(s_tag, "Factory UART echo failed");
                break;
            }
            offset += written;
        }
    }
    s_uart_test_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t TestUartInit(void) {
    if (NULL != s_uart_test_task)
        return ESP_OK;

    /* Factory test owns UART1: stop the normal receiver before installing a raw port. */
    esp_err_t err = ESP_OK;
    if (uart_is_driver_installed(UART_NUM_1)) {
        err = UartDeinit(UART_NUM_1);
        if (ESP_OK != err)
            return err;
    }

    const uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    err = uart_driver_install(UART_NUM_1, UART_TEST_BUFFER_SIZE, UART_TEST_BUFFER_SIZE, 0, NULL, 0);
    if (ESP_OK != err)
        return err;
    err = uart_param_config(UART_NUM_1, &config);
    if (ESP_OK == err)
        err = uart_set_pin(UART_NUM_1, PIN_UART_TX, PIN_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ESP_OK == err && pdPASS != xTaskCreate(TestUartTask, "factory_uart", 2048, NULL, 5, &s_uart_test_task))
        err = ESP_ERR_NO_MEM;
    if (ESP_OK != err)
        uart_driver_delete(UART_NUM_1);
    return err;
}

static void BleScanCallback(void *arg, IOT_BleMgmtEventStatusType_t event, void *event_data) {
    (void)arg;
    if (BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT == event && NULL != event_data) {
        const IOT_BleMgmtEventScanResultData_t *result = event_data;
        ++s_ble_count;
        if (s_ble_best_rssi < result->rssi) {
            s_ble_best_rssi = result->rssi;
        }
    }
}

static void TestBle(cJSON *data) {
    if (!IOT_BleIsAvailable()) {
        cJSON_AddNumberToObject(data, "code", -1);
        cJSON_AddStringToObject(data, "error", "ble_unavailable");
        return;
    }

    s_ble_count = 0;
    s_ble_best_rssi = -127;
    if (IOT_OK != IOT_BleRegisterEventCallback(BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT,
                                               BleScanCallback, NULL)) {
        cJSON_AddNumberToObject(data, "code", -1);
        cJSON_AddStringToObject(data, "error", "ble_callback_failed");
        return;
    }
    /* A zero duration scans until stopped on both supported BLE stacks. */
    iot_err_t err = IOT_BleStartScan(0);
    if (IOT_OK != err) {
        IOT_BleUnregisterEventCallback(BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT);
        cJSON_AddNumberToObject(data, "code", -1);
        cJSON_AddStringToObject(data, "error", "ble_scan_failed");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(BLE_SCAN_MS));
    err = IOT_BleStopScan();
    IOT_BleUnregisterEventCallback(BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT);

    cJSON_AddNumberToObject(data, "code", IOT_OK == err ? 0 : -1);
    cJSON_AddNumberToObject(data, "devices", (double)s_ble_count);
    cJSON_AddBoolToObject(data, "detected", 0U < s_ble_count);
    if (0U < s_ble_count) {
        cJSON_AddNumberToObject(data, "bestRssi", s_ble_best_rssi);
    }
    if (IOT_OK != err) {
        cJSON_AddStringToObject(data, "error", "ble_scan_stop_failed");
    }
}

static void TestWifi(cJSON *data) {
    wifi_ap_record_t ap = {0};
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (ESP_OK != err) {
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
    if (NULL != netif && ESP_OK == esp_netif_get_ip_info(netif, &ip)) {
        char ip_text[16];
        snprintf(ip_text, sizeof(ip_text), IPSTR, IP2STR(&ip.ip));
        cJSON_AddStringToObject(data, "ip", ip_text);
    }
}

static void HandlePcbaCommand(const char *method, const cJSON *params, cJSON *data) {
    if (0 == strcmp(method, "wifiReport")) {
        TestWifi(data);
        return;
    }
    if (0 == strcmp(method, "bleReport")) {
        TestBle(data);
        return;
    }

    if (0 == strcmp(method, "adc")) {
        TestAdc(data);
    } else if (0 == strcmp(method, "input")) {
        /* PumpInit applies the board's active-low polarity to this live GPIO reading. */
        cJSON_AddBoolToObject(data, "pump_active", D_TRUE == PumpIsActive());
        SetResult(data, 1U, NULL);
    }
    // else if (0 == strcmp(method, "output")) {
    //     UI32 index, state;
    //     if (!ReadUnsigned(params, "index", 7, &index) ||
    //         !ReadUnsigned(params, "state", 1, &state)) {
    //         SetResult(data, 0U, "invalid_argument");
    //         return;
    //     }
    //     cJSON_AddNumberToObject(data, "index", index);
    //     cJSON_AddNumberToObject(data, "state", state);
    //     SetResult(data, D_TRUE == WaterControllerSetOutputControl((UI8)index, (UI8)state),
    //               "uart_send_failed");
    // }
    else if (0 == strcmp(method, "lc8823")) {
        UI32 rgb;
        if (!ReadUnsigned(params, "rgb", 0xFFFFFF, &rgb)) {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        cJSON_AddNumberToObject(data, "rgb", rgb);
        SetResult(data, ESP_OK == LedManagerSetRgb((UI8)(rgb >> 16), (UI8)(rgb >> 8), (UI8)rgb),
                  "led_failed");
    } else {
        SetResult(data, 0U, "unknown_command");
    }
}

static UI8 ReadUartBitmask(WaterPurifyInputStatus type) {
    UI8 mask = 0U;
    /* MLN getters number inputs 1..8; the response uses bit 0 for input 1. */
    for (UI8 index = 1U; index <= KGR_MAX_INDEX; ++index) {
        if (0U != WaterProfileGetInputStatus(type, index)) {
            mask |= (UI8)(1U << (index - 1U));
        }
    }
    return mask;
}

static void HandleUartTestCommand(const char *method, const cJSON *params, cJSON *data) {
    if (0 == strcmp(method, "reset")) {
        SetResult(data, D_TRUE == WaterControllerResetSystem(), "uart_send_failed");
    } else if (0 == strcmp(method, "setMode")) {
        UI32 mode;
        if (!ReadUnsigned(params, "mode", 2, &mode)) {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        UI8 result = 0U == mode   ? WaterControllerSetFeatureHot(1, 100)
                     : 1U == mode ? WaterControllerSetFeatureCold(1, 20)
                                  : WaterControllerSetFeatureEnergySaving(1);
        cJSON_AddNumberToObject(data, "mode", mode);
        SetResult(data, D_TRUE == result, "uart_send_failed");
    } else if (0 == strcmp(method, "setFilterTime")) {
        UI32 index, minutes;
        if (!ReadUnsigned(params, "filterIdx", FILTER_TIME_INDEX_POST, &index) ||
            !ReadUnsigned(params, "time", UINT16_MAX, &minutes)) {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        cJSON_AddNumberToObject(data, "filterIdx", index);
        cJSON_AddNumberToObject(data, "time", minutes);
        SetResult(data,
                  D_TRUE ==
                      WaterControllerSetFilterRuntimeMinutes((FilterTimeIndex)index, (UI16)minutes),
                  "uart_send_failed");
    } else if (0 == strcmp(method, "testMode")) {
        SetResult(data, D_TRUE == WaterControllerEnableCommandTest(1), "uart_send_failed");
    } else if (0 == strcmp(method, "setDevId")) {
        UI32 device_id;
        if (!ReadUnsigned(params, "setDevId", UINT32_MAX, &device_id)) {
            SetResult(data, 0U, "invalid_argument");
            return;
        }
        cJSON_AddNumberToObject(data, "setDevId", device_id);
        SetResult(data, D_TRUE == WaterControllerSetDeviceId(device_id), "uart_send_failed");
    } else if (0 == strcmp(method, "getAll")) {
        /* The response combines fields from MLN UART frames 1 and A. */
        if (NULL != s_uart_test_task || D_TRUE != HasMessageType1() || D_TRUE != HasMessageTypeA()) {
            SetResult(data, 0U, "no_uart_data");
            return;
        }
        cJSON_AddNumberToObject(data, "system_run", WaterProfileGetSystemRunSeconds());
        cJSON_AddNumberToObject(data, "system_time", WaterProfileGetSystemTimeSeconds());
        cJSON_AddNumberToObject(data, "setting", WaterProfileGetSetting());
        cJSON_AddNumberToObject(data, "filter_time_1",
                                WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_1ST));
        cJSON_AddNumberToObject(data, "filter_time_2",
                                WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_2ND));
        cJSON_AddNumberToObject(data, "filter_time_3",
                                WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_3RD));
        cJSON_AddNumberToObject(data, "filter_time_ro",
                                WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_RO));
        cJSON_AddNumberToObject(data, "filter_time_post",
                                WaterProfileGetFilterRuntimeSeconds(FILTER_TIME_INDEX_POST));
        cJSON_AddNumberToObject(data, "dev_id", WaterProfileGetDeviceId());
        cJSON_AddNumberToObject(data, "input_stt", ReadUartBitmask(INPUT_STATUS));
        cJSON_AddNumberToObject(data, "output_stt", ReadUartBitmask(OUTPUT_STATUS));
        cJSON_AddNumberToObject(data, "switch_stt", ReadUartBitmask(SWITCH_STATUS));
        cJSON_AddNumberToObject(data, "led_stt", ReadUartBitmask(LED_STATUS));
        /* These ADC values come from the MLN UART frame; method adc reads local TDS pins. */
        cJSON_AddNumberToObject(data, "raw1", WaterProfileGetAdcRaw1());
        cJSON_AddNumberToObject(data, "raw2", WaterProfileGetAdcRaw2());
        SetResult(data, 1U, NULL);
    } else {
        SetResult(data, 0U, "unknown_command");
    }
}

static void HandleRequest(int socket_fd, const char *request, size_t length,
                          const struct sockaddr_in *peer, socklen_t peer_length) {
    cJSON *root = cJSON_ParseWithLength(request, length);
    if (NULL == root) {
        return;
    }
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    const cJSON *mac = cJSON_GetObjectItemCaseSensitive(params, "mac");
    UI32 request_id;
    if (0U == ReadUnsigned(root, "id", UINT32_MAX, &request_id) || !cJSON_IsString(method) ||
        !cJSON_IsObject(params) || !cJSON_IsString(mac)) {
        cJSON_Delete(root);
        return;
    }

    UI8 address[6] = {0};
    if (ESP_OK != esp_read_mac(address, ESP_MAC_WIFI_STA)) {
        cJSON_Delete(root);
        return;
    }
    char own_mac[13];
    snprintf(own_mac, sizeof(own_mac), "%02x%02x%02x%02x%02x%02x", address[0], address[1],
             address[2], address[3], address[4], address[5]);
    if (0 != strcmp(mac->valuestring, "alldevice") && 0 != strcmp(mac->valuestring, own_mac)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    if (NULL == response || NULL == data) {
        cJSON_Delete(response);
        cJSON_Delete(data);
        cJSON_Delete(root);
        return;
    }
    cJSON_AddNumberToObject(response, "id", request_id);
    cJSON_AddStringToObject(data, "mac", own_mac);
    const char *method_name = method->valuestring;
    if (0 == strcmp(method_name, "wifiReport") || 0 == strcmp(method_name, "bleReport") ||
        0 == strcmp(method_name, "adc") || 0 == strcmp(method_name, "input") ||
        0 == strcmp(method_name, "output") || 0 == strcmp(method_name, "lc8823")) {
        HandlePcbaCommand(method_name, params, data);
    } else {
        HandleUartTestCommand(method_name, params, data);
    }
    cJSON_AddItemToObject(response, "result", data);
    SendResponse(socket_fd, peer, peer_length, response);
    cJSON_Delete(response);
    cJSON_Delete(root);
}

static void FactoryTestUdpTask(void *arg) {
    (void)arg;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (0 > socket_fd) {
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
    if (0 > bind(socket_fd, (const struct sockaddr *)&local, sizeof(local))) {
        ESP_LOGE(s_tag, "Cannot bind UDP port %d", TEST_UDP_PORT);
        close(socket_fd);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(s_tag, "Listening on UDP port %d", TEST_UDP_PORT);
    char request[TEST_UDP_MAX_REQUEST];
    for (;;) {
        struct sockaddr_in peer = {0};
        socklen_t peer_length = sizeof(peer);
        ssize_t length = recvfrom(socket_fd, request, sizeof(request), 0, (struct sockaddr *)&peer,
                                  &peer_length);
        if (0 < length) {
            HandleRequest(socket_fd, request, (size_t)length, &peer, peer_length);
        } else if (0 > length) {
            ESP_LOGE(s_tag, "UDP receive failed");
            break;
        }
    }
    close(socket_fd);
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t FactoryTestUdpStart(void) {
    if (NULL != s_task) {
        return ESP_OK;
    }
    /* Factory UART runs independently of UDP and echoes bytes on the same port. */
    esp_err_t err = TestUartInit();
    if (ESP_OK != err)
        return err;
    PumpInit(PUMP_PIN, PUMP_ACTIVE_LOW);
    if (pdPASS != xTaskCreate(FactoryTestUdpTask, "factory_test_udp", 4096, NULL, 5, &s_task)) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

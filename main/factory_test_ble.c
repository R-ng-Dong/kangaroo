#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "IOT_Core.h"
#include "ble/IOT_BleMgmt.h"
#include "common.h"
#include "factory_test_ble.h"

#define FACTORY_BLE_UUID_LENGTH 16U
#define FACTORY_BLE_SSID_MAX_LENGTH 32U
#define FACTORY_BLE_PASSWORD_MAX_LENGTH 63U
#define FACTORY_BLE_PAYLOAD_MAX_LENGTH                                                             \
    (FACTORY_BLE_SSID_MAX_LENGTH + FACTORY_BLE_PASSWORD_MAX_LENGTH)
#define FACTORY_BLE_START_COMMAND 1U
#define FACTORY_BLE_DATA_COMMAND 2U
#define FACTORY_BLE_COMMIT_COMMAND 3U
#define FACTORY_BLE_MAX_PACKET_LENGTH 20U
#define FACTORY_BLE_DATA_HEADER_LENGTH 2U
#define FACTORY_BLE_WORKER_STACK_SIZE 3072U

/* Canonical UUIDs:
 * Service: 6f1c4a80-9f62-4de8-941b-4be0ad65f431
 * Write:   6f1c4a80-9f62-4de8-941b-4be0ad65f432
 * NimBLE stores 128-bit UUID bytes in little-endian order. */
static UI8 s_service_uuid[FACTORY_BLE_UUID_LENGTH] = {
    0x31, 0xf4, 0x65, 0xad, 0xe0, 0x4b, 0x1b, 0x94, 0xe8, 0x4d, 0x62, 0x9f, 0x80, 0x4a, 0x1c, 0x6f};
static UI8 s_write_uuid[FACTORY_BLE_UUID_LENGTH] = {0x32, 0xf4, 0x65, 0xad, 0xe0, 0x4b, 0x1b, 0x94,
                                                    0xe8, 0x4d, 0x62, 0x9f, 0x80, 0x4a, 0x1c, 0x6f};

typedef struct {
    char ssid[FACTORY_BLE_SSID_MAX_LENGTH + 1U];
    char password[FACTORY_BLE_PASSWORD_MAX_LENGTH + 1U];
} WifiCredentials;

static const char *s_tag = "FACTORY_TEST_BLE";
static QueueHandle_t s_wifi_queue;
static UI8 s_payload[FACTORY_BLE_PAYLOAD_MAX_LENGTH];
static UI8 s_ssid_len;
static UI8 s_password_len;
static UI8 s_received;
static UI8 s_receiving;

static void ClearTransfer(void) {
    memset(s_payload, 0, sizeof(s_payload));
    s_ssid_len = 0;
    s_password_len = 0;
    s_received = 0;
    s_receiving = 0U;
}

static int WifiWrite(struct IOT_BleMgmtGattAccessContext_t *ctxt, void *arg) {
    (void)arg;
    if (NULL == ctxt || IOT_BLE_GATT_ACCESS_OP_WRITE_CHR != ctxt->op || NULL == ctxt->om ||
        NULL == ctxt->om->data || 0U == ctxt->om->len || IOT_CoreIsProvisioned()) {
        return -1;
    }
    const UI8 *data = ctxt->om->data;
    const UI16 length = ctxt->om->len;
    if (FACTORY_BLE_START_COMMAND == data[0]) {
        ClearTransfer();
        if (3U != length || 0U == data[1] || FACTORY_BLE_SSID_MAX_LENGTH < data[1] ||
            8U > data[2] || FACTORY_BLE_PASSWORD_MAX_LENGTH < data[2]) {
            return -1;
        }
        s_ssid_len = data[1];
        s_password_len = data[2];
        s_receiving = 1U;
        return 0;
    }
    if (FACTORY_BLE_DATA_COMMAND == data[0]) {
        if (0U == s_receiving || 3U > length || FACTORY_BLE_MAX_PACKET_LENGTH < length ||
            s_received != data[1] ||
            s_ssid_len + s_password_len < s_received + length - FACTORY_BLE_DATA_HEADER_LENGTH) {
            return -1;
        }
        memcpy(s_payload + s_received, data + FACTORY_BLE_DATA_HEADER_LENGTH,
               length - FACTORY_BLE_DATA_HEADER_LENGTH);
        s_received += length - FACTORY_BLE_DATA_HEADER_LENGTH;
        return 0;
    }
    if (FACTORY_BLE_COMMIT_COMMAND == data[0]) {
        if (0U == s_receiving || 1U != length || s_received != s_ssid_len + s_password_len ||
            NULL == s_wifi_queue) {
            return -1;
        }
        if (NULL != memchr(s_payload, 0, s_ssid_len + s_password_len)) {
            ClearTransfer();
            return -1;
        }
        WifiCredentials credentials = {0};
        memcpy(credentials.ssid, s_payload, s_ssid_len);
        memcpy(credentials.password, s_payload + s_ssid_len, s_password_len);
        const BaseType_t queued = xQueueSend(s_wifi_queue, &credentials, 0);
        memset(&credentials, 0, sizeof(credentials));
        ClearTransfer();
        if (pdTRUE == queued) {
            ESP_LOGI(s_tag, "Wi-Fi credentials received over BLE");
        }
        return pdTRUE == queued ? 0 : -1;
    }
    return -1;
}

static IOT_BleMgmtGattChar_t s_characteristics[] = {
    {
        .uuid_len = sizeof(s_write_uuid),
        .uuid = s_write_uuid,
        .access_cb = WifiWrite,
        .flags = IOT_BLE_GATT_CHR_F_WRITE | IOT_BLE_GATT_CHR_F_WRITE_ENC,
    },
    {.uuid = NULL},
};

static IOT_BleMgmtGattServ_t s_services[] = {
    {
        .service_index = 0,
        .type = IOT_GATT_SERVICE_TYPE_PRIMARY,
        .uuid_len = sizeof(s_service_uuid),
        .uuid = s_service_uuid,
        .characteristics = s_characteristics,
    },
    {.type = 0, .uuid = NULL},
};

static void WifiWorker(void *arg) {
    (void)arg;
    WifiCredentials credentials;
    for (;;) {
        if (pdTRUE == xQueueReceive(s_wifi_queue, &credentials, portMAX_DELAY)) {
            wifi_config_t config = {0};
            memcpy(config.sta.ssid, credentials.ssid, strlen(credentials.ssid));
            memcpy(config.sta.password, credentials.password, strlen(credentials.password));
            esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &config);
            if (ESP_OK == result) {
                result = esp_wifi_connect();
            }
            ESP_LOGI(s_tag, "Wi-Fi configuration %s", ESP_OK == result ? "submitted" : "failed");
            memset(&config, 0, sizeof(config));
            memset(&credentials, 0, sizeof(credentials));
        }
    }
}

esp_err_t FactoryTestBleInit(void) {
    if (!IOT_BleIsAvailable()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_wifi_queue = xQueueCreate(1, sizeof(WifiCredentials));
    if (NULL == s_wifi_queue) {
        return ESP_ERR_NO_MEM;
    }
    /* The worker holds two Wi-Fi credential buffers plus ESP-IDF call frames. */
    if (pdPASS !=
        xTaskCreate(WifiWorker, "factory_wifi", FACTORY_BLE_WORKER_STACK_SIZE, NULL, 5, NULL)) {
        vQueueDelete(s_wifi_queue);
        s_wifi_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (IOT_OK != IOT_BleAddServices(s_services)) {
        ESP_LOGE(s_tag, "Could not register factory Wi-Fi GATT service");
        return ESP_FAIL;
    }
    return ESP_OK;
}

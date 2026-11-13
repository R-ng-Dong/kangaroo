#include "IOT_MqttMgmt.h"
#include "IOT_Memory.h"
#include "IOT_Log.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "IOT_Osal.h"
#include "sys/param.h"

static const char *TAG = "ESP_MQTT";

/* MQTT keepalive in seconds. esp-mqtt sends PINGREQ every keepalive/2 and the broker
 * declares the client dead after 1.5x keepalive of silence, so this is the knob for how
 * fast a dropped link is noticed on both ends. 120 matches what esp-mqtt substitutes when
 * the field is left at 0; stated here so the value is a choice rather than a default.
 *
 * Do not lower this without measuring on a real link. esp-mqtt waits reconnect_timeout_ms
 * (10 s default, not overridden here) before retrying after it drops a connection, so
 * below ~15 s a WiFi stall costs more offline time than the faster detection buys, and
 * every spurious drop pays for a full TLS handshake. */
#ifndef IOT_MQTT_KEEPALIVE_SEC
#define IOT_MQTT_KEEPALIVE_SEC 120
#endif

typedef struct
{
    IOT_MqttEventCb_t cb;
    void *arg;
    bool isRegistered;
} IOT_MqttCbMgmt_t;

struct IOT_MqttClient
{
    esp_mqtt_client_handle_t espClient;
    char *pem;
    IOT_MqttCbMgmt_t cbs[IOT_MQTT_EVENT_MAX];
    volatile bool isConnected;
    volatile bool isStarted;       /* esp-mqtt task running: between a start and a stop/destroy */
    IOT_OsalTaskHandle_t mqttTask; /* esp-mqtt's own task, learned from the events it dispatches */
};

/* A controlled reboot closes every started session before WiFi goes down.
 *
 * esp_restart() runs its shutdown handlers in reverse registration order. WiFi registers
 * esp_wifi_stop when the network comes up, before any client connects, so the handler
 * registered at connect runs first. Without it, WiFi stops under an esp-mqtt task blocked in a
 * TLS read, and that read fails with four E lines from esp-tls and esp-mqtt on every factory
 * reset of a connected device. */
#define IOT_MQTT_SHUTDOWN_SLOTS 4
static struct IOT_MqttClient *s_liveClients[IOT_MQTT_SHUTDOWN_SLOTS];

static void trackClient(struct IOT_MqttClient *client)
{
    int freeSlot = -1;
    for (int i = 0; i < IOT_MQTT_SHUTDOWN_SLOTS; i++)
    {
        if (s_liveClients[i] == client)
        {
            return;
        }
        if (s_liveClients[i] == NULL && freeSlot < 0)
        {
            freeSlot = i;
        }
    }
    if (freeSlot < 0)
    {
        IOT_LOGW(TAG, "No shutdown slot left: this client is not stopped before a reboot");
        return;
    }
    s_liveClients[freeSlot] = client;
}

static void untrackClient(struct IOT_MqttClient *client)
{
    for (int i = 0; i < IOT_MQTT_SHUTDOWN_SLOTS; i++)
    {
        if (s_liveClients[i] == client)
        {
            s_liveClients[i] = NULL;
        }
    }
}

static void mqttShutdownHandler(void)
{
    IOT_OsalTaskHandle_t self = IOT_OsalTaskGetCurrent();
    for (int i = 0; i < IOT_MQTT_SHUTDOWN_SLOTS; i++)
    {
        struct IOT_MqttClient *client = s_liveClients[i];
        if (client == NULL || !client->isStarted || client->espClient == NULL)
        {
            continue;
        }
        /* A reboot started from the esp-mqtt task itself (a cloud removal) has no other task
         * reading the socket, and esp-mqtt refuses a stop from there with an E of its own. */
        if (client->mqttTask != NULL && client->mqttTask == self)
        {
            continue;
        }
        client->isStarted = false;
        client->isConnected = false;
        (void) esp_mqtt_client_stop(client->espClient);
    }
}

static IOT_MqttEventStatus_t getEventTypeFromEspMqttEvent(esp_mqtt_event_id_t espEventId)
{
    switch (espEventId)
    {
    case MQTT_EVENT_CONNECTED:
        return IOT_MQTT_EVENT_CONNECTED;
    case MQTT_EVENT_DISCONNECTED:
        return IOT_MQTT_EVENT_DISCONNECTED;
    case MQTT_EVENT_SUBSCRIBED:
        return IOT_MQTT_EVENT_SUBSCRIBED;
    case MQTT_EVENT_UNSUBSCRIBED:
        return IOT_MQTT_EVENT_UNSUBSCRIBED;
    case MQTT_EVENT_PUBLISHED:
        return IOT_MQTT_EVENT_PUBLISHED;
    case MQTT_EVENT_DATA:
        return IOT_MQTT_EVENT_DATA;
    case MQTT_EVENT_ERROR:
        return IOT_MQTT_EVENT_ERROR;
    case MQTT_EVENT_BEFORE_CONNECT:
        return IOT_MQTT_EVENT_MAX;
    case MQTT_EVENT_DELETED:
        IOT_LOGI(TAG, "MQTT_EVENT_DELETED received - not mapped");
        return IOT_MQTT_EVENT_MAX;
    default:
        return IOT_MQTT_EVENT_MAX;
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    struct IOT_MqttClient *ctx = (struct IOT_MqttClient *) handler_args;
    if (ctx == NULL)
    {
        IOT_LOGE(TAG, "MQTT event handler called with NULL context");
        return;
    }
    ctx->mqttTask = IOT_OsalTaskGetCurrent();

    IOT_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    int msg_id;
    IOT_MqttEventStatus_t eventType = getEventTypeFromEspMqttEvent((esp_mqtt_event_id_t) event_id);
    if (eventType == IOT_MQTT_EVENT_MAX)
    {
        return;
    }
    switch ((esp_mqtt_event_id_t) event_id)
    {
    case MQTT_EVENT_CONNECTED:
    {
        ctx->isConnected = true;
        if (ctx->cbs[eventType].isRegistered && ctx->cbs[eventType].cb)
        {
            ctx->cbs[eventType].cb(ctx->cbs[eventType].arg, eventType, event);
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
    {
        IOT_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        ctx->isConnected = false;
        if (ctx->cbs[eventType].isRegistered && ctx->cbs[eventType].cb)
        {
            ctx->cbs[eventType].cb(ctx->cbs[eventType].arg, eventType, event);
        }
        break;
    }

    case MQTT_EVENT_SUBSCRIBED:
    {
        msg_id = event->msg_id;
        if (ctx->cbs[eventType].isRegistered && ctx->cbs[eventType].cb)
        {
            IOT_MqttSubscribedEventData_t subEvt;
            subEvt.msgId = msg_id;
            subEvt.returnedCode = 0;
            if (event->data && event->data_len > 0)
            {
                subEvt.returnedCode = (uint8_t) event->data[0];
            }
            ctx->cbs[eventType].cb(ctx->cbs[eventType].arg, eventType, &subEvt);
        }
        break;
    }
    case MQTT_EVENT_UNSUBSCRIBED:
    {
        IOT_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = event->msg_id;
        if (ctx->cbs[eventType].isRegistered && ctx->cbs[eventType].cb)
        {
            IOT_MqttUnsubscribedEventData_t unsubEvt;
            unsubEvt.msgId = event->msg_id;
            ctx->cbs[eventType].cb(ctx->cbs[eventType].arg, eventType, &unsubEvt);
        }
        break;
    }
    case MQTT_EVENT_PUBLISHED:
    {
        msg_id = event->msg_id;
        if (ctx->cbs[eventType].isRegistered && ctx->cbs[eventType].cb)
        {
            IOT_MqttPublishedEventData_t pubEvt;
            pubEvt.msgId = event->msg_id;
            ctx->cbs[eventType].cb(ctx->cbs[eventType].arg, eventType, &pubEvt);
        }
        break;
    }
    case MQTT_EVENT_DATA:
    {
        if (ctx->cbs[eventType].isRegistered && ctx->cbs[eventType].cb)
        {
            IOT_MqttDataEventData_t dataEvt;
            memset(&dataEvt, 0, sizeof(dataEvt));
            dataEvt.dataLen = (uint16_t) event->data_len;

            /* copy topic into a null-terminated string */
            if (event->topic && event->topic_len > 0)
            {
                dataEvt.topic = (char *) Mem_SafeMalloc(event->topic_len + 1, TAG, "mqtt topic");
                if (dataEvt.topic)
                {
                    memcpy(dataEvt.topic, event->topic, event->topic_len);
                    dataEvt.topic[event->topic_len] = '\0';
                }
                // IOT_LOGI(TAG, "MQTT_EVENT_DATA received on topic: %s", dataEvt.topic ? dataEvt.topic : "(null)");
            }

            /* copy data payload */
            if (event->data && event->data_len > 0)
            {
                dataEvt.data = (uint8_t *) Mem_SafeMalloc(event->data_len, TAG, "mqtt data");
                if (dataEvt.data)
                {
                    memcpy(dataEvt.data, event->data, event->data_len);
                }
            }

            ctx->cbs[eventType].cb(ctx->cbs[eventType].arg, eventType, &dataEvt);

            if (dataEvt.topic)
                SAFE_FREE(dataEvt.topic);
            if (dataEvt.data)
                SAFE_FREE(dataEvt.data);
        }
        break;
    }
    case MQTT_EVENT_ERROR:
    {
        IOT_LOGW(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            IOT_LOGW(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            IOT_LOGW(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            /* The single most diagnostic field, and it was not being logged. esp-tls captures
             * the X.509 verify flags but only PRINTS them under CONFIG_LOG_DEFAULT_LEVEL_DEBUG
             * and at ESP_LOGD (esp_tls_mbedtls.c:444), so a release-level capture of a failing
             * handshake showed -0x2700 with no reason attached — which sent a 2026-09-14
             * investigation through the broker, the clock and the heap before landing here.
             *
             * Decode: 0x01 EXPIRED, 0x02 REVOKED, 0x04 CN_MISMATCH, 0x08 NOT_TRUSTED,
             * 0x200 FUTURE. CN_MISMATCH means the server's name does not cover the host we
             * asked for; NOT_TRUSTED means the CA we were provisioned with did not issue it. */
            IOT_LOGW(TAG, "TLS cert verify flags: 0x%x%s%s%s%s",
                     event->error_handle->esp_tls_cert_verify_flags,
                     (event->error_handle->esp_tls_cert_verify_flags & 0x01) ? " EXPIRED" : "",
                     (event->error_handle->esp_tls_cert_verify_flags & 0x04) ? " CN_MISMATCH" : "",
                     (event->error_handle->esp_tls_cert_verify_flags & 0x08) ? " NOT_TRUSTED" : "",
                     (event->error_handle->esp_tls_cert_verify_flags & 0x200) ? " FUTURE" : "");
            IOT_LOGW(TAG,
                     "Last captured errno : %d (%s)",
                     event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            IOT_LOGW(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        }
        else
        {
            IOT_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    }
    default:
        IOT_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

/* The one place in the platform layer that touches esp_log; everything else logs through
 * IOT_Log.h. ESP-IDF's own component tags log an E line on every dying MQTT
 * socket (WiFi drop) and on every failed connect (broker unreachable). Those are expected network
 * events that MQTT_EVENT_ERROR above already reports -- esp-tls code, X.509 verify flags, errno --
 * at W, so the duplicates only drown real errors. NONE, not WARN: WARN would leave the E lines.
 * Tags checked against the ESP-IDF sources (esp_tls.c, esp_tls_mbedtls.c, transport_ssl.c,
 * mqtt_client.c, esp_mbedtls_dynamic_impl.c). Idempotent, so every IOT_MqttInit() may call it. */
static void silenceIdfNetworkTags(void)
{
    esp_log_level_set("esp-tls", ESP_LOG_NONE);
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("mqtt_client", ESP_LOG_NONE);
    esp_log_level_set("Dynamic Impl", ESP_LOG_NONE);
}

iot_err_t IOT_MqttInit(IOT_MqttClientHandle_t *outHandle)
{
    if (outHandle == NULL)
    {
        IOT_LOGE(TAG, "outHandle is NULL");
        return IOT_ERR_INVALID_ARG;
    }

    struct IOT_MqttClient *client = (struct IOT_MqttClient *) Mem_SafeMalloc(sizeof(struct IOT_MqttClient), TAG, "mqtt client");
    if (client == NULL)
    {
        IOT_LOGE(TAG, "Failed to allocate memory for MQTT client");
        return IOT_ERR_NO_MEM;
    }
    memset(client, 0, sizeof(struct IOT_MqttClient));

    silenceIdfNetworkTags();
    *outHandle = client;
    return IOT_OK;
}

iot_err_t IOT_MqttConfigClient(IOT_MqttClientHandle_t handle, const IOT_MqttClientConfig_t config)
{
    if (handle == NULL)
    {
        IOT_LOGE(TAG, "MQTT handle is NULL");
        return IOT_ERR_INVALID_ARG;
    }

    if (config.cert != NULL)
    {
        SAFE_FREE(handle->pem);
        handle->pem = (char *) Mem_SafeMalloc(strlen(config.cert) + 1, TAG, "mqtt pem");
        if (handle->pem == NULL)
        {
            return IOT_ERR_NO_MEM;
        }
        strcpy(handle->pem, config.cert);

    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker =
            {
                .address =
                    {
                        .uri = config.uri,
                        .port = config.port,
                    },
                .verification =
                    {
                        .certificate = (const char *) handle->pem,
                    },
            },
        .credentials =
            {
                .client_id = config.clientId,
                .username = config.username,
                .authentication =
                    {
                        .password = config.password,
                    },
            },
        .session =
            {
                .keepalive = IOT_MQTT_KEEPALIVE_SEC,
            },
        .network =
            {
                .timeout_ms = 5000,
            },
        .buffer =
            {
                .size = 2048,
                .out_size = 2048,
            },
        .outbox =
            {
                .limit = 4096,
            },
    };
    /* Destroy previous client if reconfiguring (e.g., after WiFi switch) */
    if (handle->espClient != NULL)
    {
        handle->isStarted = false; /* destroy stops a running client itself */
        esp_mqtt_client_destroy(handle->espClient);
        handle->espClient = NULL;
    }
    handle->espClient = esp_mqtt_client_init(&mqtt_cfg);
    if (handle->espClient == NULL)
    {
        IOT_LOGE(TAG, "Failed to initialize MQTT client");
        return IOT_ERR_FAIL;
    }

    esp_err_t esp_err = esp_mqtt_client_register_event(handle->espClient, ESP_EVENT_ANY_ID, mqtt_event_handler, handle);
    if (esp_err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(esp_err));
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_MqttDeinit(IOT_MqttClientHandle_t handle)
{
    if (handle == NULL)
    {
        return IOT_OK;
    }
    untrackClient(handle);
    handle->isStarted = false;
    if (handle->espClient != NULL)
    {
        esp_mqtt_client_destroy(handle->espClient);
        handle->espClient = NULL;
    }
    if (handle->pem != NULL)
    {
        SAFE_FREE(handle->pem);
        handle->pem = NULL;
    }
    SAFE_FREE(handle);
    return IOT_OK;
}

iot_err_t IOT_MqttConnect(IOT_MqttClientHandle_t handle)
{
    if (handle == NULL || handle->espClient == NULL)
    {
        IOT_LOGE(TAG, "MQTT client not initialized");
        return IOT_ERR_FAIL;
    }
    /* TLS handshake needs a large contiguous allocation; log heap so a
     * SSL_ALLOC_FAILED (mbedtls 0x7f00) can be correlated with headroom. INFO, not
     * WARNING: it fires on every connect and nothing is wrong when it does. */
    IOT_LOGI(TAG, "pre-connect heap: free=%u largest-block=%u", (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    esp_err_t err = esp_mqtt_client_start(handle->espClient);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return IOT_ERR_FAIL;
    }
    handle->isStarted = true;
    trackClient(handle);
    esp_err_t regErr = esp_register_shutdown_handler(mqttShutdownHandler);
    if (regErr != ESP_OK && regErr != ESP_ERR_INVALID_STATE) /* INVALID_STATE: already registered */
    {
        IOT_LOGW(TAG, "MQTT shutdown handler not registered: %s", esp_err_to_name(regErr));
    }
    return IOT_OK;
}

iot_err_t IOT_MqttGetConnectionStatus(IOT_MqttClientHandle_t handle, bool *isConnected)
{
    if (handle == NULL)
    {
        IOT_LOGE(TAG, "MQTT client not initialized");
        return IOT_ERR_FAIL;
    }
    *isConnected = handle->isConnected;
    return IOT_OK;
}

iot_err_t IOT_MqttDisconnect(IOT_MqttClientHandle_t handle)
{
    if (handle == NULL || handle->espClient == NULL)
    {
        return IOT_OK; /* Not initialized — nothing to disconnect */
    }
    if (!handle->isStarted)
    {
        /* Already stopped, or never started (a connect held for the clock): nothing to stop.
         * esp-mqtt would refuse the stop with a W and ESP_FAIL, and this would log an E. */
        handle->isConnected = false;
        return IOT_OK;
    }
    handle->isConnected = false;
    handle->isStarted = false;
    esp_err_t err = esp_mqtt_client_stop(handle->espClient);
    if (err != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(err));
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

// Enable only for local diagnostics. Distribution sync preflight fails if these are enabled.
// #define IOT_MQTT_TRACE_TX_PAYLOAD
// #define IOT_MQTT_TRACE_TX_TOPIC
iot_err_t IOT_MqttSendMessage(IOT_MqttClientHandle_t handle, char *topic, uint8_t *payload, uint16_t len)
{
    if (handle == NULL || handle->espClient == NULL)
    {
        return IOT_ERR_FAIL;
    }
    if (!handle->isConnected)
    {
        return IOT_ERR_FAIL;
    }
#ifdef IOT_MQTT_TRACE_TX_TOPIC
    IOT_LOGW(TAG, "Publishing message on topic '%s'", topic);
#endif
#ifdef IOT_MQTT_TRACE_TX_PAYLOAD
    IOT_LOGW(TAG, "Publishing MQTT message with payload (len %d):", len);
    for (int i = 0; i < len; i++)
    {
        printf("%02x ", payload[i]);
    }
    printf("\n");
#endif
    int msg_id = esp_mqtt_client_publish(handle->espClient, topic, (const char *) payload, len, 0, 0);
    if (msg_id < 0)
    {
        IOT_LOGE(TAG, "Failed to publish message");
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_MqttSubscribe(IOT_MqttClientHandle_t handle, char *topic, const uint8_t qosMax, int *msgId)
{
    if (handle == NULL || handle->espClient == NULL)
    {
        IOT_LOGE(TAG, "MQTT client not initialized");
        return IOT_ERR_FAIL;
    }
    *msgId = esp_mqtt_client_subscribe(handle->espClient, topic, qosMax);
    if (*msgId < 0)
    {
        IOT_LOGE(TAG, "Failed to subscribe to topic");
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_MqttUnsubscribe(IOT_MqttClientHandle_t handle, char *topic)
{
    if (handle == NULL || handle->espClient == NULL)
    {
        IOT_LOGE(TAG, "MQTT client not initialized");
        return IOT_ERR_FAIL;
    }
    int msg_id = esp_mqtt_client_unsubscribe(handle->espClient, topic);
    if (msg_id < 0)
    {
        IOT_LOGE(TAG, "Failed to unsubscribe from topic");
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

iot_err_t IOT_MqttRegisterEventCallback(IOT_MqttClientHandle_t handle, IOT_MqttEventStatus_t eventType,
                                        IOT_MqttEventCb_t callback, void *arg)
{
    if (handle == NULL)
    {
        IOT_LOGE(TAG, "MQTT handle is NULL");
        return IOT_ERR_FAIL;
    }
    if (eventType < 0 || eventType >= IOT_MQTT_EVENT_MAX)
    {
        IOT_LOGE(TAG, "Invalid MQTT event type: %d", eventType);
        return IOT_ERR_FAIL;
    }

    handle->cbs[eventType].cb = callback;
    handle->cbs[eventType].arg = arg;
    handle->cbs[eventType].isRegistered = (callback != NULL);

    return IOT_OK;
}

iot_err_t IOT_MqttUnregisterEventCallback(IOT_MqttClientHandle_t handle, IOT_MqttEventStatus_t eventType)
{
    if (handle == NULL)
    {
        IOT_LOGE(TAG, "MQTT handle is NULL");
        return IOT_ERR_FAIL;
    }
    if (eventType < 0 || eventType >= IOT_MQTT_EVENT_MAX)
    {
        IOT_LOGE(TAG, "Invalid MQTT event type: %d", eventType);
        return IOT_ERR_FAIL;
    }

    handle->cbs[eventType].cb = NULL;
    handle->cbs[eventType].arg = NULL;
    handle->cbs[eventType].isRegistered = false;

    return IOT_OK;
}

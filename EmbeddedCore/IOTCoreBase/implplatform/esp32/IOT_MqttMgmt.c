#include "IOT_MqttMgmt.h"
#include "IOT_Memory.h"
#include "IOT_Log.h"
#include "esp_err.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_heap_caps.h"
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
};

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
        IOT_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            IOT_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            IOT_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            IOT_LOGI(TAG,
                     "Last captured errno : %d (%s)",
                     event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            IOT_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
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
    handle->isConnected = false;
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

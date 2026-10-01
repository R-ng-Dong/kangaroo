/**
 * @file IOT_MqttMgmt.h
 * @brief Platform-independent MQTT Client Management
 *
 * @details
 * Provides a unified MQTT client interface for IoT devices. Supports
 * multiple independent MQTT clients, each with its own connection,
 * subscriptions, and event callbacks.
 *
 * **Key Features:**
 * - Multiple independent MQTT clients via opaque handles
 * - Connect/disconnect from MQTT broker
 * - Publish messages to topics
 * - Subscribe/unsubscribe to topics
 * - Event-driven architecture with per-client callbacks
 * - TLS/SSL support
 * - QoS 0, 1, 2 support
 * - Automatic reconnection (platform-specific)
 *
 * **Typical Usage:**
 * @code
 * // 1. Initialize MQTT client
 * IOT_MqttClientHandle_t client;
 * IOT_MqttInit(&client);
 *
 * // 2. Configure client
 * IOT_MqttClientConfig_t config = {
 *     .uri = "mqtt://broker.example.com",
 *     .port = 1883,
 *     .clientId = "my_device_001",
 *     .username = "user",
 *     .password = "pass",
 *     .cert = NULL  // No TLS
 * };
 * IOT_MqttConfigClient(client, config);
 *
 * // 3. Register event callbacks
 * IOT_MqttRegisterEventCallback(client, IOT_MQTT_EVENT_CONNECTED, on_connected, NULL);
 * IOT_MqttRegisterEventCallback(client, IOT_MQTT_EVENT_DATA, on_data_received, NULL);
 *
 * // 4. Connect
 * IOT_MqttConnect(client);
 *
 * // 5. Subscribe and publish
 * int msg_id;
 * IOT_MqttSubscribe(client, "sensors/temperature", 0, &msg_id);
 * IOT_MqttSendMessage(client, "status", (uint8_t*)"online", 6);
 * @endcode
 *
 * @author IoT Platform Team
 * @date 2025-12-18
 * @version 2.0
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include "IOT_ErrorManager.h"

#ifdef __cplusplus
extern "C"
{
#endif

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

/**
 * @brief Opaque MQTT client handle
 *
 * @details
 * Each handle represents an independent MQTT client with its own connection,
 * subscriptions, and callbacks. Created by IOT_MqttInit() and destroyed by
 * IOT_MqttDeinit().
 */
typedef struct IOT_MqttClient *IOT_MqttClientHandle_t;

/**
 * @brief MQTT event types
 *
 * @details
 * Events are triggered by MQTT client state changes and are delivered
 * through registered callbacks.
 */
typedef enum
{
    IOT_MQTT_EVENT_DISCONNECTED = 0,  /**< Disconnected from broker */
    IOT_MQTT_EVENT_CONNECTED,         /**< Connected to broker */
    IOT_MQTT_EVENT_SUBSCRIBED,        /**< Successfully subscribed to topic */
    IOT_MQTT_EVENT_UNSUBSCRIBED,      /**< Successfully unsubscribed from topic */
    IOT_MQTT_EVENT_PUBLISHED,         /**< Message published successfully (QoS 1/2) */
    IOT_MQTT_EVENT_DATA,              /**< Received data on subscribed topic */
    IOT_MQTT_EVENT_ERROR,             /**< Error occurred */
    IOT_MQTT_EVENT_MAX                /**< Number of event types */
} IOT_MqttEventStatus_t;

/**
 * @brief Data for SUBSCRIBED event
 */
typedef struct {
    int msgId;              /**< Message ID from subscribe request */
    uint8_t returnedCode;   /**< QoS granted by broker (0, 1, or 2) */
} IOT_MqttSubscribedEventData_t;

/**
 * @brief Data for UNSUBSCRIBED event
 */
typedef struct {
    int msgId;              /**< Message ID from unsubscribe request */
} IOT_MqttUnsubscribedEventData_t;

/**
 * @brief Data for PUBLISHED event
 */
typedef struct {
    int msgId;              /**< Message ID from publish request */
} IOT_MqttPublishedEventData_t;

/**
 * @brief Data for DATA event
 */
typedef struct {
    char *topic;            /**< Topic name */
    uint8_t *data;          /**< Message payload */
    uint16_t dataLen;       /**< Payload length in bytes */
} IOT_MqttDataEventData_t;

/**
 * @brief MQTT client configuration
 */
typedef struct
{
    char *uri;              /**< Broker URI (e.g., "mqtt://broker.com", "mqtts://secure.com") */
    uint16_t port;          /**< Broker port (1883 for MQTT, 8883 for MQTTS) */
    char *clientId;         /**< Unique client identifier */
    char *username;         /**< Username for authentication (NULL if not needed) */
    char *password;         /**< Password for authentication (NULL if not needed) */
    char *cert;             /**< Broker certificate in PEM format for TLS (NULL for no TLS) */
} IOT_MqttClientConfig_t;

/**
 * @brief MQTT event callback function type
 *
 * @param[in] arg User-provided argument (from registration)
 * @param[in] eventType Type of event that occurred
 * @param[in] eventData Event-specific data (cast to appropriate struct)
 */
typedef void (*IOT_MqttEventCb_t)(void *arg, IOT_MqttEventStatus_t eventType, void *eventData);

// ============================================================================
// MQTT LIFECYCLE MANAGEMENT
// ============================================================================

/**
 * @brief Initialize a new MQTT client
 *
 * @details
 * Allocates and initializes a new MQTT client instance. The returned handle
 * must be used for all subsequent operations on this client.
 * Multiple clients can be created independently.
 *
 * @param[out] outHandle Pointer to receive the new client handle
 *
 * @return IOT_OK on success, IOT_ERR_NO_MEM on allocation failure
 *
 * @note Each handle must be cleaned up with IOT_MqttDeinit()
 *
 * @see IOT_MqttDeinit()
 * @see IOT_MqttConfigClient()
 */
iot_err_t IOT_MqttInit(IOT_MqttClientHandle_t *outHandle);

/**
 * @brief Configure MQTT client
 *
 * @details
 * Sets the broker connection parameters. Must be called after IOT_MqttInit()
 * and before IOT_MqttConnect().
 *
 * @param[in] handle Client handle from IOT_MqttInit()
 * @param[in] config Client configuration (broker URI, credentials, etc.)
 *
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG if handle or config is invalid
 *
 * @note All string pointers in config must remain valid until IOT_MqttDeinit()
 * @note Can be called multiple times to change configuration
 *
 * @see IOT_MqttInit()
 * @see IOT_MqttConnect()
 */
iot_err_t IOT_MqttConfigClient(IOT_MqttClientHandle_t handle, const IOT_MqttClientConfig_t config);

/**
 * @brief Deinitialize MQTT client
 *
 * @details
 * Disconnects from broker and frees all resources associated with this client.
 * The handle becomes invalid after this call.
 *
 * @param[in] handle Client handle to destroy
 *
 * @return IOT_OK on success, IOT_ERR_FAIL on failure
 *
 * @note Caller should set their handle variable to NULL after this call
 *
 * @see IOT_MqttInit()
 */
iot_err_t IOT_MqttDeinit(IOT_MqttClientHandle_t handle);

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

/**
 * @brief Connect to MQTT broker
 *
 * @details
 * Establishes connection to the configured MQTT broker.
 * IOT_MQTT_EVENT_CONNECTED callback is triggered on success.
 *
 * @param[in] handle Client handle
 *
 * @return IOT_OK if connection initiated, IOT_ERR_FAIL on error
 *
 * @note This function is asynchronous - returns before connection completes
 * @note Register IOT_MQTT_EVENT_CONNECTED callback to know when connected
 *
 * @see IOT_MqttDisconnect()
 * @see IOT_MqttGetConnectionStatus()
 */
iot_err_t IOT_MqttConnect(IOT_MqttClientHandle_t handle);

/**
 * @brief Get current connection status
 *
 * @param[in] handle Client handle
 * @param[out] isConnected Pointer to receive connection status
 *
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG if isConnected is NULL
 *
 * @see IOT_MqttConnect()
 */
iot_err_t IOT_MqttGetConnectionStatus(IOT_MqttClientHandle_t handle, bool *isConnected);

/**
 * @brief Disconnect from MQTT broker
 *
 * @details
 * Gracefully disconnects from broker. IOT_MQTT_EVENT_DISCONNECTED
 * callback is triggered on disconnect.
 *
 * @param[in] handle Client handle
 *
 * @return IOT_OK on success, IOT_ERR_FAIL on failure
 *
 * @see IOT_MqttConnect()
 */
iot_err_t IOT_MqttDisconnect(IOT_MqttClientHandle_t handle);

// ============================================================================
// PUBLISH / SUBSCRIBE
// ============================================================================

/**
 * @brief Publish message to topic
 *
 * @details
 * Publishes a message to the specified MQTT topic.
 * Default QoS is 0 (fire and forget).
 *
 * @param[in] handle Client handle
 * @param[in] topic Topic name (e.g., "sensors/temperature")
 * @param[in] payload Message payload (binary data)
 * @param[in] len Payload length in bytes
 *
 * @return IOT_OK on success, IOT_ERR_FAIL on failure
 *
 * @note Must be connected before publishing
 *
 * @see IOT_MqttSubscribe()
 */
iot_err_t IOT_MqttSendMessage(IOT_MqttClientHandle_t handle, char *topic, uint8_t *payload, uint16_t len);

/**
 * @brief Subscribe to topic
 *
 * @details
 * Subscribes to receive messages on the specified topic.
 * Supports MQTT wildcards (+ and #).
 *
 * @param[in] handle Client handle
 * @param[in] topic Topic filter (supports wildcards)
 * @param[in] qosMax Maximum QoS level (0, 1, or 2)
 * @param[out] msgId Message ID for tracking (can be NULL)
 *
 * @return IOT_OK on success, IOT_ERR_FAIL on failure
 *
 * @see IOT_MqttUnsubscribe()
 * @see IOT_MqttRegisterEventCallback()
 */
iot_err_t IOT_MqttSubscribe(IOT_MqttClientHandle_t handle, char *topic, const uint8_t qosMax, int *msgId);

/**
 * @brief Unsubscribe from topic
 *
 * @param[in] handle Client handle
 * @param[in] topic Topic filter to unsubscribe from
 *
 * @return IOT_OK on success, IOT_ERR_FAIL on failure
 *
 * @see IOT_MqttSubscribe()
 */
iot_err_t IOT_MqttUnsubscribe(IOT_MqttClientHandle_t handle, char *topic);

// ============================================================================
// EVENT CALLBACKS
// ============================================================================

/**
 * @brief Register event callback
 *
 * @details
 * Registers a callback function for a specific MQTT event type on this client.
 * Only one callback per event type per client is allowed (new registration replaces old).
 *
 * @param[in] handle Client handle
 * @param[in] eventType Event type to register for
 * @param[in] callback Callback function
 * @param[in] arg User argument to pass to callback
 *
 * @return IOT_OK on success, IOT_ERR_INVALID_ARG if callback is NULL
 *
 * @see IOT_MqttUnregisterEventCallback()
 */
iot_err_t IOT_MqttRegisterEventCallback(IOT_MqttClientHandle_t handle, IOT_MqttEventStatus_t eventType,
                                        IOT_MqttEventCb_t callback, void *arg);

/**
 * @brief Unregister event callback
 *
 * @param[in] handle Client handle
 * @param[in] eventType Event type to unregister
 *
 * @return IOT_OK on success
 *
 * @see IOT_MqttRegisterEventCallback()
 */
iot_err_t IOT_MqttUnregisterEventCallback(IOT_MqttClientHandle_t handle, IOT_MqttEventStatus_t eventType);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "IOT_ErrorManager.h"

// ============================================================================
// Constants
// ============================================================================

#define IOT_ZIGBEE_IEEE_SIZE    8   ///< EUI-64 address length in bytes
#define IOT_ZIGBEE_MODEL_SIZE  32   ///< Max length for model/friendly-name strings
#define IOT_ZIGBEE_VENDOR_SIZE 32   ///< Max length for vendor string

// ============================================================================
// Event payloads
// ============================================================================

/**
 * @brief Payload for the device_joined event.
 *
 * Fires immediately when a device connects. Device type is unknown at this
 * point — wait for onInterviewed for the full model info.
 */
typedef struct
{
    uint8_t ieeeAddr[IOT_ZIGBEE_IEEE_SIZE]; ///< EUI-64 (8 bytes, big-endian)
    char    friendlyName[IOT_ZIGBEE_MODEL_SIZE]; ///< Z2M friendly name (defaults to IEEE string)
} IOT_ZigbeeJoinInfo_t;

/**
 * @brief Payload for the device_interview_successful event.
 *
 * Fires after Z2M has finished interrogating the device and knows its model.
 * Only fires on success — failed interviews do not fire this callback.
 */
typedef struct
{
    uint8_t  ieeeAddr[IOT_ZIGBEE_IEEE_SIZE];     ///< EUI-64 (8 bytes, big-endian)
    char     friendlyName[IOT_ZIGBEE_MODEL_SIZE]; ///< Z2M friendly name
    bool     supported;                           ///< Z2M has a converter for this model
    char     model[IOT_ZIGBEE_MODEL_SIZE];        ///< e.g. "LED1624G9"
    char     vendor[IOT_ZIGBEE_VENDOR_SIZE];      ///< e.g. "IKEA"
    uint16_t deviceType;                          ///< 0 — reserved until model/vendor lookup is implemented
} IOT_ZigbeeDeviceInfo_t;

// ============================================================================
// Callbacks
// ============================================================================

/** @brief Called immediately when a device joins the network. */
typedef void (*IOT_ZigbeeDeviceJoinedCb_t)(const IOT_ZigbeeJoinInfo_t *info);

/**
 * @brief Called after Z2M successfully interviews the device.
 * Not called on interview failure — caller should handle timeouts separately.
 */
typedef void (*IOT_ZigbeeDeviceInterviewedCb_t)(const IOT_ZigbeeDeviceInfo_t *info);

/** @brief Called when a device leaves the network. */
typedef void (*IOT_ZigbeeDeviceLeftCb_t)(const uint8_t ieeeAddr[IOT_ZIGBEE_IEEE_SIZE]);

/**
 * @brief Callback bundle passed to IOT_ZigbeeCoordinatorInit().
 * Any callback may be NULL if not needed.
 */
typedef struct
{
    IOT_ZigbeeDeviceJoinedCb_t      onJoined;
    IOT_ZigbeeDeviceInterviewedCb_t onInterviewed;
    IOT_ZigbeeDeviceLeftCb_t        onLeft;
} IOT_ZigbeeCoordinatorCallbacks_t;

// ============================================================================
// API
// ============================================================================

/**
 * @brief Initialise the Zigbee coordinator and subscribe for device events.
 *
 * Linux: connects to the local MQTT broker and subscribes to
 * zigbee2mqtt/bridge/event. Callbacks fire on the mosquitto network thread.
 *
 * Call before IOT_SDKInit() is not required, but must be called before
 * any OpenPairing/ClosePairing call.
 *
 * @param cbs  Callback bundle. May be NULL (no events delivered).
 * @return IOT_OK on success.
 */
iot_err_t IOT_ZigbeeCoordinatorInit(const IOT_ZigbeeCoordinatorCallbacks_t *cbs);

/**
 * @brief Deinitialise the Zigbee coordinator.
 *
 * Linux: disconnects from the Z2M MQTT broker.
 *
 * @return IOT_OK on success, IOT_ERR_INVALID_STATE if not initialised,
 *         IOT_ERR_NOT_SUPPORTED if no driver is registered.
 */
iot_err_t IOT_ZigbeeCoordinatorDeinit(void);

/**
 * @brief Open the Zigbee pairing window.
 *
 * Linux: publishes {"value":true,"time":<timeSec>} to
 * zigbee2mqtt/bridge/request/permit_join.
 *
 * @param gatewayEid       EID of the hub gateway (logged, forwarded to Z2M context).
 * @param timeSec          Pairing window duration in seconds.
 * @param mode             0=stop, 1=pair one device, 2=pair N devices.
 * @param deviceTypeFilter 0=accept all device types, else filter by type.
 * @return IOT_OK on success, IOT_ERR_INVALID_STATE if not initialised.
 */
iot_err_t IOT_ZigbeeCoordinatorOpenPairing(uint16_t gatewayEid, uint8_t timeSec,
                                            uint8_t mode, uint16_t deviceTypeFilter);

/**
 * @brief Close the Zigbee pairing window immediately.
 *
 * Linux: publishes {"value":false} to zigbee2mqtt/bridge/request/permit_join.
 *
 * @param gatewayEid EID of the hub gateway (logged).
 * @return IOT_OK on success, IOT_ERR_INVALID_STATE if not initialised.
 */
iot_err_t IOT_ZigbeeCoordinatorClosePairing(uint16_t gatewayEid);

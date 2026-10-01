#pragma once

/**
 * @file IOT_CoreHubRole.h
 * @brief iotcore public API for hub-role capability startup.
 *
 * Zigbee hub lifecycle, still owned by core.
 *
 * The MESH half of this header has moved out of core: playing a hub role is a
 * product decision, so the transport lifecycle belongs with the SDK that decides
 * it, and core keeps only the protocol. Zigbee has not been migrated yet and is
 * expected to get the same treatment.
 *
 * Whether a role is *played* is recorded separately, in IOT_CoreHubRoleFlags.h,
 * which the SDK writes and the protocol handlers read.
 */

#include <stdbool.h>
#include "IOT_ErrorManager.h"

/**
 * @brief Start the Zigbee hub service.
 *
 * - Initialises the Zigbee coordinator (IOT_ZigbeeCoordinatorInit).
 * - Registers internal callbacks that translate coordinator events into
 *   IOT_EVENT_DEVICE events:
 *     onJoined      → DEV_EVENT_ZIGBEE_DEVICE_JOINING
 *     onInterviewed → DEV_EVENT_ZIGBEE_DEVICE_INTERVIEWED (known)
 *                  or DEV_EVENT_ZIGBEE_DEVICE_UNKNOWN_MODEL (unknown)
 *     onLeft        → DEV_EVENT_ZIGBEE_DEVICE_LEFT
 *
 * Returns IOT_ERR_NOT_SUPPORTED if no coordinator driver is registered —
 * callers should treat this as a non-fatal skip.
 *
 * @return IOT_OK on success, IOT_ERR_NOT_SUPPORTED if no driver, or an
 *         error code from coordinator initialisation.
 */
iot_err_t IOT_CoreHubZigbeeStart(void);

/**
 * @brief Is this device playing the Zigbee hub role?
 *
 * True once the SDK has successfully called IOT_CoreHubZigbeeStart(). As with
 * the mesh role, this is the SDK's declaration of intent rather than
 * a platform capability — CONFIG_IOT_ZIGBEE_ENABLED only says the transport is
 * compiled in, which a non-hub can also have.
 */
bool IOT_CoreHubZigbeeIsActive(void);

/**
 * @brief Stop the Zigbee hub service.
 *
 * Calls IOT_ZigbeeCoordinatorDeinit(). Logs a warning and returns without
 * action if the Zigbee hub was never started.
 */
void IOT_CoreHubZigbeeStop(void);

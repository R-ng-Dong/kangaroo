#pragma once

/**
 * @file IOT_CoreHubRoleFlags.h
 * @brief Which hub roles the SDK has declared this device plays.
 *
 * Core answers the cloud when it asks whether this device can act as a mesh or
 * Zigbee hub, but it does not decide the answer. Whether a device acts as a hub is
 * an SDK/app-writer choice: a compiled-in transport says only that the radio
 * exists, and a plain mesh node has that without being a hub.
 *
 * So the SDK declares the role and core records it. Core learns nothing about how
 * mesh or Zigbee work, which is what lets the whole hub lifecycle live in the SDK.
 *
 * Declare the role at the point the SDK commits to playing it — not after the
 * transport is fully up. The cloud only sends mesh netKey/appKey once it believes
 * the device is a hub, and the device cannot join without them, so waiting for a
 * completed join before claiming the role deadlocks.
 */

#include <stdbool.h>

typedef enum
{
    IOT_HUB_ROLE_MESH = 0,
    IOT_HUB_ROLE_ZIGBEE = 1,

    IOT_HUB_ROLE_COUNT
} IOT_HubRole_t;

/**
 * @brief Declare (or withdraw) a hub role.
 *
 * Idempotent. Roles are independent — declaring one never affects another.
 * An out-of-range role is ignored.
 */
void IOT_CoreSetHubRole(IOT_HubRole_t role, bool active);

/**
 * @brief Does the SDK currently claim this hub role?
 */
bool IOT_CoreHasHubRole(IOT_HubRole_t role);

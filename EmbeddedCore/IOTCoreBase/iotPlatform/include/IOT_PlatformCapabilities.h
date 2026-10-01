#pragma once
#include <stdbool.h>
#include "IOT_ErrorManager.h"

/**
 * @file IOT_PlatformCapabilities.h
 * @brief Runtime health of platform-provided services.
 *
 * This answers exactly one kind of question: **is a platform service working
 * right now?** It deliberately does NOT answer either of these, which it used to
 * conflate and which caused real bugs:
 *
 *   "Is this feature compiled in?"
 *       Driver registration already answers this, and answers it truthfully.
 *       Every IOT_<Driver>* call forwards through a null-checked vtable and
 *       returns IOT_ERR_NOT_SUPPORTED when nothing is registered; see also
 *       IOT_BleIsAvailable(). A CONFIG_* check cannot see a driver that was
 *       compiled but failed to link — which is precisely how the ESP32 mesh
 *       driver stayed unregistered while a capability query reported it present.
 *
 *   "Does this device play the hub role?"
 *       That is the SDK's decision, declared with IOT_CoreSetHubRole() and read
 *       back with IOT_CoreHasHubRole() (IOT_CoreHubRoleFlags.h). A device can
 *       have a transport compiled in without playing a hub role at all.
 *
 * Only services with a genuine runtime answer belong here. Mesh, BLE and WiFi
 * were removed because on ESP32 they were plain #ifdefs dressed as runtime
 * queries, with no consumer that a truthful mechanism could not serve.
 */

/**
 * @brief Runtime health of a platform service.
 *
 * DOWN     — absent, or present but not responding. Do not answer cloud queries
 *            that depend on it.
 * DEGRADED — present but not fully responsive (e.g. the Z2M service is up but
 *            slow to answer). Answer the cloud, but log it.
 * UP       — present and responding.
 */
typedef enum
{
    IOT_SERVICE_DOWN     = 0,
    IOT_SERVICE_UP       = 1,
    IOT_SERVICE_DEGRADED = 2,
} IOT_ServiceHealth_t;

/**
 * @brief Platform services with a meaningful runtime health check.
 */
typedef enum
{
    /** Zigbee coordinator. On Linux this probes the zigbee2mqtt service, which
     *  can genuinely come and go while the firmware runs. */
    IOT_PLATFORM_SERVICE_ZIGBEE,
} IOT_PlatformService_t;

/**
 * @brief Query the current health of a platform service.
 *
 * @param service The service to query.
 * @return IOT_SERVICE_UP, IOT_SERVICE_DEGRADED, or IOT_SERVICE_DOWN.
 */
IOT_ServiceHealth_t IOT_PlatformGetServiceHealth(IOT_PlatformService_t service);

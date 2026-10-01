#include "IOT_PlatformCapabilities.h"

/*
 * ESP32 has no runtime Zigbee coordinator: there is no ESP32 coordinator
 * implementation at all, so IOT_ZigbeeCoordinatorInit() returns
 * IOT_ERR_NOT_SUPPORTED and IOT_CoreHubZigbeeStart() never marks the role active.
 * Reporting DOWN here is therefore the honest answer, not a placeholder.
 *
 * Mesh, BLE and WiFi used to be reported here as compile-time #ifdefs. They were
 * removed: "compiled in" is not runtime health, and driver registration already
 * answers it truthfully (see IOT_PlatformCapabilities.h).
 */
IOT_ServiceHealth_t IOT_PlatformGetServiceHealth(IOT_PlatformService_t service)
{
    switch (service)
    {
    case IOT_PLATFORM_SERVICE_ZIGBEE:
        return IOT_SERVICE_DOWN;

    default:
        return IOT_SERVICE_DOWN;
    }
}

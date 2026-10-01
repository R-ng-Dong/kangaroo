#include "IOT_ZigbeeCoordinator.h"
#include "IOT_ZigbeeCoordinatorDriver.h"
#include "IOT_Log.h"

static const char *TAG = "ZIGBEE_DRV";

static const IOT_ZigbeeCoordinatorDriver_t *s_driver = NULL;

void IOT_ZigbeeCoordinatorRegisterDriver(const IOT_ZigbeeCoordinatorDriver_t *driver)
{
    s_driver = driver;
    IOT_LOGI(TAG, "Zigbee coordinator driver registered");
}

iot_err_t IOT_ZigbeeCoordinatorInit(const IOT_ZigbeeCoordinatorCallbacks_t *cbs)
{
    if (s_driver == NULL || s_driver->init == NULL)
    {
        IOT_LOGD(TAG, "No zigbee coordinator driver registered");
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_driver->init(cbs);
}

iot_err_t IOT_ZigbeeCoordinatorDeinit(void)
{
    if (s_driver == NULL || s_driver->deinit == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->deinit();
}

iot_err_t IOT_ZigbeeCoordinatorOpenPairing(uint16_t gatewayEid, uint8_t timeSec,
                                            uint8_t mode, uint16_t deviceTypeFilter)
{
    if (s_driver == NULL || s_driver->openPairing == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->openPairing(gatewayEid, timeSec, mode, deviceTypeFilter);
}

iot_err_t IOT_ZigbeeCoordinatorClosePairing(uint16_t gatewayEid)
{
    if (s_driver == NULL || s_driver->closePairing == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->closePairing(gatewayEid);
}

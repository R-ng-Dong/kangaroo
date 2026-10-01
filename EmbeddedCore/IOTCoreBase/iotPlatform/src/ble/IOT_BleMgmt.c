#include "ble/IOT_BleMgmt.h"
#include "ble/IOT_BleDriver.h"
#include "IOT_Log.h"

static const char *TAG = "BLE_DRV";
static const IOT_BleDriver_t *s_driver = NULL;

void IOT_BleRegisterDriver(const IOT_BleDriver_t *driver)
{
    s_driver = driver;
    if (driver != NULL)
        IOT_LOGI(TAG, "BLE driver registered");
}

bool IOT_BleIsAvailable(void)
{
    return s_driver != NULL;
}

iot_err_t IOT_BleInit(void)
{
    if (s_driver == NULL || s_driver->init == NULL)
    {
        IOT_LOGD(TAG, "No BLE driver registered");
        return IOT_ERR_NOT_SUPPORTED;
    }
    return s_driver->init();
}

iot_err_t IOT_BleDeinit(void)
{
    if (s_driver == NULL || s_driver->deinit == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->deinit();
}

iot_err_t IOT_BleAddServices(const IOT_BleMgmtGattServ_t *service)
{
    if (s_driver == NULL || s_driver->addServices == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->addServices(service);
}

iot_err_t IOT_BleStartAdvertising(IOT_BleMgmtAdvData_t *adv_requirements)
{
    if (s_driver == NULL || s_driver->startAdvertising == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->startAdvertising(adv_requirements);
}

iot_err_t IOT_BleStopAdvertising(void)
{
    if (s_driver == NULL || s_driver->stopAdvertising == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->stopAdvertising();
}

iot_err_t IOT_BleStartBeaconAdvertising(IOT_BleMgmtAdvData_t *adv_requirements)
{
    if (s_driver == NULL || s_driver->startBeaconAdvertising == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->startBeaconAdvertising(adv_requirements);
}

iot_err_t IOT_BleStopBeaconAdvertising(void)
{
    if (s_driver == NULL || s_driver->stopBeaconAdvertising == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->stopBeaconAdvertising();
}

iot_err_t IOT_BleNotifyToCharacteristic(const char *addr, const uint8_t *charUuid,
                                         const uint8_t *data, uint16_t len)
{
    if (s_driver == NULL || s_driver->notifyToCharacteristic == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->notifyToCharacteristic(addr, charUuid, data, len);
}

iot_err_t IOT_BleIsConnected(bool *isConnected)
{
    if (s_driver == NULL || s_driver->isConnected == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->isConnected(isConnected);
}

iot_err_t IOT_BleDisconnect(void)
{
    if (s_driver == NULL || s_driver->disconnect == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->disconnect();
}

iot_err_t IOT_BleStartScan(uint32_t intervalTime)
{
    if (s_driver == NULL || s_driver->startScan == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->startScan(intervalTime);
}

iot_err_t IOT_BleStopScan(void)
{
    if (s_driver == NULL || s_driver->stopScan == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->stopScan();
}

iot_err_t IOT_BleSetDeviceName(const char *name)
{
    if (s_driver == NULL || s_driver->setDeviceName == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->setDeviceName(name);
}

iot_err_t IOT_BleRegisterEventCallback(IOT_BleMgmtEventStatusType_t event,
                                        IOT_BleEventCb_t cb, void *arg)
{
    if (s_driver == NULL || s_driver->registerEventCallback == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->registerEventCallback(event, cb, arg);
}

iot_err_t IOT_BleUnregisterEventCallback(IOT_BleMgmtEventStatusType_t event)
{
    if (s_driver == NULL || s_driver->unregisterEventCallback == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->unregisterEventCallback(event);
}

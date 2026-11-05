#include "ble/IOT_BleMgmt.h"
#include "ble/IOT_BleDriver.h"
#include "IOT_Log.h"
#include <string.h>

static const char *TAG = "BLE_DRV";
static const IOT_BleDriver_t *s_driver = NULL;
#define IOT_BLE_ADV_NAME_MAX_LEN 29U
static char s_advertising_name[IOT_BLE_ADV_NAME_MAX_LEN + 1U];

iot_err_t IOT_BleSetAdvertisingNameOverride(const char *name)
{
    if (NULL == name)
        return IOT_ERR_INVALID_ARG;
    size_t length = strlen(name);
    if (0U == length || IOT_BLE_ADV_NAME_MAX_LEN < length)
        return IOT_ERR_INVALID_ARG;
    memcpy(s_advertising_name, name, length + 1U);
    return IOT_OK;
}

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

iot_err_t IOT_BleStart(void)
{
    if (s_driver == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    /* A stack with no separate activation step (Bluedroid) leaves this NULL and is
     * already usable after init — that is success, not "unsupported". */
    if (s_driver->start == NULL)
        return IOT_OK;
    return s_driver->start();
}

iot_err_t IOT_BleStartAdvertising(IOT_BleMgmtAdvData_t *adv_requirements)
{
    if (s_driver == NULL || s_driver->startAdvertising == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    if (NULL == adv_requirements)
        return IOT_ERR_INVALID_ARG;
    if ('\0' != s_advertising_name[0])
    {
        IOT_BleMgmtAdvData_t advertising_data = *adv_requirements;
        advertising_data.name = s_advertising_name;
        advertising_data.nameLen = (uint8_t)strlen(s_advertising_name);
        return s_driver->startAdvertising(&advertising_data);
    }
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

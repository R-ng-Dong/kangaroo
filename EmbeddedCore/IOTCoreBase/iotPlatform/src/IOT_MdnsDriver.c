#include "IOT_MdnsMgmt.h"
#include "IOT_MdnsDriver.h"
#include "IOT_Log.h"

static const char *TAG = "MDNS_DRV";
static const IOT_MdnsDriver_t *s_driver = NULL;

void IOT_MdnsRegisterDriver(const IOT_MdnsDriver_t *driver)
{
    s_driver = driver;
    IOT_LOGI(TAG, "mDNS driver registered");
}

iot_err_t IOT_MdnsStartService(const char *hostname, const char *domain)
{
    if (s_driver == NULL || s_driver->startService == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->startService(hostname, domain);
}

iot_err_t IOT_MdnsStopService(void)
{
    if (s_driver == NULL || s_driver->stopService == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->stopService();
}

iot_err_t IOT_MdnsAddAdvertisement(char *serviceName, char *serviceType, uint16_t port)
{
    if (s_driver == NULL || s_driver->addAdvertisement == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->addAdvertisement(serviceName, serviceType, port);
}

iot_err_t IOT_MdnsRemoveAdvertisement(const char *serviceName)
{
    if (s_driver == NULL || s_driver->removeAdvertisement == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->removeAdvertisement(serviceName);
}

iot_err_t IOT_MdnsRemoveAllAdvertisements(void)
{
    if (s_driver == NULL || s_driver->removeAllAdvertisements == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->removeAllAdvertisements();
}

iot_err_t IOT_MdnsStartDiscovery(char *serviceType)
{
    if (s_driver == NULL || s_driver->startDiscovery == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->startDiscovery(serviceType);
}

iot_err_t IOT_MdnsStopDiscovery(void)
{
    if (s_driver == NULL || s_driver->stopDiscovery == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->stopDiscovery();
}

iot_err_t IOT_MdnsRegisterEventCallback(mdns_event_status_type_t eventType, mdns_event_callback_t cb, void *arg)
{
    if (s_driver == NULL || s_driver->registerEventCallback == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->registerEventCallback(eventType, cb, arg);
}

iot_err_t IOT_MdnsUnregisterEventCallback(mdns_event_status_type_t eventType)
{
    if (s_driver == NULL || s_driver->unregisterEventCallback == NULL)
        return IOT_ERR_NOT_SUPPORTED;
    return s_driver->unregisterEventCallback(eventType);
}

#pragma once

#include "IOT_MdnsMgmt.h"

typedef struct
{
    iot_err_t (*startService)(const char *hostname, const char *domain);
    iot_err_t (*stopService)(void);
    iot_err_t (*addAdvertisement)(char *serviceName, char *serviceType, uint16_t port);
    iot_err_t (*removeAdvertisement)(const char *serviceName);
    iot_err_t (*removeAllAdvertisements)(void);
    iot_err_t (*startDiscovery)(char *serviceType);
    iot_err_t (*stopDiscovery)(void);
    iot_err_t (*registerEventCallback)(mdns_event_status_type_t eventType, mdns_event_callback_t cb, void *arg);
    iot_err_t (*unregisterEventCallback)(mdns_event_status_type_t eventType);
} IOT_MdnsDriver_t;

void IOT_MdnsRegisterDriver(const IOT_MdnsDriver_t *driver);

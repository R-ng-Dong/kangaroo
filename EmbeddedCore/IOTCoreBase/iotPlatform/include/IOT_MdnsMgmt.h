#pragma once
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <IOT_ErrorManager.h>

typedef struct {
    char *name;
    char *type;
    char *inet4Address;
    uint16_t port;
} mdns_service_t; // universal minimum set of fields for mDNS service

typedef enum {
    MDNS_EVENT_ADVERTISEMENT_STARTED,
    MDNS_EVENT_ADVERTISEMENT_STOPPED,
    MDNS_EVENT_QUERY_STARTED,
    MDNS_EVENT_QUERY_STOPPED,
    MDNS_EVENT_SERVICE_FOUND,
    MDNS_EVENT_SERVICE_LOST
} mdns_event_status_type_t;

typedef void (*mdns_event_callback_t)(void *arg, mdns_event_status_type_t eventType, void *eventData);

iot_err_t IOT_MdnsStartService(const char *hostname, const char *domain);
iot_err_t IOT_MdnsStopService(void);
iot_err_t IOT_MdnsAddAdvertisement(char *serviceName, char *serviceType, uint16_t port);
iot_err_t IOT_MdnsRemoveAdvertisement(const char *serviceName);
iot_err_t IOT_MdnsRemoveAllAdvertisements(void);
iot_err_t IOT_MdnsStartDiscovery(char *serviceType);
iot_err_t IOT_MdnsStopDiscovery(void);
iot_err_t IOT_MdnsRegisterEventCallback(mdns_event_status_type_t eventType, mdns_event_callback_t cb, void *arg);
iot_err_t IOT_MdnsUnregisterEventCallback(mdns_event_status_type_t eventType);

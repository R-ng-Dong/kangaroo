#pragma once

#include "IOT_ErrorManager.h"
#include <inttypes.h>
#include <stdlib.h>

typedef enum
{
    TCP_SERVER_EVENT_STATUS_RECEIVED_DATA,
    TCP_SERVER_EVENT_STATUS_MAX
} IOT_TcpEventStatus_t;

typedef struct
{
    uint8_t *data;
    uint16_t dataLen;
} IOT_EventReceivedData_t;

typedef void (*IOT_TcpEventCb_t)(void *arg, IOT_TcpEventStatus_t eventType, void *eventData);

iot_err_t IOT_TcpInit(void);
iot_err_t IOT_TcpDeinit(void);
iot_err_t IOT_TcpGetServerPort(uint16_t *port);
iot_err_t IOT_TcpRegisterEventCb(IOT_TcpEventStatus_t eventType, IOT_TcpEventCb_t callback, void *userData);
iot_err_t IOT_TcpUnregisterEventCb(IOT_TcpEventStatus_t eventType);

/**
 * @brief Send data to the last accepted TCP client
 *
 * Creates a new TCP connection to the last client's IP at the specified port.
 * The client IP is stored from the most recent accept() call.
 *
 * @param destPort Destination port (from message header, client's listening port)
 * @param data Data to send
 * @param dataLen Data length
 * @return IOT_OK on success
 */
iot_err_t IOT_TcpSendData(uint16_t destPort, const uint8_t *data, size_t dataLen);

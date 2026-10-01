#include "IOT_TcpServer.h"
#include "IOT_Log.h"
#include "IOT_Common.h"

#include "lwip/sockets.h"
#include "esp_netif_ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <errno.h>

static const char *TAG = "IOT_TCP";

#define TCP_RX_BUF_SIZE       1024
#define TCP_SERVER_TASK_STACK 4096
#define TCP_SERVER_TASK_PRIO  5
#define TCP_KEEPALIVE_IDLE    5
#define TCP_KEEPALIVE_INTVL   5
#define TCP_KEEPALIVE_CNT     3
#define TCP_ACK_BYTE          0x01

static uint16_t     s_serverPort     = 0;
static int          s_listenSock     = -1;
static TaskHandle_t s_serverTask     = NULL;
static volatile bool s_running       = false;
static char         s_lastClientAddr[16] = {0};
static IOT_TcpEventCb_t  s_eventCb   = NULL;
static void             *s_eventCbArg = NULL;

static void handleClientConnection(int clientSock)
{
    uint8_t rxBuf[TCP_RX_BUF_SIZE];
    while (s_running)
    {
        int len = recv(clientSock, rxBuf, sizeof(rxBuf), 0);
        if (len <= 0)
        {
            if (len < 0)
            {
                IOT_LOGE(TAG, "recv error: %d", errno);
            }
            break;
        }
        uint8_t ack = TCP_ACK_BYTE;
        send(clientSock, &ack, 1, 0);
        if (s_eventCb != NULL)
        {
            IOT_EventReceivedData_t eventData = {
                .data    = rxBuf,
                .dataLen = (uint16_t)len,
            };
            s_eventCb(s_eventCbArg, TCP_SERVER_EVENT_STATUS_RECEIVED_DATA, &eventData);
        }
    }
}

static void tcpServerTask(void *pvParameters)
{
    (void)pvParameters;
    IOT_LOGI(TAG, "TCP server listening on port %d", s_serverPort);
    while (s_running)
    {
        struct sockaddr_in clientAddr;
        socklen_t          addrLen = sizeof(clientAddr);
        int clientSock = accept(s_listenSock, (struct sockaddr *)&clientAddr, &addrLen);
        if (clientSock < 0)
        {
            if (!s_running)
            {
                break;
            }
            IOT_LOGE(TAG, "accept error: %d", errno);
            continue;
        }
        inet_ntoa_r(clientAddr.sin_addr, s_lastClientAddr, sizeof(s_lastClientAddr));
        IOT_LOGD(TAG, "Client connected from %s", s_lastClientAddr);
        int keepAlive    = 1;
        int keepIdle     = TCP_KEEPALIVE_IDLE;
        int keepInterval = TCP_KEEPALIVE_INTVL;
        int keepCount    = TCP_KEEPALIVE_CNT;
        setsockopt(clientSock, SOL_SOCKET,  SO_KEEPALIVE,  &keepAlive,    sizeof(keepAlive));
        setsockopt(clientSock, IPPROTO_TCP, TCP_KEEPIDLE,  &keepIdle,     sizeof(keepIdle));
        setsockopt(clientSock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(keepInterval));
        setsockopt(clientSock, IPPROTO_TCP, TCP_KEEPCNT,   &keepCount,    sizeof(keepCount));
        handleClientConnection(clientSock);
        close(clientSock);
    }
    s_serverTask = NULL;
    vTaskDelete(NULL);
}

iot_err_t IOT_TcpInit(void)
{
    if (s_running)
    {
        IOT_LOGW(TAG, "TCP server already running");
        return IOT_OK;
    }
    s_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s_listenSock < 0)
    {
        IOT_LOGE(TAG, "Failed to create socket: %d", errno);
        return IOT_ERR_FAIL;
    }
    int opt = 1;
    setsockopt(s_listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in serverAddr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = 0,
    };
    if (bind(s_listenSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0)
    {
        IOT_LOGE(TAG, "Failed to bind: %d", errno);
        close(s_listenSock);
        s_listenSock = -1;
        return IOT_ERR_FAIL;
    }
    struct sockaddr_in boundAddr;
    socklen_t          addrLen = sizeof(boundAddr);
    if (getsockname(s_listenSock, (struct sockaddr *)&boundAddr, &addrLen) != 0)
    {
        IOT_LOGE(TAG, "getsockname failed: %d", errno);
        close(s_listenSock);
        s_listenSock = -1;
        return IOT_ERR_FAIL;
    }
    s_serverPort = ntohs(boundAddr.sin_port);
    if (listen(s_listenSock, 1) != 0)
    {
        IOT_LOGE(TAG, "listen failed: %d", errno);
        close(s_listenSock);
        s_listenSock = -1;
        s_serverPort = 0;
        return IOT_ERR_FAIL;
    }
    s_running = true;
    memset(s_lastClientAddr, 0, sizeof(s_lastClientAddr));
    if (xTaskCreate(tcpServerTask, "tcp_server", TCP_SERVER_TASK_STACK, NULL,
                    TCP_SERVER_TASK_PRIO, &s_serverTask) != pdPASS)
    {
        IOT_LOGE(TAG, "Failed to create TCP server task");
        s_running = false;
        close(s_listenSock);
        s_listenSock = -1;
        s_serverPort = 0;
        return IOT_ERR_FAIL;
    }
    IOT_LOGI(TAG, "TCP server init on port %d", s_serverPort);
    return IOT_OK;
}

iot_err_t IOT_TcpDeinit(void)
{
    if (!s_running)
    {
        return IOT_OK;
    }
    s_running = false;
    if (s_listenSock >= 0)
    {
        close(s_listenSock);
        s_listenSock = -1;
    }
    for (int i = 0; i < 20 && s_serverTask != NULL; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    s_eventCb    = NULL;
    s_eventCbArg = NULL;
    s_serverPort = 0;
    IOT_LOGI(TAG, "TCP server stopped");
    return IOT_OK;
}

iot_err_t IOT_TcpGetServerPort(uint16_t *port)
{
    if (port == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    *port = s_serverPort;
    return IOT_OK;
}

iot_err_t IOT_TcpRegisterEventCb(IOT_TcpEventStatus_t eventType, IOT_TcpEventCb_t callback, void *userData)
{
    if (eventType >= TCP_SERVER_EVENT_STATUS_MAX || callback == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    s_eventCb    = callback;
    s_eventCbArg = userData;
    return IOT_OK;
}

iot_err_t IOT_TcpUnregisterEventCb(IOT_TcpEventStatus_t eventType)
{
    if (eventType >= TCP_SERVER_EVENT_STATUS_MAX)
    {
        return IOT_ERR_INVALID_ARG;
    }
    s_eventCb    = NULL;
    s_eventCbArg = NULL;
    return IOT_OK;
}

iot_err_t IOT_TcpSendData(uint16_t destPort, const uint8_t *data, size_t dataLen)
{
    if (data == NULL || dataLen == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }
    if (s_lastClientAddr[0] == '\0')
    {
        IOT_LOGE(TAG, "No client address stored");
        return IOT_ERR_FAIL;
    }
    if (destPort == 0)
    {
        IOT_LOGE(TAG, "Invalid destination port");
        return IOT_ERR_INVALID_ARG;
    }
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0)
    {
        IOT_LOGE(TAG, "Failed to create send socket: %d", errno);
        return IOT_ERR_FAIL;
    }
    struct sockaddr_in destAddr = {
        .sin_family = AF_INET,
        .sin_port   = htons(destPort),
    };
    destAddr.sin_addr.s_addr = inet_addr(s_lastClientAddr);
    iot_err_t result = IOT_OK;
    if (connect(sock, (struct sockaddr *)&destAddr, sizeof(destAddr)) != 0)
    {
        IOT_LOGE(TAG, "Failed to connect to %s:%d: %d", s_lastClientAddr, destPort, errno);
        result = IOT_ERR_FAIL;
        goto send_done;
    }
    size_t totalSent = 0;
    while (totalSent < dataLen)
    {
        int sent = send(sock, data + totalSent, dataLen - totalSent, 0);
        if (sent < 0)
        {
            IOT_LOGE(TAG, "send error: %d", errno);
            result = IOT_ERR_FAIL;
            goto send_done;
        }
        totalSent += sent;
    }
    IOT_LOGD(TAG, "Sent %zu bytes to %s:%d", dataLen, s_lastClientAddr, destPort);
send_done:
    close(sock);
    return result;
}

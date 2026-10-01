#include "network/IOT_NetworkMgmt.h"
#include "IOT_Log.h"
#include "sdkconfig.h"
// Interface for managing network interfaces in IoT devices
#if CONFIG_IOT_ETHERNET_ENABLED
#include "network/ethernet.h"
#endif
#include "network/wifi.h"
#include "IOT_Common.h"
#include <stdbool.h>
#include <stdio.h>

#define NWK_MAX_CBS_PER_EVENT 3

typedef struct
{
    IOT_NwkEventCb cb;
    void *arg;
    bool isRegistered;
} nwk_cb_slot_t;

typedef struct
{
    nwk_cb_slot_t slots[NWK_MAX_CBS_PER_EVENT];
} nwk_cb_mgmt_t;

static nwk_cb_mgmt_t *wifiIfaceEventCallbackMgmts = NULL;
#if CONFIG_IOT_ETHERNET_ENABLED
static nwk_cb_mgmt_t *ethIfaceEventCallbackMgmts = NULL;
#endif

static void fireNwkEventCbs(nwk_cb_mgmt_t *mgmt, IOT_NwkEventStatus_t eventType,
                            IOT_NetworkIface iface, void *eventData)
{
    if (mgmt == NULL)
        return;
    for (int i = 0; i < NWK_MAX_CBS_PER_EVENT; i++)
    {
        nwk_cb_slot_t *s = &mgmt[eventType].slots[i];
        if (s->isRegistered && s->cb != NULL)
        {
            s->cb(iface, s->arg, eventType, eventData);
        }
    }
}

static void internalWifiEventHandler(void *arg, wifi_event_status_t eventType, void *event_data)
{
    if (wifiIfaceEventCallbackMgmts == NULL)
    {
        return;
    }

    IOT_NetworkIface iface = {.type = WIFI, .number = 0};

    switch (eventType)
    {
    case WIFI_EVENT_STATUS_DISCONNECTED:
    {
        wifi_mgmt_event_disconnected_data_t *wifiDisconnectedData = (wifi_mgmt_event_disconnected_data_t *) event_data;

        IOT_NwkEventDisconnectedData_t disconnectedData = {0};
        memcpy(disconnectedData.ssid, wifiDisconnectedData->ssid, sizeof(wifiDisconnectedData->ssid));
        disconnectedData.ssid_len = wifiDisconnectedData->ssid_len;
        memcpy(disconnectedData.bssid, wifiDisconnectedData->bssid, sizeof(wifiDisconnectedData->bssid));
        disconnectedData.rssi = wifiDisconnectedData->rssi;
        disconnectedData.reason = wifiDisconnectedData->reason;
        fireNwkEventCbs(wifiIfaceEventCallbackMgmts, IOT_NWK_EVENT_STATUS_DISCONNECTED, iface, &disconnectedData);
        break;
    }
    case WIFI_EVENT_STATUS_CONNECTING:
    {
        fireNwkEventCbs(wifiIfaceEventCallbackMgmts, IOT_NWK_EVENT_STATUS_CONNECTING, iface, NULL);
        break;
    }
    case WIFI_EVENT_STATUS_CONNECTED_NO_IP:
        break;
    case WIFI_EVENT_STATUS_GOT_IP:
    {
        IOT_LOGI("iot_nwk", "WiFi got IP event received");
        iot_wifi_ip_event_got_ip_data_t *wifiConnectedData = (iot_wifi_ip_event_got_ip_data_t *) event_data;
        IOT_NwkEventConnectedData_t connectedData = {0};
        memcpy(&connectedData, wifiConnectedData, sizeof(iot_wifi_ip_event_got_ip_data_t));
        fireNwkEventCbs(wifiIfaceEventCallbackMgmts, IOT_NWK_EVENT_STATUS_CONNECTED, iface, &connectedData);
        break;
    }
    case WIFI_EVENT_STATUS_CONNECTED:
        break;
    case WIFI_EVENT_STATUS_WRONG_PASSWORD:
        break;
    case WIFI_EVENT_STATUS_NO_AP_FOUND:
        break;
    case WIFI_EVENT_STATUS_AUTH_FAIL:
        break;
    case WIFI_EVENT_STATUS_UNKNOWN_ERROR:
        break;
    default:
        IOT_LOGE("iot_wifi", "Unknown WiFi event type: %d", eventType);
        break;
    }
}

#if CONFIG_IOT_ETHERNET_ENABLED
static void internalEthEventHandler(void *arg, eth_event_status_t eventType, void *eventData)
{
    if (ethIfaceEventCallbackMgmts == NULL)
    {
        return;
    }

    switch (eventType)
    {
    case ETH_EVENT_STATUS_UNINITIALIZED:
        break;
    case ETH_EVENT_STATUS_INITIALIZED:
        break;
    case ETH_EVENT_STATUS_LINK_UP:
        break;
    case ETH_EVENT_STATUS_LINK_DOWN:
        break;
    case ETH_EVENT_STATUS_RX_PACKET:
        break;
    case ETH_EVENT_STATUS_TX_COMPLETE:
        break;
    case ETH_EVENT_STATUS_ERROR:
        break;
    case ETH_EVENT_STATUS_PHY_STATUS_CHANGE:
        break;
    default:
        IOT_LOGE("iot_eth", "Unknown Ethernet event type: %d", eventType);
        break;
    }
}
#endif

iot_err_t IOT_NwkInit(IOT_NetworkIface *ifaces)
{
    if (ifaces == NULL)
    {
        IOT_LOGE("iot_iface", "No network interfaces provided for initialization");
        return IOT_ERR_INVALID_ARG;
    }
    uint8_t index = 0;
    iot_err_t res = IOT_OK;
    while (ifaces[index].type != NETWORK_TYPE_NONE)
    {
        switch (ifaces[index].type)
        { // TODO: Add 2 dimensional array to support multiple interfaces
        case WIFI:
            res = IOT_WifiInit(ifaces[index].number);
            if (res != IOT_OK)
            {
                IOT_LOGE("iot_iface", "Failed to initialize WiFi interface number %d", ifaces[index].number);
                return res;
            }
            IOT_LOGI("iot_iface", "WiFi interface number %d initialized successfully", ifaces[index].number);
            wifiIfaceEventCallbackMgmts = (nwk_cb_mgmt_t *) Mem_SafeCalloc(
                IOT_NWK_EVENT_STATUS_MAX + 1, sizeof(nwk_cb_mgmt_t), "iot_iface", "wifi event callbacks");
            if (wifiIfaceEventCallbackMgmts == NULL)
            {
                IOT_LOGE("iot_iface", "Failed to allocate memory for network event callbacks");
                return IOT_ERR_NO_MEM;
            }
            res = IOT_WifiRegisterEventCb(ifaces[index].number, WIFI_EVENT_STATUS_ALL, internalWifiEventHandler, NULL);
            if (res != IOT_OK)
            {
                IOT_LOGE("iot_iface", "Failed to register WiFi event handler");
                SAFE_FREE(wifiIfaceEventCallbackMgmts);
                return res;
            }
            break;
#if CONFIG_IOT_ETHERNET_ENABLED
        case ETHERNET:
            res = IOT_EthInit(&ifaces[index].number);
            if (res != IOT_OK)
            {
                IOT_LOGE("iot_iface", "Failed to initialize Ethernet interface number %d", ifaces[index].number);
                return res;
            }
            IOT_LOGI("iot_iface", "Ethernet interface number %d initialized successfully", ifaces[index].number);
            ethIfaceEventCallbackMgmts = (nwk_cb_mgmt_t *) Mem_SafeCalloc(
                IOT_NWK_EVENT_STATUS_MAX + 1, sizeof(nwk_cb_mgmt_t), "iot_iface", "eth event callbacks");
            if (ethIfaceEventCallbackMgmts == NULL)
            {
                IOT_LOGE("iot_iface", "Failed to allocate memory for network event callbacks");
                return IOT_ERR_NO_MEM;
            }
            res = IOT_EthRegisterEventCb(ifaces[index].number, ETH_EVENT_STATUS_ALL, internalEthEventHandler, NULL);
            if (res != IOT_OK)
            {
                IOT_LOGE("iot_iface", "Failed to register Ethernet event handler");
                return res;
            }
            break;
#endif
        default:
            IOT_LOGE("iot_iface", "Unsupported network interface type: %d", ifaces[index].type);
            return IOT_ERR_NOT_SUPPORTED;
        }
        index++;
    }
    IOT_LOGI("iot_iface", "Network interface(s) initialized successfully");
    return IOT_OK;
}

iot_err_t IOT_NwkDeinit(IOT_NetworkIface *ifaces)
{
    if (ifaces == NULL)
    {
        IOT_LOGE("iot_iface", "No network interfaces provided for deinitialization");
        return IOT_ERR_INVALID_ARG;
    }

    uint8_t index = 0;
    iot_err_t res = IOT_OK;

    while (ifaces[index].type != NETWORK_TYPE_NONE)
    {
        switch (ifaces[index].type)
        {
        case WIFI:
        {
            res = IOT_WifiDeinit(ifaces[index].number);
            if (res != IOT_OK)
            {
                IOT_LOGE("iot_iface", "Failed to deinitialize WiFi interface %d", ifaces[index].number);
                return res;
            }
            SAFE_FREE(wifiIfaceEventCallbackMgmts);
            break;
        }

#if CONFIG_IOT_ETHERNET_ENABLED
        case ETHERNET:
        {
            res = IOT_EthDeinit(ifaces[index].number);
            if (res != IOT_OK)
            {
                IOT_LOGE("iot_iface", "Failed to deinitialize Ethernet interface %d", ifaces[index].number);
                return res;
            }
            SAFE_FREE(ethIfaceEventCallbackMgmts);
            break;
        }
#endif

        default:
            IOT_LOGE("iot_iface", "Unsupported network interface type: %d", ifaces[index].type);
            return IOT_ERR_NOT_SUPPORTED;
        }
        index++;
    }

    IOT_LOGI("iot_iface", "Network interface(s) de-initialized successfully");
    return IOT_OK;
}

iot_err_t IOT_NwkScanWifi(IOT_NetworkIface iface, uint8_t interval, wifi_scan_res_require_t *res, uint8_t *numOfResults)
{
    if (iface.type != WIFI)
    {
        return IOT_ERR_FAIL;
    }
    uint8_t numOfDevices = (*numOfResults > 0) ? *numOfResults : 15;
    iot_ap_info_t *scanRes = IOT_WifiScan(iface.number, &numOfDevices, interval);
    if (scanRes == NULL)
    {
        IOT_LOGE("iot_iface", "Failed to scan WiFi networks on interface number %d", iface.number);
        return IOT_ERR_FAIL;
    }
    IOT_LOGI("iot_iface", "Found %d WiFi networks on interface number %d", numOfDevices, iface.number);
    for (uint8_t i = 0; i < numOfDevices; i++)
    {
        res[i].ssid = Mem_SafeStrdup(scanRes[i].ssid, "iot_iface", "scan ssid");
        if (res[i].ssid == NULL)
        {
            IOT_LOGE("iot_iface", "Failed to allocate memory for SSID of scan result %d", i + 1);
            // Free previously allocated SSIDs
            for (uint8_t j = 0; j < i; j++)
            {
                SAFE_FREE(res[j].ssid);
            }
            SAFE_FREE(scanRes);
            return IOT_ERR_NO_MEM;
        }
        res[i].authType = (uint8_t) scanRes[i].authmode;
        res[i].minusRssi = (uint8_t) (-scanRes[i].rssi);
        /* Convert WiFi channel to frequency (MHz) */
        uint8_t ch = scanRes[i].primary;
        if (ch >= 1 && ch <= 13)
        {
            res[i].freq = 2412 + (ch - 1) * 5; /* 2.4 GHz band */
        }
        else if (ch == 14)
        {
            res[i].freq = 2484;
        }
        else if (ch >= 36)
        {
            res[i].freq = 5000 + ch * 5; /* 5 GHz band */
        }
        else
        {
            res[i].freq = 0;
        }
    }
    *numOfResults = numOfDevices;
    SAFE_FREE(scanRes);
    return IOT_OK;
}
iot_err_t IOT_NwkConnectWifi(IOT_NetworkIface *ifaces, char *ssid, char *pwd, uint8_t retryTimes)
{
    if (ifaces == NULL || ssid == NULL || pwd == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    uint8_t index = 0;
    iot_err_t res = IOT_OK;

    while (ifaces[index].type != NETWORK_TYPE_NONE)
    {
        if (ifaces[index].type != WIFI)
        {
            index++;
            continue;
        }
        res = IOT_WifiConnect(ifaces[index].number, ssid, pwd, retryTimes);
        if (res != IOT_OK)
        {
            IOT_LOGE("iot_nwk", "Failed to connect WiFi on interface number %d", ifaces[index].number);
            return res;
        }
        index++;
    }
    return IOT_OK;
}

iot_err_t IOT_NwkStop(IOT_NetworkIface *ifaces)
{
    // TODO: Implement network stop
    return IOT_OK;
}

iot_err_t IOT_NwkGetMac(IOT_NetworkIface iface, uint8_t *mac)
{
    if (iface.type != WIFI || iface.number != 0)
    {
        IOT_LOGW("iot_nwk", "interface type or number is not supported");
        return IOT_ERR_INVALID_ARG;
    }
    return IOT_WifiGetMac(mac);
}

iot_err_t IOT_NwkRegisterEventCb(IOT_NetworkIface nwkInf, IOT_NwkEventStatus_t eventType, IOT_NwkEventCb cb, void *arg)
{
    if (cb == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    nwk_cb_mgmt_t *mgmt = NULL;
    const char *ifTag = NULL;

    if (nwkInf.type == WIFI)
    {
        if (nwkInf.number != 0)
        {
            IOT_LOGE("iot_wifi", "Invalid interface number: %d", nwkInf.number);
            return IOT_ERR_INVALID_ARG;
        }
        if (wifiIfaceEventCallbackMgmts == NULL)
        {
            IOT_LOGE("iot_wifi", "WiFi interface not initialized");
            return IOT_ERR_NOT_INITIALIZED;
        }
        mgmt = wifiIfaceEventCallbackMgmts;
        ifTag = "iot_wifi";
    }
#if CONFIG_IOT_ETHERNET_ENABLED
    else if (nwkInf.type == ETHERNET)
    {
        if (nwkInf.number != 0)
        {
            IOT_LOGE("iot_eth", "Invalid interface number: %d", nwkInf.number);
            return IOT_ERR_INVALID_ARG;
        }
        if (ethIfaceEventCallbackMgmts == NULL)
        {
            IOT_LOGE("iot_eth", "Ethernet interface not initialized");
            return IOT_ERR_NOT_INITIALIZED;
        }
        mgmt = ethIfaceEventCallbackMgmts;
        ifTag = "iot_eth";
    }
#endif
    else if (nwkInf.type == MOBILE_3G_4G)
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
    else
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (eventType <= IOT_NWK_EVENT_STATUS_NONE || eventType >= IOT_NWK_EVENT_STATUS_MAX)
    {
        IOT_LOGE(ifTag, "Invalid event type: %d", eventType);
        return IOT_ERR_INVALID_ARG;
    }

    for (int i = 0; i < NWK_MAX_CBS_PER_EVENT; i++)
    {
        nwk_cb_slot_t *s = &mgmt[eventType].slots[i];
        if (!s->isRegistered)
        {
            s->cb = cb;
            s->arg = arg;
            s->isRegistered = true;
            return IOT_OK;
        }
    }

    IOT_LOGW(ifTag, "No free slot for event %d (max %d) — increase NWK_MAX_CBS_PER_EVENT", eventType,
             NWK_MAX_CBS_PER_EVENT);
    return IOT_ERR_NO_MEM;
}

iot_err_t IOT_NwkGetConnectionStatus(IOT_NetworkIface iface, IOT_NwkEventStatus_t *status)
{
    if (status == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    if (iface.type == WIFI)
    {
        wifi_event_status_t wifiStatus = IOT_WifiGetConnectionStatus(iface.number);
        switch (wifiStatus)
        {
        case WIFI_EVENT_STATUS_GOT_IP:
        case WIFI_EVENT_STATUS_CONNECTED:
            *status = IOT_NWK_EVENT_STATUS_CONNECTED;
            break;
        case WIFI_EVENT_STATUS_CONNECTING:
        case WIFI_EVENT_STATUS_CONNECTED_NO_IP:
            *status = IOT_NWK_EVENT_STATUS_CONNECTING;
            break;
        case WIFI_EVENT_STATUS_DISCONNECTED:
        case WIFI_EVENT_STATUS_WRONG_PASSWORD:
        case WIFI_EVENT_STATUS_NO_AP_FOUND:
        case WIFI_EVENT_STATUS_AUTH_FAIL:
        case WIFI_EVENT_STATUS_UNKNOWN_ERROR:
            *status = IOT_NWK_EVENT_STATUS_DISCONNECTED;
            break;
        default:
            *status = IOT_NWK_EVENT_STATUS_NONE;
            break;
        }
        return IOT_OK;
    }
#if CONFIG_IOT_ETHERNET_ENABLED
    else if (iface.type == ETHERNET)
    {
        eth_event_status_t ethStatus = IOT_EthGetConnectionStatus(iface.number);
        switch (ethStatus)
        {
        case ETH_EVENT_STATUS_LINK_UP:
            *status = IOT_NWK_EVENT_STATUS_CONNECTED;
            break;
        case ETH_EVENT_STATUS_LINK_DOWN:
        case ETH_EVENT_STATUS_INITIALIZED:
        case ETH_EVENT_STATUS_ERROR:
            *status = IOT_NWK_EVENT_STATUS_DISCONNECTED;
            break;
        case ETH_EVENT_STATUS_UNINITIALIZED:
            *status = IOT_NWK_EVENT_STATUS_NONE;
            break;
        default:
            *status = IOT_NWK_EVENT_STATUS_NONE;
            break;
        }
        return IOT_OK;
    }
#endif
    else
    {
        return IOT_ERR_NOT_SUPPORTED;
    }
}

iot_err_t IOT_NwkGetConnectivities(IOT_NetworkConnectivity_t *connectivities, uint8_t *numOfConnectivities)
{
    if (connectivities == NULL || numOfConnectivities == NULL || *numOfConnectivities == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    uint8_t count = 0;
    IOT_NetworkIface wifiIface = {.type = WIFI, .number = 0};
    IOT_NwkEventStatus_t wifiStatus = IOT_NWK_EVENT_STATUS_NONE;
    if (IOT_NwkGetConnectionStatus(wifiIface, &wifiStatus) == IOT_OK)
    {
        IOT_NwkEventConnectedData_t ipInfo = {0};
        bool isConnected = wifiStatus == IOT_NWK_EVENT_STATUS_CONNECTED;
        bool hasIpv4 = isConnected && IOT_NwkGetIpInfo(wifiIface, &ipInfo) == IOT_OK;
        connectivities[count++] = (IOT_NetworkConnectivity_t) {
            .iface = wifiIface,
            .status = (uint8_t) wifiStatus,
            .auth = isConnected ? IOT_NwkGetAuthMode(wifiIface) : 0,
            .strength = isConnected ? (uint8_t) IOT_NwkGetRssi(wifiIface) : 0,
            .hasIpv4 = hasIpv4,
            .ipv4 = hasIpv4 ? ipInfo.ip : 0,
        };
    }

#if CONFIG_IOT_ETHERNET_ENABLED
    if (count < *numOfConnectivities)
    {
        IOT_NetworkIface ethIface = {.type = ETHERNET, .number = 0};
        IOT_NwkEventStatus_t ethStatus = IOT_NWK_EVENT_STATUS_NONE;
        if (IOT_NwkGetConnectionStatus(ethIface, &ethStatus) == IOT_OK)
        {
            connectivities[count++] = (IOT_NetworkConnectivity_t) {
                .iface = ethIface,
                .status = (uint8_t) ethStatus,
                .auth = 0,
                .strength = 100,
                .hasIpv4 = false,
                .ipv4 = 0,
            };
        }
    }
#endif

    *numOfConnectivities = count;
    return IOT_OK;
}

iot_err_t IOT_NwkUnregisterEventCb(IOT_NetworkIface nwkInf, IOT_NwkEventStatus_t eventType, IOT_NwkEventCb cb)
{
    nwk_cb_mgmt_t *mgmt = NULL;

    if (nwkInf.type == WIFI)
    {
        if (nwkInf.number != 0)
            return IOT_ERR_INVALID_ARG;
        mgmt = wifiIfaceEventCallbackMgmts;
    }
#if CONFIG_IOT_ETHERNET_ENABLED
    else if (nwkInf.type == ETHERNET)
    {
        if (nwkInf.number != 0)
            return IOT_ERR_INVALID_ARG;
        mgmt = ethIfaceEventCallbackMgmts;
    }
#endif
    else
    {
        return IOT_ERR_NOT_SUPPORTED;
    }

    if (mgmt == NULL)
        return IOT_OK;

    if (eventType <= IOT_NWK_EVENT_STATUS_NONE || eventType >= IOT_NWK_EVENT_STATUS_MAX)
        return IOT_ERR_INVALID_ARG;

    for (int i = 0; i < NWK_MAX_CBS_PER_EVENT; i++)
    {
        nwk_cb_slot_t *s = &mgmt[eventType].slots[i];
        if (s->isRegistered && s->cb == cb)
        {
            s->cb = NULL;
            s->arg = NULL;
            s->isRegistered = false;
            return IOT_OK;
        }
    }

    return IOT_OK;
}

void IOT_NwkSetAutoReconnect(IOT_NetworkIface iface, bool enable)
{
    if (iface.type == WIFI)
    {
        IOT_WifiSetAutoReconnect(enable);
    }
}

bool IOT_NwkGetAutoReconnect(IOT_NetworkIface iface)
{
    if (iface.type == WIFI)
    {
        return IOT_WifiGetAutoReconnect();
    }
    return false;
}

uint8_t IOT_NwkGetAuthMode(IOT_NetworkIface iface)
{
    if (iface.type == WIFI)
        return (uint8_t) IOT_WifiGetAuth();
    return 0;
}

int8_t IOT_NwkGetRssi(IOT_NetworkIface iface)
{
    if (iface.type == WIFI)
        return IOT_WifiGetRssi();
    return 0;
}

iot_err_t IOT_NwkGetIpInfo(IOT_NetworkIface iface, IOT_NwkEventConnectedData_t *ipInfo)
{
    if (ipInfo == NULL)
        return IOT_ERR_INVALID_ARG;
    if (iface.type == WIFI)
    {
        IOT_WifiIpInfo_t wifiIp = {0};
        iot_err_t err = IOT_WifiGetIpInfo(iface.number, &wifiIp);
        if (err != IOT_OK)
            return err;
        ipInfo->ip = wifiIp.ip;
        ipInfo->netmask = wifiIp.netmask;
        ipInfo->gateway = wifiIp.gateway;
        return IOT_OK;
    }
    return IOT_ERR_NOT_SUPPORTED;
}

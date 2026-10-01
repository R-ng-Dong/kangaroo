#pragma once
#include "IOT_ErrorManager.h"
#include <inttypes.h>
#include <stdbool.h>

typedef struct
{
    int status;    // Status of the interface (e.g., connected, disconnected)
    char ssid[32]; // SSID of the connected network
} iface_info_t;

/**
 * @brief network interface types.
 *
 * This enum defines the different types of network interfaces, such as WiFi or Ethernet, ...
 */

typedef enum
{
    NETWORK_TYPE_NONE = 0,  // End-of-array marker
    ETHERNET = 1,
    WIFI = 2,
    MOBILE_3G_4G = 6
} IOT_NetworkIfaceType;

typedef struct
{
    IOT_NetworkIfaceType type;
    uint8_t number;
} IOT_NetworkIface;

// typedef struct
// {
//     IOT_WifiEventCb handler;
//     void *arg;
//     iot_event_base_t event_base;
//     int32_t event_id;
// } iot_event_holder_t;

typedef enum
{
    IOT_NWK_EVENT_STATUS_NONE, //
    IOT_NWK_EVENT_STATUS_CONNECTING,
    IOT_NWK_EVENT_STATUS_CONNECTED, // got ip
    IOT_NWK_EVENT_STATUS_DISCONNECTING,
    IOT_NWK_EVENT_STATUS_DISCONNECTED,
    IOT_NWK_EVENT_STATUS_LINKLOSS,
    IOT_NWK_EVENT_STATUS_CLOSE,
    IOT_NWK_EVENT_STATUS_RETRY_CONNECTING,
    IOT_NWK_EVENT_STATUS_MAX
} IOT_NwkEventStatus_t;

typedef enum {
    IOT_NWK_ERR_REASON_UNSPECIFIED = 1,
    IOT_NWK_ERR_REASON_AUTH_EXPIRE = 2,
    IOT_NWK_ERR_REASON_AUTH_LEAVE = 3,
    IOT_NWK_ERR_REASON_ASSOC_EXPIRE = 4,
    IOT_NWK_ERR_REASON_ASSOC_TOOMANY = 5,
    IOT_NWK_ERR_REASON_NOT_AUTHED = 6,
    IOT_NWK_ERR_REASON_NOT_ASSOCED = 7,
    IOT_NWK_ERR_REASON_ASSOC_LEAVE = 8,
    IOT_NWK_ERR_REASON_ASSOC_NOT_AUTHED = 9,
    IOT_NWK_ERR_REASON_DISASSOC_PWRCAP_BAD = 10,
    IOT_NWK_ERR_REASON_DISASSOC_SUPCHAN_BAD = 11,
    IOT_NWK_ERR_REASON_IE_INVALID = 13,
    IOT_NWK_ERR_REASON_MIC_FAILURE = 14,
    IOT_NWK_ERR_REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
    IOT_NWK_ERR_REASON_GROUP_KEY_UPDATE_TIMEOUT = 16,
    IOT_NWK_ERR_REASON_IE_IN_4WAY_DIFFERS = 17,
    IOT_NWK_ERR_REASON_GROUP_CIPHER_INVALID = 18,
    IOT_NWK_ERR_REASON_PAIRWISE_CIPHER_INVALID = 19,
    IOT_NWK_ERR_REASON_AKMP_INVALID = 20,
    IOT_NWK_ERR_REASON_UNSUPP_RSN_IE_VERSION = 21,
    IOT_NWK_ERR_REASON_INVALID_RSN_IE_CAPABILITIES = 22,
    IOT_NWK_ERR_REASON_IEEE_802_1X_AUTH_FAILED = 23,
    IOT_NWK_ERR_REASON_CIPHER_SUITE_REJECTED = 24,
    IOT_NWK_ERR_REASON_NO_AP_FOUND = 201,
    IOT_NWK_ERR_REASON_AUTH_FAIL = 202,
    IOT_NWK_ERR_REASON_ASSOC_FAIL = 203,
    IOT_NWK_ERR_REASON_HANDSHAKE_TIMEOUT = 204,
    IOT_NWK_ERR_REASON_CONNECTION_FAIL = 205,
    IOT_NWK_ERR_REASON_AP_TSF_RESET = 206,
    IOT_NWK_ERR_REASON_NO_AP_FOUND_COMPATIBLE_SECURITY = 210,
} IOT_NwkErrorReason_t;

typedef struct
{
    uint8_t reason;
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t bssid[6];
    int8_t rssi;
} IOT_NwkEventDisconnectedData_t;

typedef struct
{
    uint32_t ip;      /**< IP address */
    uint32_t netmask; /**< Netmask */
    uint32_t gateway; /**< Gateway address */
    // iot_wifi_auth_mode_t authmode;
} IOT_NwkEventConnectedData_t;

typedef struct
{
    uint8_t authType;
    uint8_t minusRssi;
    uint16_t freq;
    char *ssid;
} wifi_scan_res_require_t;

typedef struct
{
    IOT_NetworkIface iface;
    uint8_t status;
    uint8_t auth;
    uint8_t strength;
    bool hasIpv4;
    uint32_t ipv4;
} IOT_NetworkConnectivity_t;

typedef int mac_addr_t[6]; /**< MAC address of network interface */
typedef int (*IOT_NwkEventCb)(IOT_NetworkIface nwkInterface,
                              void *arg,
                              IOT_NwkEventStatus_t eventType,
                              void *eventData);

/**
 * @brief Init network interfaces.
 *
 * This function initializes the network interfaces, such as WiFi or Ethernet, or both...
 *
 * @param[in] ifaces the network interfaces to initialize
 * @return
 *  - IOT_OK: Success
 *  - Others: Fail
 */

iot_err_t IOT_NwkInit(IOT_NetworkIface *ifaces);

/**
 * @brief Deinit network interfaces.
 *
 * This function de-initializes the network interfaces, such as WiFi or Ethernet, or both...
 *
 * @param[in] ifaces the network interfaces to deinitialize
 * @return
 *  - IOT_OK: Success
 *  - Others: Fail
 */
iot_err_t IOT_NwkDeinit(IOT_NetworkIface *ifaces);

/**
 * @brief start network interfaces.
 *
 * This function starts the network interfaces, such as WiFi or Ethernet, or both, the network
 * interfaces must be initialized before starting.
 *
 * @param[in] ifaces the network interfaces to start
 * @return
 *  - IOT_OK: Success
 *  - Others: Fail
 */
iot_err_t IOT_NwkScanWifi(IOT_NetworkIface iface, uint8_t interval, wifi_scan_res_require_t *res, uint8_t *numOfResults);
iot_err_t IOT_NwkConnectWifi(IOT_NetworkIface *ifaces, char *ssid, char *pwd, uint8_t retryTimes);

/**
 * @brief stop network interfaces.
 *
 * This function stops the network interfaces, such as WiFi or Ethernet, or both...
 *
 * @param[in] ifaces the network interfaces to stop
 * @return
 *  - IOT_OK: Success
 *  - Others: Fail
 */
iot_err_t IOT_NwkStop(IOT_NetworkIface *ifaces);
iot_err_t IOT_NwkGetMac(IOT_NetworkIface iface, uint8_t *mac);
iot_err_t IOT_NwkRegisterEventCb(IOT_NetworkIface nwkInf, IOT_NwkEventStatus_t eventType, IOT_NwkEventCb cb, void *arg);
iot_err_t IOT_NwkUnregisterEventCb(IOT_NetworkIface nwkInf, IOT_NwkEventStatus_t eventType, IOT_NwkEventCb cb);

iot_err_t IOT_NwkGetConnectionStatus(IOT_NetworkIface iface, IOT_NwkEventStatus_t *status);
iot_err_t IOT_NwkGetConnectivities(IOT_NetworkConnectivity_t *connectivities, uint8_t *numOfConnectivities);
void IOT_NwkSetAutoReconnect(IOT_NetworkIface iface, bool enable);
bool IOT_NwkGetAutoReconnect(IOT_NetworkIface iface);
uint8_t IOT_NwkGetAuthMode(IOT_NetworkIface iface);
int8_t IOT_NwkGetRssi(IOT_NetworkIface iface);
iot_err_t IOT_NwkGetIpInfo(IOT_NetworkIface iface, IOT_NwkEventConnectedData_t *ipInfo);

// iot_err_t _IFACE_SET_STATIC_IP(IOT_NetworkIface iface, const ip_info_t *ip_info);
// iot_err_t _IFACE_GET_IP_INFO(IOT_NetworkIface iface, ip_info_t *ip_info);
// iot_err_t _IFACE_SET_MAC_ADDR(IOT_NetworkIface iface, const mac_addr_t mac);
// iot_err_t _IFACE_GET_MAC_ADDR(IOT_NetworkIface iface, mac_addr_t *out_mac);

#pragma once

#include "IOT_ErrorManager.h"
#include <inttypes.h>
#include <stdbool.h>
// WiFi interface management

#define IOT_WIFI_EVENT "wifi_event"
#define _WIFI_EVENT_ALL NULL  /**< register handler for any event base */
#define _WIFI_EVENT_ALL_ID -1 /**< register handler for any event id */
typedef enum
{
    IOT_WIFI_AUTH_OPEN = 0,     /**< authenticate mode : open */
    IOT_WIFI_AUTH_WEP,          /**< authenticate mode : WEP */
    IOT_WIFI_AUTH_WPA_PSK,      /**< authenticate mode : WPA_PSK */
    IOT_WIFI_AUTH_WPA2_PSK,     /**< authenticate mode : WPA2_PSK */
    IOT_WIFI_AUTH_WPA_WPA2_PSK, /**< authenticate mode : WPA_WPA2_PSK */
    IOT_WIFI_AUTH_ENTERPRISE,   /**< authenticate mode : WiFi EAP security */
    IOT_WIFI_AUTH_WPA2_ENTERPRISE =
        IOT_WIFI_AUTH_ENTERPRISE, /**< authenticate mode : WiFi EAP security */
    IOT_WIFI_AUTH_WPA3_PSK,       /**< authenticate mode : WPA3_PSK */
    IOT_WIFI_AUTH_WPA2_WPA3_PSK,  /**< authenticate mode : WPA2_WPA3_PSK */
    IOT_WIFI_AUTH_WAPI_PSK,       /**< authenticate mode : WAPI_PSK */
    IOT_WIFI_AUTH_MAX,            /**< authenticate mode : OWE */
} iot_wifi_auth_mode_t;

typedef enum {
    IOT_WIFI_ERR_REASON_UNSPECIFIED = 1,
    IOT_WIFI_ERR_REASON_AUTH_EXPIRE = 2,
    IOT_WIFI_ERR_REASON_AUTH_LEAVE = 3,
    IOT_WIFI_ERR_REASON_ASSOC_EXPIRE = 4,
    IOT_WIFI_ERR_REASON_ASSOC_TOOMANY = 5,
    IOT_WIFI_ERR_REASON_NOT_AUTHED = 6,
    IOT_WIFI_ERR_REASON_NOT_ASSOCED = 7,
    IOT_WIFI_ERR_REASON_ASSOC_LEAVE = 8,
    IOT_WIFI_ERR_REASON_ASSOC_NOT_AUTHED = 9,
    IOT_WIFI_ERR_REASON_DISASSOC_PWRCAP_BAD = 10,
    IOT_WIFI_ERR_REASON_DISASSOC_SUPCHAN_BAD = 11,
    IOT_WIFI_ERR_REASON_IE_INVALID = 13,
    IOT_WIFI_ERR_REASON_MIC_FAILURE = 14,
    IOT_WIFI_ERR_REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
    IOT_WIFI_ERR_REASON_GROUP_KEY_UPDATE_TIMEOUT = 16,
    IOT_WIFI_ERR_REASON_IE_IN_4WAY_DIFFERS = 17,
    IOT_WIFI_ERR_REASON_GROUP_CIPHER_INVALID = 18,
    IOT_WIFI_ERR_REASON_PAIRWISE_CIPHER_INVALID = 19,
    IOT_WIFI_ERR_REASON_AKMP_INVALID = 20,
    IOT_WIFI_ERR_REASON_UNSUPP_RSN_IE_VERSION = 21,
    IOT_WIFI_ERR_REASON_INVALID_RSN_IE_CAPABILITIES = 22,
    IOT_WIFI_ERR_REASON_IEEE_802_1X_AUTH_FAILED = 23,
    IOT_WIFI_ERR_REASON_CIPHER_SUITE_REJECTED = 24,
    IOT_WIFI_ERR_REASON_NO_AP_FOUND = 201,
    IOT_WIFI_ERR_REASON_AUTH_FAIL = 202,
    IOT_WIFI_ERR_REASON_HANDSHAKE_TIMEOUT = 204,
} iot_wifi_err_reason_t;

typedef struct
{
    char ssid[32];                 // SSID of the access point
    int rssi;                      // Signal strength
    uint8_t bssid[6];              /**< MAC address of AP */
    uint8_t primary;               /**< channel of AP */
    iot_wifi_auth_mode_t authmode; /**< authmode of AP */
} iot_ap_info_t;

typedef enum
{
    WIFI_EVENT_STATUS_DISCONNECTED,
    WIFI_EVENT_STATUS_CONNECTING,
    WIFI_EVENT_STATUS_CONNECTED_NO_IP,
    WIFI_EVENT_STATUS_GOT_IP,
    WIFI_EVENT_STATUS_CONNECTED,
    WIFI_EVENT_STATUS_WRONG_PASSWORD,
    WIFI_EVENT_STATUS_NO_AP_FOUND,
    WIFI_EVENT_STATUS_AUTH_FAIL,
    WIFI_EVENT_STATUS_UNKNOWN_ERROR,
    WIFI_EVENT_STATUS_MAX,
    WIFI_EVENT_STATUS_ALL
} wifi_event_status_t;

typedef struct
{
    uint8_t ssid[32]; /**< SSID of AP */
    uint8_t ssid_len; /**< SSID length of AP */
    uint8_t bssid[6]; /**< BSSID of AP */
    int8_t rssi;      /**< signal strength of AP */
    uint8_t reason;
} wifi_mgmt_event_disconnected_data_t;

typedef struct
{
    uint8_t ssid[32];              /**< SSID of AP */
    uint8_t ssid_len;              /**< SSID length of AP */
    uint8_t bssid[6];              /**< BSSID of AP */
    int8_t channel;                /**< channel of AP */
    iot_wifi_auth_mode_t authmode; /**< authmode of AP */
} wifi_mgmt_event_connected_data_t;

// typedef struct
// {
//     char ip[16];      // IP address
//     char gateway[16]; // Gateway address
//     char netmask[16]; // Netmask
//     char dns[16];     // DNS server address
// } wifi_ip_conf_info_t;

// typedef enum
// {
//     IOT_WIFI_MODE_NULL = 0, /**< null mode */
//     IOT_WIFI_MODE_STA,      /**< WiFi station mode */
//     IOT_WIFI_MODE_AP,       /**< WiFi soft-AP mode */
// } iot_wifi_mode_t;

typedef struct
{
    uint8_t ssid[32]; /**< SSID of AP */
    uint8_t ssid_len; /**< SSID length of AP */
    uint8_t bssid[6]; /**< BSSID of AP */
    int8_t rssi;      /**< signal strength of AP */
    uint8_t reason;
} iot_wifi_event_disconnected_data_t;

typedef struct
{
    uint8_t ssid[32];              /**< SSID of AP */
    uint8_t ssid_len;              /**< SSID length of AP */
    uint8_t bssid[6];              /**< BSSID of AP */
    int8_t channel;                /**< channel of AP */
    iot_wifi_auth_mode_t authmode; /**< authmode of AP */
} iot_wifi_event_connected_data_t;

typedef struct
{
    uint32_t ip;      /**< IP address */
    uint32_t netmask; /**< Netmask */
    uint32_t gateway; /**< Gateway address */
} IOT_WifiIpInfo_t;

typedef IOT_WifiIpInfo_t iot_wifi_ip_event_got_ip_data_t; /**< Alias for event callback compat */

typedef void (*IOT_WifiEventCb)(
    void *arg, wifi_event_status_t eventType,
    void *event_data); /**< function called when an event is posted to the queue */

iot_err_t IOT_WifiInit(uint8_t iface_num);
iot_err_t IOT_WifiDeinit(uint8_t iface_num);
iot_err_t IOT_WifiGetMac(uint8_t *mac);
iot_err_t IOT_WifiConnect(uint8_t iface_num, const char *target_ssid, const char *target_password, uint8_t retryTimes);
iot_err_t IOT_WifiDisconnect(uint8_t iface_num);
iot_ap_info_t *IOT_WifiScan(uint8_t iface_num, uint8_t *maxDevice, uint8_t timeout_sec);
wifi_event_status_t IOT_WifiGetConnectionStatus(uint8_t iface_num);
iot_err_t IOT_WifiRegisterEventCb(uint8_t iface_num, wifi_event_status_t eventType,
                                  IOT_WifiEventCb callback, void *arg);
iot_err_t IOT_WifiUnregisterEventCb(uint8_t iface_num, wifi_event_status_t eventType);

void IOT_WifiSetAutoReconnect(bool enable);
bool IOT_WifiGetAutoReconnect(void);
iot_wifi_auth_mode_t IOT_WifiGetAuth(void);
int8_t IOT_WifiGetRssi(void);
iot_err_t IOT_WifiGetIpInfo(uint8_t iface_num, IOT_WifiIpInfo_t *ipInfo);

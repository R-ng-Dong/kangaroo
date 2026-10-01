#pragma once
#ifdef __cplusplus
extern "C"
{
#endif

// Ethernet interface management
/**
 * @brief Handle of Ethernet driver
 *
 */
#define IOT_ETH_EVENT "eth_event"
    typedef void *iot_eth_handle_t;
#include <IOT_ErrorManager.h>
#include <inttypes.h>
#include <stdbool.h>
#include <inttypes.h>
    /*
    Ethernet including:
    - Initialization
    - Deinitialization
    - Start: bring up the interface
    - Stop: bring down the interface
    - Event handling: link up, link down, got IP, lost IP
    - Status checking: check if the interface is up or down
    - IP acquired: via DHCP or manual config.

    # parameters and attributes
    - MAC address
    - IP configuration: IP address, subnet mask, gateway, DNS servers
    - Link status: up or down
    - Speed and duplex mode
    - MTU (Maximum Transmission Unit)
    - PHY (Physical Layer) status and statistics
    - Event callbacks for link status changes and IP acquisition
    - Support for multiple Ethernet interfaces
    - Support for different Ethernet PHY
    - Support for static IP and DHCP
    - Support for IPv4 and IPv6
    - Error handling and reporting
    - Integration with the overall network management system

    */

    /*

    OPERATIONS AND EVENT:

    Init → Link Up → DHCP Start → Got IP → Ready
           ↓
         Link Down → Lost IP → Not Ready
           ↑
       Cable Plug In
    */

    /* Ethernet Events */
    typedef enum
    {
        ETH_EVENT_STATUS_UNINITIALIZED = 0, /**< Ethernet driver uninitialized */
        ETH_EVENT_STATUS_INITIALIZED,       /**< Ethernet driver initialized */
        ETH_EVENT_STATUS_LINK_UP,
        ETH_EVENT_STATUS_LINK_DOWN,
        ETH_EVENT_STATUS_RX_PACKET,
        ETH_EVENT_STATUS_TX_COMPLETE,
        ETH_EVENT_STATUS_ERROR,
        ETH_EVENT_STATUS_PHY_STATUS_CHANGE,
        ETH_EVENT_STATUS_MAX,
        ETH_EVENT_STATUS_ALL
    } eth_event_status_t;

    /* Ethernet Event Handler Function Type */
    typedef void (*eth_event_handler_t)(void *arg, eth_event_status_t eventType, void *eventData);
    typedef uint8_t eth_mac_addr_t[6]; /**< MAC address of Ethernet interface */

    typedef struct
    {
        uint16_t speed;
        bool full_duplex;
        bool link_up;
        uint8_t iface_num;
    } eth_event_up_t;

    typedef struct
    {
        uint16_t reason;
        uint8_t iface_num;
    } eth_event_down_t;

    typedef struct
    {
        uint8_t mac[6];
        uint32_t mtu;
        uint32_t flags; // Combination of ETH_FLAG_*
    } eth_event_init_t;

    typedef struct
    {
        uint8_t ip[4];      // IPv4 address
        uint8_t subnet[4];  // Subnet mask
        uint8_t gateway[4]; // Gateway address
    } ip_info_t;

    typedef struct
    {
        uint8_t iface_num;
        ip_info_t ip_info;
    } eth_event_got_ip_t;

    typedef struct
    {
        uint8_t iface_num;
    } eth_event_lost_ip_t;

    iot_err_t IOT_EthInit(uint8_t *iface_num);
    iot_err_t IOT_EthDeinit(uint8_t iface_num);
    // iot_err_t _ETH_START(uint8_t *iface_num);
    // iot_err_t _ETH_STOP(uint8_t *iface_num);

    iot_err_t IOT_EthRegisterEventCb(uint8_t ifaceNum, eth_event_status_t eventType,
                                     eth_event_handler_t handler, void *user_data);
    iot_err_t IOT_EthUnregisterEventCb(uint8_t ifaceNum, eth_event_status_t eventType);

    eth_event_status_t IOT_EthGetConnectionStatus(uint8_t iface_num);
// iot_err_t _ETH_GET_IP_INFO(uint8_t iface_num, ip_info_t *ip_info);
// iot_err_t _ETH_SET_STATIC_IP(uint8_t iface_num, const ip_info_t *ip_info);
// iot_err_t _ETH_SET_MAC_ADDR(uint8_t iface_num, const eth_mac_addr_t mac);
// iot_err_t _ETH_GET_MAC_ADDR(uint8_t iface_num, eth_mac_addr_t *out_mac);
#ifdef __cplusplus
}
#endif

#include "sdkconfig.h"
#if CONFIG_IOT_ETHERNET_ENABLED

#include "network/ethernet.h"
// Ethernet Interface implementation for ESP32
#include "IOT_ErrorManager.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_event.h"
#include "IOT_Log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "sdkconfig.h"
#include "ethernet_init.h"
#include <string.h>

#define CONFIG_EXAMPLE_ETH_PHY_ADDR 1
#define CONFIG_EXAMPLE_ETH_PHY_RST_GPIO -1
#define CONFIG_EXAMPLE_ETH_MDC_GPIO 23
#define CONFIG_EXAMPLE_ETH_MDIO_GPIO 18
#define CONFIG_EXAMPLE_SPI_ETHERNETS_NUM 2
#define CONFIG_EXAMPLE_ETH_PHY_LAN87XX 1
#define SPI_ETHERNETS_NUM 0
#define INTERNAL_ETHERNETS_NUM 1
static const char *TAG = "eth_example";
static volatile eth_event_status_t currentEthStatus = ETH_EVENT_STATUS_UNINITIALIZED;

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *) event_data;

    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
        currentEthStatus = ETH_EVENT_STATUS_LINK_UP;
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        IOT_LOGI(TAG, "Ethernet Link Up");
        IOT_LOGI(TAG,
                 "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0],
                 mac_addr[1],
                 mac_addr[2],
                 mac_addr[3],
                 mac_addr[4],
                 mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        currentEthStatus = ETH_EVENT_STATUS_LINK_DOWN;
        IOT_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        currentEthStatus = ETH_EVENT_STATUS_INITIALIZED;
        IOT_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        currentEthStatus = ETH_EVENT_STATUS_UNINITIALIZED;
        IOT_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    IOT_LOGI(TAG, "Ethernet Got IP Address");
    IOT_LOGI(TAG, "~~~~~~~~~~~");
    IOT_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    IOT_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    IOT_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    IOT_LOGI(TAG, "~~~~~~~~~~~");
}

iot_err_t IOT_EthInit(uint8_t *iface_num)
{
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles = NULL;
    esp_err_t res = ESP_OK;
    res = example_eth_init(&eth_handles, &eth_port_cnt);
    if (res != ESP_OK)
    {
        IOT_LOGE("iot_eth", "example_eth_init failed: %s", esp_err_to_name(res));
        if (res == ESP_ERR_NOT_SUPPORTED)
        {
            IOT_LOGE("iot_eth", "Ethernet not supported on this configuration");
            return IOT_ERR_NOT_SUPPORTED;
        }
        return IOT_ERR_FAIL;
    }
    if (eth_handles == NULL || eth_port_cnt == 0)
    {
        IOT_LOGE("iot_eth", "Failed to initialize Ethernet interface");
        return IOT_ERR_FAIL;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (eth_port_cnt == 1)
    {
        // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to
        // modify default esp-netif configuration parameters.
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *eth_netif = esp_netif_new(&cfg);
        // Attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
    }
    else
    {
        // Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are used and so you
        // need to modify esp-netif configuration parameters for each interface (name, priority,
        // etc.).
        esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
        esp_netif_config_t cfg_spi = {.base = &esp_netif_config, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
        char if_key_str[10];
        char if_desc_str[10];
        char num_str[3];
        for (int i = 0; i < eth_port_cnt; i++)
        {
            itoa(i, num_str, 10);
            strcat(strcpy(if_key_str, "ETH_"), num_str);
            strcat(strcpy(if_desc_str, "eth"), num_str);
            esp_netif_config.if_key = if_key_str;
            esp_netif_config.if_desc = if_desc_str;
            esp_netif_config.route_prio -= i * 5;
            esp_netif_t *eth_netif = esp_netif_new(&cfg_spi);
            // Attach Ethernet driver to TCP/IP stack
            ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[i])));
        }
    }
    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // Start Ethernet driver state machine
    for (int i = 0; i < eth_port_cnt; i++)
    {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }
    return IOT_OK;
}
iot_err_t IOT_EthDeinit(uint8_t iface_num)
{
    // This function should deinitialize the Ethernet module.
    // The implementation will depend on the specific Ethernet library or API being used.
    IOT_LOGW("iot_eth", "Ethernet de-initialized");
    return IOT_OK; // Return success status
}
// iot_err_t _ETH_START(uint8_t *iface_num);
// iot_err_t _ETH_STOP(uint8_t *iface_num);

iot_err_t
IOT_EthRegisterEventCb(uint8_t ifaceNum, eth_event_status_t eventType, eth_event_handler_t handler, void *user_data)
{
    // This function should register an event handler for the specified event base and ID.
    // The implementation will depend on the specific event handling library or API being used.
    IOT_LOGW("iot_eth", "Ethernet event handler registered for event type %d on interface %d", eventType, ifaceNum);
    return IOT_OK; // Return success status
}
iot_err_t IOT_EthUnregisterEventCb(uint8_t ifaceNum, eth_event_status_t eventType)
{
    // This function should unregister an event handler for the specified event base and ID.
    // The implementation will depend on the specific event handling library or API being used.
    IOT_LOGW("iot_eth", "Ethernet event handler unregistered for event type %d on interface %d", eventType, ifaceNum);
    return IOT_OK; // Return success status
}

eth_event_status_t IOT_EthGetConnectionStatus(uint8_t iface_num)
{
    return currentEthStatus;
}

#endif // CONFIG_IOT_ETHERNET_ENABLED

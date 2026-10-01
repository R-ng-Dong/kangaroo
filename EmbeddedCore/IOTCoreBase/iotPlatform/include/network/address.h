#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Address Constants */
#define ADDR_IPV4_LEN           4       /* IPv4 address length in bytes */
#define ADDR_IPV6_LEN           16      /* IPv6 address length in bytes */
#define ADDR_MAC_LEN            6       /* MAC address length in bytes */
#define ADDR_STR_IPV4_MAX       16      /* Max IPv4 string: "255.255.255.255\0" */
#define ADDR_STR_IPV6_MAX       40      /* Max IPv6 string length */
#define ADDR_STR_MAC_MAX        18      /* Max MAC string: "FF:FF:FF:FF:FF:FF\0" */

/* Special IPv4 Addresses */
#define ADDR_IPV4_ANY           0x00000000UL    /* 0.0.0.0 */
#define ADDR_IPV4_LOOPBACK      0x7F000001UL    /* 127.0.0.1 */
#define ADDR_IPV4_BROADCAST     0xFFFFFFFFUL    /* 255.255.255.255 */

/* Address Types */
typedef enum {
    ADDR_TYPE_IPV4 = 0,
    ADDR_TYPE_IPV6,
    ADDR_TYPE_MAC,
    ADDR_TYPE_UNKNOWN
} addr_type_t;

/* Address Family */
typedef enum {
    ADDR_FAMILY_IPV4 = 2,       /* AF_INET */
    ADDR_FAMILY_IPV6 = 10,      /* AF_INET6 */
    ADDR_FAMILY_UNSPEC = 0      /* AF_UNSPEC */
} addr_family_t;

/* IP Version */
typedef enum {
    IP_VERSION_4 = 4,
    IP_VERSION_6 = 6
} ip_version_t;

/* Address Configuration Type */
typedef enum {
    ADDR_CONFIG_STATIC = 0,     /* Manual configuration */
    ADDR_CONFIG_DHCP,           /* DHCP assigned */
    ADDR_CONFIG_AUTO,           /* Auto-configuration */
    ADDR_CONFIG_LINK_LOCAL      /* Link-local address */
} addr_config_type_t;

/* IPv4 Address Structure */
typedef struct {
    union {
        uint32_t addr;          /* Network byte order */
        uint8_t bytes[4];       /* Byte array access */
        struct {
            uint8_t a, b, c, d; /* Dotted decimal access */
        };
    };
} ipv4_addr_t;

/* IPv6 Address Structure */
typedef struct {
    union {
        uint8_t bytes[16];      /* Byte array access */
        uint16_t words[8];      /* 16-bit word access */
        uint32_t dwords[4];     /* 32-bit dword access */
    };
} ipv6_addr_t;

/* MAC Address Structure */
typedef struct {
    uint8_t bytes[6];           /* MAC address bytes */
} mac_addr_t;

/* Generic Network Address */
typedef struct {
    addr_type_t type;
    union {
        ipv4_addr_t ipv4;
        ipv6_addr_t ipv6;
        mac_addr_t mac;
        uint8_t raw[16];        /* Raw bytes for any address type */
    };
} net_addr_t;

/* Network Interface Configuration */
typedef struct {
    ipv4_addr_t ip;             /* IP address */
    ipv4_addr_t netmask;        /* Subnet mask */
    ipv4_addr_t gateway;        /* Default gateway */
    ipv4_addr_t dns_primary;    /* Primary DNS server */
    ipv4_addr_t dns_secondary;  /* Secondary DNS server */
    mac_addr_t mac;             /* MAC address */
    addr_config_type_t config_type; /* Configuration method */
    uint8_t prefix_len;         /* Prefix length (CIDR notation) */
} net_config_t;

/* IPv6 Network Configuration */
typedef struct {
    ipv6_addr_t ip;             /* IPv6 address */
    ipv6_addr_t gateway;        /* IPv6 gateway */
    ipv6_addr_t dns_primary;    /* Primary DNS server */
    ipv6_addr_t dns_secondary;  /* Secondary DNS server */
    mac_addr_t mac;             /* MAC address */
    uint8_t prefix_len;         /* Prefix length */
    addr_config_type_t config_type; /* Configuration method */
} net_config_v6_t;

/* Address Resolution Entry */
typedef struct {
    ipv4_addr_t ip;             /* IP address */
    mac_addr_t mac;             /* Corresponding MAC address */
    uint32_t timestamp;         /* Last update time */
    bool valid;                 /* Entry validity */
} arp_entry_t;

/* Network Address Pool */
typedef struct {
    ipv4_addr_t network;        /* Network address */
    ipv4_addr_t netmask;        /* Network mask */
    ipv4_addr_t start_ip;       /* Start of address pool */
    ipv4_addr_t end_ip;         /* End of address pool */
    uint32_t total_addresses;   /* Total addresses in pool */
    uint32_t used_addresses;    /* Currently allocated addresses */
} addr_pool_t;

/* IPv4 Address Initialization Macros */
#define IPV4_ADDR_INIT(a, b, c, d) \
    { .bytes = {(a), (b), (c), (d)} }

#define IPV4_ADDR_SET(addr, a, b, c, d) \
    do { \
        (addr)->bytes[0] = (a); (addr)->bytes[1] = (b); \
        (addr)->bytes[2] = (c); (addr)->bytes[3] = (d); \
    } while(0)

#define IPV4_ADDR_FROM_UINT32(addr, val) \
    do { \
        (addr)->addr = htonl(val); \
    } while(0)

/* IPv6 Address Initialization Macros */
#define IPV6_ADDR_INIT(w0, w1, w2, w3, w4, w5, w6, w7) \
    { .words = {htons(w0), htons(w1), htons(w2), htons(w3), \
                htons(w4), htons(w5), htons(w6), htons(w7)} }

#define IPV6_ADDR_SET_LOOPBACK(addr) \
    do { \
        memset((addr), 0, sizeof(ipv6_addr_t)); \
        (addr)->bytes[15] = 1; \
    } while(0)

/* MAC Address Initialization Macros */
#define MAC_ADDR_INIT(b0, b1, b2, b3, b4, b5) \
    { .bytes = {(b0), (b1), (b2), (b3), (b4), (b5)} }

#define MAC_ADDR_SET(addr, b0, b1, b2, b3, b4, b5) \
    do { \
        (addr)->bytes[0] = (b0); (addr)->bytes[1] = (b1); \
        (addr)->bytes[2] = (b2); (addr)->bytes[3] = (b3); \
        (addr)->bytes[4] = (b4); (addr)->bytes[5] = (b5); \
    } while(0)

/* Network Configuration Initialization */
#define NET_CONFIG_INIT_STATIC(ip_a, ip_b, ip_c, ip_d, \
                               mask_a, mask_b, mask_c, mask_d, \
                               gw_a, gw_b, gw_c, gw_d) \
    { \
        .ip = IPV4_ADDR_INIT(ip_a, ip_b, ip_c, ip_d), \
        .netmask = IPV4_ADDR_INIT(mask_a, mask_b, mask_c, mask_d), \
        .gateway = IPV4_ADDR_INIT(gw_a, gw_b, gw_c, gw_d), \
        .config_type = ADDR_CONFIG_STATIC \
    }

#define NET_CONFIG_INIT_DHCP() \
    { \
        .config_type = ADDR_CONFIG_DHCP \
    }

/* Address Comparison Macros */
#define IPV4_ADDR_EQUAL(addr1, addr2) \
    ((addr1)->addr == (addr2)->addr)

#define IPV6_ADDR_EQUAL(addr1, addr2) \
    (memcmp((addr1)->bytes, (addr2)->bytes, 16) == 0)

#define MAC_ADDR_EQUAL(addr1, addr2) \
    (memcmp((addr1)->bytes, (addr2)->bytes, 6) == 0)

/* Address Type Checking Macros */
#define IPV4_IS_LOOPBACK(addr) \
    (((addr)->addr & htonl(0xFF000000UL)) == htonl(0x7F000000UL))

#define IPV4_IS_MULTICAST(addr) \
    (((addr)->addr & htonl(0xF0000000UL)) == htonl(0xE0000000UL))

#define IPV4_IS_BROADCAST(addr) \
    ((addr)->addr == htonl(0xFFFFFFFFUL))

#define IPV4_IS_PRIVATE(addr) \
    (((addr)->addr & htonl(0xFF000000UL)) == htonl(0x0A000000UL) || \
     ((addr)->addr & htonl(0xFFF00000UL)) == htonl(0xAC100000UL) || \
     ((addr)->addr & htonl(0xFFFF0000UL)) == htonl(0xC0A80000UL))

#define MAC_IS_BROADCAST(addr) \
    ((addr)->bytes[0] == 0xFF && (addr)->bytes[1] == 0xFF && \
     (addr)->bytes[2] == 0xFF && (addr)->bytes[3] == 0xFF && \
     (addr)->bytes[4] == 0xFF && (addr)->bytes[5] == 0xFF)

#define MAC_IS_MULTICAST(addr) \
    ((addr)->bytes[0] & 0x01)

#define MAC_IS_UNICAST(addr) \
    (!((addr)->bytes[0] & 0x01))

/* Network Byte Order Conversion */
#ifndef htons
#define htons(x) ((uint16_t)((((x) & 0xff) << 8) | (((x) >> 8) & 0xff)))
#endif

#ifndef ntohs
#define ntohs(x) htons(x)
#endif

#ifndef htonl
#define htonl(x) ((uint32_t)(((x) & 0xff) << 24) | \
                  (((x) & 0xff00) << 8) | \
                  (((x) & 0xff0000) >> 8) | \
                  (((x) >> 24) & 0xff))
#endif

#ifndef ntohl
#define ntohl(x) htonl(x)
#endif

/* Function Prototypes */

/* Address Conversion Functions */
bool ipv4_str_to_addr(const char *str, ipv4_addr_t *addr);
bool ipv4_addr_to_str(const ipv4_addr_t *addr, char *str, size_t len);
bool ipv6_str_to_addr(const char *str, ipv6_addr_t *addr);
bool ipv6_addr_to_str(const ipv6_addr_t *addr, char *str, size_t len);
bool mac_str_to_addr(const char *str, mac_addr_t *addr);
bool mac_addr_to_str(const mac_addr_t *addr, char *str, size_t len);

/* Address Validation */
bool ipv4_addr_is_valid(const ipv4_addr_t *addr);
bool ipv6_addr_is_valid(const ipv6_addr_t *addr);
bool mac_addr_is_valid(const mac_addr_t *addr);

/* Address Manipulation */
void ipv4_addr_copy(ipv4_addr_t *dst, const ipv4_addr_t *src);
void ipv6_addr_copy(ipv6_addr_t *dst, const ipv6_addr_t *src);
void mac_addr_copy(mac_addr_t *dst, const mac_addr_t *src);
void ipv4_addr_clear(ipv4_addr_t *addr);
void ipv6_addr_clear(ipv6_addr_t *addr);
void mac_addr_clear(mac_addr_t *addr);

/* Network Operations */
bool ipv4_addr_in_network(const ipv4_addr_t *addr, const ipv4_addr_t *network, const ipv4_addr_t *netmask);
bool ipv4_addr_in_subnet(const ipv4_addr_t *addr, const ipv4_addr_t *network, uint8_t prefix_len);
uint8_t ipv4_netmask_to_prefix(const ipv4_addr_t *netmask);
void ipv4_prefix_to_netmask(uint8_t prefix_len, ipv4_addr_t *netmask);
void ipv4_get_network_addr(const ipv4_addr_t *addr, const ipv4_addr_t *netmask, ipv4_addr_t *network);
void ipv4_get_broadcast_addr(const ipv4_addr_t *network, const ipv4_addr_t *netmask, ipv4_addr_t *broadcast);

/* MAC Address Utilities */
void mac_addr_generate_random(mac_addr_t *addr);
void mac_addr_set_local_bit(mac_addr_t *addr);
void mac_addr_clear_local_bit(mac_addr_t *addr);
bool mac_addr_is_local(const mac_addr_t *addr);

/* Network Configuration Utilities */
void net_config_init(net_config_t *config);
void net_config_copy(net_config_t *dst, const net_config_t *src);
bool net_config_is_valid(const net_config_t *config);
void net_config_set_dhcp(net_config_t *config);
void net_config_set_static(net_config_t *config, const ipv4_addr_t *ip, 
                          const ipv4_addr_t *netmask, const ipv4_addr_t *gateway);

/* Address Pool Management */
void addr_pool_init(addr_pool_t *pool, const ipv4_addr_t *network, 
                   const ipv4_addr_t *netmask, const ipv4_addr_t *start, const ipv4_addr_t *end);
bool addr_pool_allocate(addr_pool_t *pool, ipv4_addr_t *addr);
bool addr_pool_release(addr_pool_t *pool, const ipv4_addr_t *addr);
uint32_t addr_pool_available(const addr_pool_t *pool);
bool addr_pool_contains(const addr_pool_t *pool, const ipv4_addr_t *addr);

/* Generic Network Address Functions */
void net_addr_init(net_addr_t *addr, addr_type_t type);
bool net_addr_set_ipv4(net_addr_t *addr, const ipv4_addr_t *ipv4);
bool net_addr_set_ipv6(net_addr_t *addr, const ipv6_addr_t *ipv6);
bool net_addr_set_mac(net_addr_t *addr, const mac_addr_t *mac);
bool net_addr_get_ipv4(const net_addr_t *addr, ipv4_addr_t *ipv4);
bool net_addr_get_ipv6(const net_addr_t *addr, ipv6_addr_t *ipv6);
bool net_addr_get_mac(const net_addr_t *addr, mac_addr_t *mac);
bool net_addr_equal(const net_addr_t *addr1, const net_addr_t *addr2);

/* Debug and Utility Functions */
void ipv4_addr_print(const ipv4_addr_t *addr);
void ipv6_addr_print(const ipv6_addr_t *addr);
void mac_addr_print(const mac_addr_t *addr);
void net_config_print(const net_config_t *config);
const char* addr_config_type_to_string(addr_config_type_t type);

#ifdef __cplusplus
}
#endif


#include "nwkAddr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void nwkaddr_init(NwkAddr *na) {
    if (!na) return;
    memset(na->addr, 0, MAX_ADDR_LEN);
    na->port = 0;
    na->is_ipv6 = false;
}

bool nwkaddr_set(NwkAddr *na, const char *addr, uint16_t port) {
    if (!na || !addr) return false;
    
    size_t len = strlen(addr);
    if (len >= MAX_ADDR_LEN) return false;
    
    strncpy(na->addr, addr, MAX_ADDR_LEN - 1);
    na->addr[MAX_ADDR_LEN - 1] = '\0';
    na->port = port;
    na->is_ipv6 = (strchr(addr, ':') != NULL);
    
    return true;
}

const char* nwkaddr_get_addr(const NwkAddr *na) {
    return na ? na->addr : NULL;
}

uint16_t nwkaddr_get_port(const NwkAddr *na) {
    return na ? na->port : 0;
}

bool nwkaddr_is_ipv6(const NwkAddr *na) {
    return na ? na->is_ipv6 : false;
}

bool nwkaddr_parse(NwkAddr *na, const char *str) {
    if (!na || !str) return false;
    
    char buf[MAX_ADDR_LEN + 10];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    // Handle IPv6 format [addr]:port
    if (buf[0] == '[') {
        char *end = strchr(buf, ']');
        if (!end) return false;
        
        *end = '\0';
        char *port_str = strchr(end + 1, ':');
        if (!port_str) return false;
        
        uint16_t port = (uint16_t)atoi(port_str + 1);
        return nwkaddr_set(na, buf + 1, port);
    }
    
    // Handle IPv4 format addr:port
    char *last_colon = strrchr(buf, ':');
    if (!last_colon) return false;
    
    *last_colon = '\0';
    uint16_t port = (uint16_t)atoi(last_colon + 1);
    return nwkaddr_set(na, buf, port);
}

void nwkaddr_to_string(const NwkAddr *na, char *buf, size_t len) {
    if (!na || !buf || len == 0) return;
    
    if (na->is_ipv6) {
        snprintf(buf, len, "[%s]:%u", na->addr, na->port);
    } else {
        snprintf(buf, len, "%s:%u", na->addr, na->port);
    }
}

bool nwkaddr_equals(const NwkAddr *a, const NwkAddr *b) {
    if (!a || !b) return false;
    return (strcmp(a->addr, b->addr) == 0) && (a->port == b->port);
}
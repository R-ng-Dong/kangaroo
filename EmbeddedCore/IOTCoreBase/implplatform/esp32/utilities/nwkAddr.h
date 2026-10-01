#ifndef NWKADDR_H
#define NWKADDR_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define MAX_ADDR_LEN 46  // IPv6 max length

typedef struct {
    char addr[MAX_ADDR_LEN];
    uint16_t port;
    bool is_ipv6;
} NwkAddr;

// Initialize network address
void nwkaddr_init(NwkAddr *na);

// Set address and port
bool nwkaddr_set(NwkAddr *na, const char *addr, uint16_t port);

// Get address string
const char* nwkaddr_get_addr(const NwkAddr *na);

// Get port
uint16_t nwkaddr_get_port(const NwkAddr *na);

// Check if IPv6
bool nwkaddr_is_ipv6(const NwkAddr *na);

// Parse from string format "addr:port"
bool nwkaddr_parse(NwkAddr *na, const char *str);

// Convert to string format "addr:port"
void nwkaddr_to_string(const NwkAddr *na, char *buf, size_t len);

// Compare two addresses
bool nwkaddr_equals(const NwkAddr *a, const NwkAddr *b);

#endif // NWKADDR_H
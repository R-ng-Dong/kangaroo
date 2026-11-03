/**
 * @file ntp_plan.h
 * @brief Which NTP servers to ask, and in what order.
 *
 * The clock is load-bearing: TLS certificate validity and the cloud connection both
 * depend on it, so a device with no clock has no cloud. That makes the server list a single point of failure, and it is worth more than
 * two hard-coded IP literals with every fallback disabled — which is what this replaces.
 *
 * The policy lives here, free of ESP-IDF headers, so it can be tested on the host.
 * IOT_TimerMgmt.c turns a plan into an esp_sntp_config_t.
 *
 * Order of preference, most specific first:
 *   1. the DHCP-supplied server (option 42), when the network offers one
 *   2. the configured servers, in order — DNS names, so pools and anycast can do their job
 *   3. a built-in IP literal, only so a network with broken DNS still has something to ask
 */

#ifndef IOT_ESP32_NTP_PLAN_H
#define IOT_ESP32_NTP_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Slots handed to esp_netif_sntp. Bounded because config.servers[] is a fixed array. */
#define IOT_NTP_MAX_SERVERS 4

/**
 * Last-resort server, used only when nothing else is configured or reachable by name.
 *
 * An IP literal is deliberately LAST: a pinned address is the thing most likely to go
 * stale, and NIST explicitly asks clients not to hard-code one of its addresses at all.
 * This one is Google's, which is anycast and stable in practice.
 */
#define IOT_NTP_FALLBACK_SERVER "216.239.35.0"

/**
 * A resolved server list, ready to copy into esp_sntp_config_t.
 *
 * Mirrors esp_netif_sntp.c's actual contract, which is easy to get backwards:
 *     sntp_setservername(i + index_of_first_server, servers[i]);
 *     s_storage->servers[i] = strdup(config->servers[i]);   // i in 0..num_of_servers
 *
 * So @ref servers is OUR list, **dense from index 0**, @ref numOfServers counts only our
 * entries, and @ref indexOfFirstServer is the OFFSET that leaves lwIP's slot 0 to the
 * DHCP-supplied server. It is NOT a hole to leave in this array: a NULL entry becomes
 * strdup(NULL) and faults in ROM, crashing the device immediately after GOT_IP.
 */
typedef struct
{
    /** Our servers, DENSE from index 0. Every entry in 0..numOfServers-1 is non-NULL. */
    const char *servers[IOT_NTP_MAX_SERVERS];
    /** Value for config.num_of_servers — OUR servers only, excluding the DHCP slot. */
    uint8_t numOfServers;
    /** Value for config.index_of_first_server — the offset our list is installed at. */
    uint8_t indexOfFirstServer;
    /** Value for config.server_from_dhcp. */
    bool serverFromDhcp;
} IOT_NtpPlan_t;

/**
 * Build the server plan.
 *
 * @param plan           out; untouched when NULL.
 * @param dhcpSupported  true only when CONFIG_LWIP_DHCP_GET_NTP_SRV is enabled. Passing
 *                       true without it reserves a slot IDF will never fill, silently
 *                       costing one configured server.
 * @param cfg            configured servers, in preference order. NULL and "" entries are
 *                       skipped rather than left as holes — IDF strdup()s every entry, so a
 *                       NULL is a ROM-level crash, not a truncated list. May itself be NULL.
 * @param cfgCount       entries in @p cfg.
 *
 * The plan is never empty: if @p cfg yields nothing usable, @ref IOT_NTP_FALLBACK_SERVER
 * stands in. An empty list would mean the device never syncs, never validates a
 * certificate, and never reaches the cloud — with nothing in the logs naming the cause.
 */
void IOT_NtpBuildPlan(IOT_NtpPlan_t *plan, bool dhcpSupported,
                      const char *const *cfg, size_t cfgCount,
                      uint8_t platformMaxServers);

#endif /* IOT_ESP32_NTP_PLAN_H */

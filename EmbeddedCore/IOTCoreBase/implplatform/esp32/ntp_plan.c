#include "ntp_plan.h"

void IOT_NtpBuildPlan(IOT_NtpPlan_t *plan, bool dhcpSupported,
                      const char *const *cfg, size_t cfgCount,
                      uint8_t platformMaxServers)
{
    if (plan == NULL)
    {
        return;
    }

    for (uint8_t i = 0; i < IOT_NTP_MAX_SERVERS; i++)
    {
        plan->servers[i] = NULL;
    }

    /* index_of_first_server is an OFFSET applied when our names are installed into lwIP's
     * slots -- it does NOT mean "leave a hole in servers[]". esp_netif_sntp.c does:
     *     sntp_setservername(i + index_of_first_server, servers[i]);
     * and, fatally for a sparse array:
     *     s_storage->servers[i] = strdup(config->servers[i]);
     * over i in 0..num_of_servers. So servers[] must be DENSE from index 0, and every
     * entry non-NULL -- one NULL is strdup(NULL) and a Load access fault in ROM. */
    /* Fit the PLATFORM's array, not our own ceiling. esp_sntp_config_t's servers[] is
     * sized by CONFIG_LWIP_SNTP_MAX_SERVERS, which is 1 or 2 on most boards. Demanding
     * every board raise it is not this code's business -- and the compile-time #error that
     * did so broke all eight other targets at once, plus every partner build, because
     * implplatform ships. */
    uint8_t effectiveMax = (platformMaxServers < IOT_NTP_MAX_SERVERS) ? platformMaxServers
                                                                     : IOT_NTP_MAX_SERVERS;
    if (effectiveMax == 0)
    {
        effectiveMax = 1; /* one server is the minimum that can possibly work */
    }

    /* Leave lwIP's slot 0 to the DHCP-supplied server -- but never when that would spend
     * the ONLY slot. DHCP is the less reliable source (measured 34 s to sync against 1-4 s
     * for a configured IP, on a router that advertises option 42 with no value), and a
     * plan with no server of ours cannot fall back. So on a one-slot platform the slot is
     * ours and DHCP is off. */
    plan->indexOfFirstServer = (dhcpSupported && effectiveMax > 1) ? 1 : 0;
    plan->serverFromDhcp = (plan->indexOfFirstServer == 1);

    /* Our names occupy lwIP slots [indexOfFirstServer, effectiveMax). IDF checks
     * num_of_servers against SNTP_MAX_SERVERS but ignores the offset, so reserve here. */
    const uint8_t capacity = (uint8_t) (effectiveMax - plan->indexOfFirstServer);

    uint8_t next = 0;
    for (size_t i = 0; i < cfgCount && next < capacity; i++)
    {
        const char *name = (cfg != NULL) ? cfg[i] : NULL;
        if (name == NULL || name[0] == '\0')
        {
            continue; /* skip, leaving no hole */
        }
        plan->servers[next++] = name;
    }

    /* Never hand back an empty list -- see the header. */
    if (next == 0)
    {
        plan->servers[next++] = IOT_NTP_FALLBACK_SERVER;
    }

    plan->numOfServers = next;
}

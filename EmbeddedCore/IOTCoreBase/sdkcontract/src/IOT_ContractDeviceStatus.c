/*
 * The device status request, answered for the product. Every field of the status is generic, so a
 * product that leaves the request unclaimed still gets a full answer instead of "not supported".
 * The product may still answer it itself (the contract then sends nothing), or adjust what the
 * contract sends through IOT_ContractDeviceStatusConfigure().
 */
#include "IOT_ContractDeviceStatus.h"

#include <stdint.h>
#include <string.h>

#include "IOT_AppManager.h"
#include "IOT_Contract.h"
#include "IOT_ContractCommit.h"
#include "IOT_CoreHubRoleFlags.h"
#include "IOT_CoreState.h"
#include "IOT_Log.h"
#include "IOT_Osal.h"
#include "IOT_PlatformCapabilities.h"
#include "dao/IOT_DaoMgmt.h"

static const char *TAG = "ContractDevStatus";

/* Storage statistics count entries; one entry is 32 bytes. */
#define STORAGE_ENTRY_BYTES 32u

static IOT_ContractDeviceStatusFn s_adjust;

iot_err_t IOT_ContractDeviceStatusConfigure(IOT_ContractDeviceStatusFn adjust)
{
    s_adjust = adjust;
    return IOT_OK;
}

static uint8_t percentUsed(uint32_t used, uint32_t total)
{
    if (total == 0 || used > total)
        return 0;
    return (uint8_t) (((uint64_t) used * 100u) / total);
}

/* Mesh and Zigbee are claimed only for a hub role the product declared: a transport compiled in
 * is not one the device serves, and the cloud offers what is claimed. Zigbee also needs its
 * coordinator up, as when core answers the cloud's Zigbee probe. */
static void fillProtocolFlags(uint8_t flags[6])
{
    memset(flags, 0, 6);
    flags[0] = IOT_DEVICE_MODE_INDEPENDENT;
    flags[1] = IOT_CoreHasHubRole(IOT_HUB_ROLE_MESH) ? 1 : 0;
    flags[2] = (IOT_CoreHasHubRole(IOT_HUB_ROLE_ZIGBEE) &&
                IOT_PlatformGetServiceHealth(IOT_PLATFORM_SERVICE_ZIGBEE) != IOT_SERVICE_DOWN)
                   ? 1
                   : 0;
}

static void fillStatus(IOT_CoreDeviceStatusData_t *status, char *version, size_t versionSize)
{
    memset(status, 0, sizeof(*status));

    if (IOT_AppManagerGetVersion(version, versionSize) != IOT_OK)
        version[0] = '\0';
    status->firmwareVersion = version;
    status->uptimeSeconds = IOT_OsalGetUptimeUs() / 1000000;
    status->lastActivationTime = IOT_ContractCommitLastActivation();

    uint32_t totalHeap = IOT_OsalGetTotalHeapSize();
    uint32_t freeHeap = IOT_OsalGetFreeHeapSize();
    status->ramUsagePercent = (freeHeap <= totalHeap) ? percentUsed(totalHeap - freeHeap, totalHeap) : 0;
    status->totalRamKB = totalHeap / 1024u;

    uint32_t usedEntries = 0, totalEntries = 0;
    if (IOT_DaoGetStorageStats(&usedEntries, &totalEntries) == IOT_OK)
    {
        status->nvsUsagePercent = percentUsed(usedEntries, totalEntries);
        status->totalNvsKB = (uint32_t) (((uint64_t) totalEntries * STORAGE_ENTRY_BYTES) / 1024u);
    }

    fillProtocolFlags(status->protocolFlags);
}

void IOT_ContractDeviceStatusAnswer(IOT_CoreDeviceEventStatusRequestData_t *req)
{
    /* Any other result is the product's answer (a reply it sent, or one that failed). */
    if (req == NULL || req->result != IOT_ERR_NOT_SUPPORTED)
        return;

    char version[32];
    IOT_CoreDeviceStatusData_t status;
    fillStatus(&status, version, sizeof(version));
    if (s_adjust != NULL)
        s_adjust(&status);

    req->result = IOT_CoreRespondDeviceStatus(&req->ctx, &status);
    if (req->result != IOT_OK)
        /* Routine when the requester has gone (a phone that disconnected before the reply). */
        IOT_LOGW(TAG, "Device status request not answered: %d", (int) req->result);
}

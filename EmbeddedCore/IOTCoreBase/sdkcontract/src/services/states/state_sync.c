/*
 * The full-state sync: every stored device's whole state reported to the cloud, a few seconds
 * after the request to sync. See services/IOT_ContractStateQuery.h.
 */
#include "services/IOT_ContractStateQuery.h"
#include "IOT_ContractInternal.h"

#include <string.h>
#include "IOT_Common.h"
#include "IOT_Core.h"
#include "IOT_Log.h"
#include "IOT_TimerMgmt.h"
#include "dao/IOT_DaoDeviceMgmt.h"

static const char *TAG = "ContractStateSync";

#define STATE_SYNC_DELAY_MS 3000

static bool s_syncPending = false;

static iot_err_t reportDeviceState(uint16_t eid)
{
    IOT_ContractStateReport_t req = {
        .eid = eid,
        .changedElemIds = NULL, /* every element: the full-sync shape */
        .numChangedElems = 0,
        .changedAttrId = 0,
        .pushNotify = false,
        .ctx = {0}, /* local sync: no sender */
    };
    iot_err_t err = IOT_ContractReportState(&req);
    if (err == IOT_OK)
        IOT_LOGI(TAG, "EID %04x: state reported", eid);
    return err;
}

iot_err_t IOT_ContractStateReportAllDevices(void)
{
    if (!IOT_CoreIsProvisioned())
    {
        IOT_LOGI(TAG, "Not provisioned, skipping state sync");
        return IOT_OK;
    }

    uint16_t *eids = NULL;
    uint16_t eidCount = 0;
    iot_err_t err = IOT_DaoDeviceGetAllEids(&eids, &eidCount);
    if (err != IOT_OK || eids == NULL || eidCount == 0)
    {
        IOT_LOGI(TAG, "No devices registered, skipping state sync");
        SAFE_FREE(eids);
        return IOT_OK;
    }

    IOT_LOGI(TAG, "State sync started: %d device(s)", eidCount);
    for (uint16_t i = 0; i < eidCount; i++)
        reportDeviceState(eids[i]);
    IOT_LOGI(TAG, "State sync complete");

    SAFE_FREE(eids);
    return IOT_OK;
}

static void stateSyncWorker(void *arg)
{
    (void) arg;
    /* Cleared here, not in the timer callback: if the defer had failed the sync would still be
     * pending, and a schedule call landing between that failure and its retry must not queue a
     * second run. */
    s_syncPending = false;
    IOT_ContractStateReportAllDevices();
}

static void stateSyncTimerCb(void *userData)
{
    (void) userData;

    /* Hand off to the core work task: walking every device with store reads and publishes on the
     * timer task overruns its stack. */
    if (IOT_CoreDeferWork(stateSyncWorker, NULL) != IOT_OK)
    {
        IOT_LOGW(TAG, "Core work queue full, retrying state sync in 3s");
        if (IOT_TimerInsertCb(stateSyncTimerCb, NULL, STATE_SYNC_DELAY_MS) == IOT_TIMER_HANDLE_INVALID)
        {
            IOT_LOGE(TAG, "Timer insert failed, abandoning state sync retry");
            s_syncPending = false; /* let the next schedule start a fresh cycle */
        }
    }
}

void IOT_ContractStateSyncSchedule(void)
{
    if (s_syncPending)
    {
        IOT_LOGI(TAG, "State sync already pending, skipping");
        return;
    }

    s_syncPending = true;
    IOT_LOGI(TAG, "Scheduling state sync in 3s");
    if (IOT_TimerInsertCb(stateSyncTimerCb, NULL, STATE_SYNC_DELAY_MS) == IOT_TIMER_HANDLE_INVALID)
    {
        IOT_LOGE(TAG, "Timer insert failed, abandoning state sync schedule");
        s_syncPending = false;
    }
}

/*
 * Restore at boot. The root device's stored controls go back to the hardware through apply, with
 * source IOT_CONTRACT_SOURCE_RESTORE, and nothing else happens: no store write (except the off
 * policy's on/off), no Record, no report, no Smart, no contract event. The cloud learns the state
 * from the full report the device sends when it connects.
 */
#include "IOT_ContractRestore.h"

#include <string.h>

#include "IOT_ContractAttrsInternal.h"
#include "IOT_ContractCommit.h"
#include "IOT_ContractElements.h"
#include "IOT_Core.h"
#include "IOT_CoreEventTypes.h"
#include "IOT_Log.h"
#include "dao/IOT_DaoDeviceMgmt.h"

static const char *TAG = "ContractRestore";

static IOT_ContractRestorePolicy_t s_policy = IOT_CONTRACT_RESTORE_LAST;

iot_err_t IOT_ContractRestoreConfigure(IOT_ContractRestorePolicy_t policy)
{
    if (policy != IOT_CONTRACT_RESTORE_LAST && policy != IOT_CONTRACT_RESTORE_OFF && policy != IOT_CONTRACT_RESTORE_NONE)
        return IOT_ERR_INVALID_ARG;
    s_policy = policy;
    return IOT_OK;
}

static void applyOne(const IOT_ContractCallbacks_t *cb, uint16_t rootEid, uint16_t deviceElemId, uint16_t attrId,
                     const uint8_t *value, uint16_t valueLen)
{
    IOT_ContractApply_t req = {
        .eid = rootEid,
        .elemId = deviceElemId,
        .attrId = attrId,
        .value = value,
        .valueLen = valueLen,
        .source = IOT_CONTRACT_SOURCE_RESTORE,
    };
    iot_err_t err = cb->apply(&req);
    if (err != IOT_OK)
        IOT_LOGW(TAG, "element %u attribute 0x%04x not restored: %d", (unsigned) deviceElemId, (unsigned) attrId,
                 (int) err);
}

/* Policy off: on/off goes to off in the store, then to the hardware. */
static void restoreOff(const IOT_ContractCallbacks_t *cb, uint16_t rootEid, uint16_t cloudElemId, uint16_t deviceElemId)
{
    static const uint8_t off[2] = {0x00, 0x00};
    IOT_ContractCommit_t commit = {
        .eid = rootEid,
        .elemIds = &cloudElemId,
        .numElems = 1,
        .attrId = IOT_ATTR_ONOFF,
        .value = off,
        .valueLen = sizeof(off),
        .senderFrom = IOT_SENDER_DEVICE,
    };
    if (IOT_ContractCommitStoreOnly(&commit) != IOT_OK)
        IOT_LOGW(TAG, "element %u: on/off not stored as off", (unsigned) deviceElemId);
    applyOne(cb, rootEid, deviceElemId, IOT_ATTR_ONOFF, off, sizeof(off));
}

void IOT_ContractRestoreRun(const IOT_ContractCallbacks_t *cb)
{
    if (s_policy == IOT_CONTRACT_RESTORE_NONE || cb == NULL || cb->apply == NULL)
        return;
    if (!IOT_CoreIsProvisioned())
        return; /* the hardware's power-on state stands */
    uint16_t rootEid = 0;
    if (IOT_CoreGetRootEid(&rootEid) != IOT_OK)
        return;

    IOT_DaoDeviceElemsAttrsInfo_t state;
    memset(&state, 0, sizeof(state));
    if (IOT_DaoDeviceGetDeviceElemsState(rootEid, &state) != IOT_OK)
    {
        IOT_LOGI(TAG, "nothing stored to restore");
        return;
    }

    unsigned restored = 0;
    for (uint16_t e = 0; e < state.numOfElem; e++)
    {
        const IOT_DaoDeviceStateElemState_t *elem = &state.elemList[e];
        uint16_t deviceElemId = 0;
        if (!IOT_ContractElemCloudToDevice(rootEid, elem->elemId, &deviceElemId))
            continue; /* not one of the product's elements */
        for (uint8_t a = 0; a < elem->numOfAttr; a++)
        {
            const IOT_DaoDeviceStateAttr_t *attr = &elem->attrList[a];
            if (attr->attrValue == NULL || attr->attrSize == 0)
                continue;
            if (s_policy == IOT_CONTRACT_RESTORE_OFF)
            {
                if (attr->attrId == IOT_ATTR_ONOFF)
                {
                    restoreOff(cb, rootEid, elem->elemId, deviceElemId);
                    restored++;
                }
                continue;
            }
            if (!IOT_AttrIsRestorable(attr->attrId))
                continue;
            applyOne(cb, rootEid, deviceElemId, attr->attrId, attr->attrValue, attr->attrSize);
            restored++;
        }
    }
    IOT_DaoDeviceFreeElemsInfo(&state);
    IOT_LOGI(TAG, "%u control(s) restored (%s)", restored, s_policy == IOT_CONTRACT_RESTORE_OFF ? "off" : "last state");
}

/* Core's work queue frees a job's argument after running it, so the callbacks are kept here and
 * the job carries none. */
static const IOT_ContractCallbacks_t *s_restoreCb = NULL;

static void restoreWork(void *arg)
{
    (void) arg;
    IOT_ContractRestoreRun(s_restoreCb);
}

void IOT_ContractRestoreSchedule(const IOT_ContractCallbacks_t *cb)
{
    if (s_policy == IOT_CONTRACT_RESTORE_NONE)
        return;
    s_restoreCb = cb;
    if (IOT_CoreDeferWork(restoreWork, NULL) != IOT_OK)
    {
        IOT_LOGW(TAG, "core task busy: restoring on this task");
        IOT_ContractRestoreRun(cb);
    }
}

#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"

#include <string.h>

#include "IOT_Common.h"
#include "IOT_CoreReporter.h"
#include "IOT_Log.h"
#include "IOT_Memory.h"
#include "dao/IOT_DaoDeviceMgmt.h"

static const char *TAG = "SDK_STATE_RPT";

/* A batched multi-device report is built elsewhere and emits ONE frame carrying every affected
 * device, which is a different message shape. Adding a field to
 * IOT_CoreStateChangeDeviceData_t means changing both construction sites, or the batched report
 * silently ships the field zeroed. */

iot_err_t IOT_ContractReportState(const IOT_ContractStateReport_t *req)
{
    if (req == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    iot_err_t iot_err = IOT_OK;
    IOT_DaoDeviceElemsAttrsInfo_t fullState = {0};
    IOT_DevIdentityInfo_t identity = {0};
    uint16_t *ownedElemIds = NULL; /* allocated only for the all-elements case */
    const uint16_t *elemIds = req->changedElemIds;
    uint16_t numElems = req->numChangedElems;
    uint16_t changedAttrId = req->changedAttrId;

    iot_err = IOT_DaoDeviceGetDeviceElemsState(req->eid, &fullState);
    if (iot_err != IOT_OK)
    {
        IOT_LOGW(TAG, "EID %04x: no element state (%d), not reporting", req->eid, iot_err);
        /* Normalized so every "could not read state/identity" cause looks the same
         * to callers — some of them (e.g. the device-control path)
         * gate a side effect (smart trigger evaluation) on "was the state actually
         * read", and must be able to tell that apart from "read succeeded, but the
         * subsequent report send failed" without matching on every possible DAO
         * error code. */
        return IOT_ERR_DATA_NOT_FOUND;
    }
    if (fullState.numOfElem == 0 || fullState.elemList == NULL)
    {
        IOT_LOGW(TAG, "EID %04x: empty element state, not reporting", req->eid);
        IOT_DaoDeviceFreeElemsInfo(&fullState);
        return IOT_ERR_DATA_NOT_FOUND;
    }

    iot_err = IOT_DaoDeviceGetIdentityInfo(req->eid, &identity);
    if (iot_err != IOT_OK)
    {
        IOT_LOGW(TAG, "EID %04x: no identity info (%d), not reporting", req->eid, iot_err);
        IOT_DaoDeviceFreeElemsInfo(&fullState);
        /* Same normalization as the state-read failure above. */
        return IOT_ERR_DATA_NOT_FOUND;
    }

    /* NULL means every element this device has — the full-sync shape. Build the
     * id list here rather than making each caller do it, and name the first
     * attribute of the first element as the full-sync report expects. */
    if (elemIds == NULL)
    {
        ownedElemIds = (uint16_t *) Mem_SafeMalloc(sizeof(uint16_t) * fullState.numOfElem,
                                                   TAG, "changedElemIds");
        if (ownedElemIds == NULL)
        {
            iot_err = IOT_ERR_NO_MEM;
            goto cleanup;
        }
        for (uint16_t i = 0; i < fullState.numOfElem; i++)
        {
            ownedElemIds[i] = fullState.elemList[i].elemId;
        }
        elemIds = ownedElemIds;
        numElems = fullState.numOfElem;

        changedAttrId = 0;
        if (fullState.elemList[0].numOfAttr > 0 && fullState.elemList[0].attrList != NULL)
        {
            changedAttrId = fullState.elemList[0].attrList[0].attrId;
        }
    }

    {
        IOT_CoreStateChangeDeviceData_t deviceData = {
            .eid = req->eid,
            .mac = identity.mac,
            .macLen = identity.macSize,
            .devId = identity.devId,
            .devIdLen = IOT_DAO_DEVICE_ID_LEN,
            .fullState = (devStateElemsInfo_t *) &fullState,
            .numChangedElems = numElems,
            .changedElemIds = (uint16_t *) elemIds,
            .changedAttrId = changedAttrId,
            .pushNotify = req->pushNotify,
        };
        IOT_CoreStateChangeReport_t report = {
            .devices = &deviceData,
            .numDevices = 1,
            .ctx = req->ctx, /* pass-through only — never stored */
        };

        iot_err = IOT_CoreRequestReportStateChanged(&report);
        if (iot_err != IOT_OK)
        {
            IOT_LOGE(TAG, "EID %04x: state report failed: %d", req->eid, iot_err);
        }
    }

cleanup:
    SAFE_FREE(ownedElemIds);
    IOT_DaoDeviceFreeElemsInfo(&fullState);
    IOT_DaoDeviceFreeIdentityInfo(&identity);
    return iot_err;
}

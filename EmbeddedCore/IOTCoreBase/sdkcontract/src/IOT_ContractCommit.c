/*
 * The contract's commit: store, Record, (the caller's report), Smart. See IOT_ContractCommit.h.
 *
 * Store comes before Record on every path, so a failed store write never leaves a history entry
 * for a change the device did not keep.
 */
#include "IOT_ContractCommit.h"

#include <string.h>
#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"
#include "IOT_ContractEvents.h"
#include "IOT_Log.h"
#include "IOT_Memory.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "dao/IOT_DaoStoresInternal.h"

static const char *TAG = "ContractCommit";

static bool commitIsValid(const IOT_ContractCommit_t *c)
{
    return c != NULL && c->elemIds != NULL && c->numElems > 0 && (c->value != NULL || c->valueLen == 0);
}

static IOT_ContractStateChange_t asChange(const IOT_ContractCommit_t *c)
{
    return (IOT_ContractStateChange_t) {
        .eid = c->eid,
        .elemIds = c->elemIds,
        .numElems = c->numElems,
        .attrId = c->attrId,
        .value = c->value,
        .valueLen = c->valueLen,
        .senderFrom = c->senderFrom,
    };
}

/* The Device store's shape for this change: one attribute per element, the same value on each.
 * The caller frees with IOT_DaoDeviceFreeElemsInfo() whether this succeeds or fails. */
static iot_err_t buildStoreState(const IOT_ContractCommit_t *c, IOT_DaoDeviceElemsAttrsInfo_t *out)
{
    out->elemList = (IOT_DaoDeviceStateElemState_t *) Mem_SafeCalloc(c->numElems, sizeof(IOT_DaoDeviceStateElemState_t),
                                                                      TAG, "elemList");
    if (out->elemList == NULL)
        return IOT_ERR_NO_MEM;
    out->numOfElem = c->numElems;

    for (uint16_t i = 0; i < c->numElems; i++)
    {
        IOT_DaoDeviceStateElemState_t *elem = &out->elemList[i];
        elem->elemId = c->elemIds[i];
        elem->attrList = (IOT_DaoDeviceStateAttr_t *) Mem_SafeCalloc(1, sizeof(IOT_DaoDeviceStateAttr_t), TAG,
                                                                      "attrList");
        if (elem->attrList == NULL)
            return IOT_ERR_NO_MEM;
        elem->numOfAttr = 1;
        elem->attrList[0].attrId = c->attrId;
        elem->attrList[0].attrSize = c->valueLen;
        if (c->valueLen == 0)
            continue;
        elem->attrList[0].attrValue = (uint8_t *) Mem_SafeMalloc(c->valueLen, TAG, "attrValue");
        if (elem->attrList[0].attrValue == NULL)
            return IOT_ERR_NO_MEM;
        memcpy(elem->attrList[0].attrValue, c->value, c->valueLen);
    }
    return IOT_OK;
}

iot_err_t IOT_ContractCommitStore(const IOT_ContractCommit_t *c)
{
    if (!commitIsValid(c))
        return IOT_ERR_INVALID_ARG;

    IOT_DaoDeviceElemsAttrsInfo_t state = {0};
    iot_err_t err = buildStoreState(c, &state);
    if (err == IOT_OK)
        err = IOT_DaoDeviceUpdateElementState(c->eid, &state);
    IOT_DaoDeviceFreeElemsInfo(&state);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "EID %04x: state not stored (%d), not recorded", c->eid, (int) err);
        return err;
    }

    IOT_ContractStateChange_t change = asChange(c);
    (void) IOT_ContractRecordStateChange(&change);
    return IOT_OK;
}

void IOT_ContractCommitAnnounce(const IOT_ContractCommit_t *c, IOT_ContractCause_t cause)
{
    if (!commitIsValid(c))
        return;

    IOT_ContractStateChange_t change = asChange(c);
    (void) IOT_ContractEvaluateSmart(&change);

    /* Last: the change is stored, recorded, reported and with Smart when subscribers hear of it. */
    IOT_ContractEventStateCommitted(c->eid, c->elemIds, c->numElems, c->attrId, c->value, c->valueLen,
                                    c->senderFrom, cause);
}

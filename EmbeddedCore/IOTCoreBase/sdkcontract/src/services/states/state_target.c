/*
 * A state command's target (device, voice, group, location) expanded to the device EIDs it
 * addresses on this device.
 */
#include "services/IOT_ContractStateSet.h"

#include <string.h>

#include "IOT_Log.h"
#include "IOT_Memory.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "dao/IOT_DaoGroupMgmt.h"
#include "dao/IOT_DaoStoresInternal.h"

static const char *TAG = "ContractTarget";

void IOT_ContractStateFreeTargets(IOT_ContractStateTargets_t *set)
{
    if (set == NULL)
    {
        return;
    }
    if (set->owned)
    {
        SAFE_FREE(set->eids);
    }
    set->eids = NULL;
    set->count = 0;
    set->owned = false;
    IOT_DaoGroupFreeMemberList(&set->members);
}

iot_err_t IOT_ContractStateResolveTargets(const IOT_EventStateSetRequest_t *setData, IOT_ContractStateTargets_t *out)
{
    if (setData == NULL || out == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    switch (setData->targetType)
    {
    case IOT_SET_TARGET_DEVICE:
    {
        out->inlineEid = setData->target.device.eid;
        out->eids = &out->inlineEid;
        out->count = 1;
        return IOT_OK;
    }

    case IOT_SET_TARGET_LOCATION:
    {
        /* All devices — resolved from the device DAO, independent of group membership. */
        uint16_t *eids = NULL;
        uint16_t allCount = 0;
        iot_err_t err = IOT_DaoDeviceGetAllEids(&eids, &allCount);
        if (err != IOT_OK || allCount == 0)
        {
            IOT_LOGI(TAG, "LOCATION SET_STATE: no devices in DAO, ignoring");
            SAFE_FREE(eids);
            return IOT_OK;
        }
        out->eids = eids;
        out->count = allCount;
        out->owned = true;
        return IOT_OK;
    }

    case IOT_SET_TARGET_GROUP:
    {
        iot_err_t err = IOT_DaoGroupGetMemberList(setData->target.group.groupEid, &out->members);
        if (err == IOT_ERR_DATA_NOT_FOUND || out->members.count == 0)
        {
            IOT_LOGI(TAG, "GROUP SET_STATE: not a member of group %04x, ignoring",
                     setData->target.group.groupEid);
            return IOT_OK;
        }
        if (err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to expand group %04x", setData->target.group.groupEid);
            return err;
        }

        out->eids = (uint16_t *) Mem_SafeMalloc(sizeof(uint16_t) * out->members.count, TAG,
                                                "groupEidList");
        if (out->eids == NULL)
        {
            return IOT_ERR_NO_MEM;
        }
        out->owned = true;
        out->count = out->members.count;
        for (uint16_t i = 0; i < out->members.count; i++)
        {
            out->eids[i] = out->members.members[i].eid;
            IOT_LOGI(TAG, "  group member[%d]: eid=%04x elemBindLen=%d", i,
                     out->members.members[i].eid, out->members.members[i].elemDataLen);
        }
        return IOT_OK;
    }

    case IOT_SET_TARGET_VOICE:
    {
        /* Core already routes a voice command that names the location or a group to
         * LOCATION/GROUP, so VOICE here always means a single device resolved from devId. */
        if (!IOT_DaoDeviceGetEidByDevId(setData->target.voice.devId, &out->inlineEid))
        {
            IOT_LOGE(TAG, "VOICE SET_STATE: device ID not found");
            return IOT_ERR_NOT_FOUND;
        }
        IOT_LOGI(TAG, "VOICE SET_STATE: resolved to EID %04x", out->inlineEid);
        out->eids = &out->inlineEid;
        out->count = 1;
        return IOT_OK;
    }
    }

    return IOT_ERR_INVALID_ARG;
}

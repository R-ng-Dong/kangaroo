/*
 * The contract's DEVICE events: join, removal, group bind/unbind and the device attribute set.
 * See services/IOT_ContractDevice.h.
 */
#include "services/IOT_ContractDevice.h"
#include "IOT_ContractInternal.h"

#include <string.h>

#include "IOT_Common.h"
#include "IOT_ContractCommit.h"
#include "IOT_ContractElements.h"
#include "IOT_ContractEvents.h"
#include "IOT_Core.h"
#include "IOT_CoreReporter.h"
#include "IOT_Log.h"
#include "IOT_Memory.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "dao/IOT_DaoStoresInternal.h"
#include "dao/IOT_DaoGroupMgmt.h"
#include "defines/IOT_AttrDefines.h" /* attribute sizes and defaults: the product's attribute table */

static const char *TAG = "ContractDevice";

#define MAC_LEN 6

static void respond(IOT_DeviceCmdType_t cmdType, uint8_t cmdData2, iot_err_t result, IOT_RequestContext_t ctx)
{
    IOT_CoreDeviceCmdResponse_t rsp = {
        .cmdType = cmdType,
        .cmdData2 = cmdData2,
        .result = result,
        .ctx = ctx,
    };
    IOT_CoreRespondDeviceCmd(&rsp);
}

/* ---- removal ---- */

/* The product releases what it holds for the device while the record still exists; the record is
 * gone after the store delete. */
static iot_err_t removeOneDevice(uint16_t eid, const IOT_ContractCallbacks_t *cb)
{
    if (cb->onDeviceRemoving != NULL)
        cb->onDeviceRemoving(eid, false);
    return IOT_ContractRemoveDevice(eid) == IOT_OK ? IOT_OK : IOT_ERR_FAIL;
}

/* The removal is over: subscribers hear of it, then the product's hook (which may restart). */
static void removalDone(uint16_t eid, bool root, iot_err_t err, const IOT_ContractCallbacks_t *cb)
{
    if (err == IOT_OK)
        IOT_ContractEventDeviceRemoved(eid, root);
    if (cb->onDeviceRemoved != NULL)
        cb->onDeviceRemoved(eid, root);
}

static iot_err_t handleRemoved(const IOT_CoreDeviceEventDeviceRemovedData_t *d, const IOT_ContractCallbacks_t *cb)
{
    iot_err_t err = IOT_OK;
    IOT_LOGI(TAG, "Device removed: EID %04x, root %d", d->eid, (int) d->isRootDevice);

    if (d->isRootDevice)
    {
        /* This device itself: every contract store goes. The product resets its own data and
         * restarts from onDeviceRemoved, after core has its answer. */
        if (cb->onDeviceRemoving != NULL)
            cb->onDeviceRemoving(d->eid, true);
        (void) IOT_ContractFactoryReset();
    }
    else
    {
        err = removeOneDevice(d->eid, cb);
    }

    respond(IOT_DEVICE_CMD_SYNC_DEV_REMOVED, d->cmdData2, err, d->ctx);

    removalDone(d->eid, d->isRootDevice, err, cb);
    return err;
}

/* ---- join ---- */

static iot_err_t joinDeviceInfo(const IOT_CoreDeviceEventNewDeviceJoinedData_t *join, const IOT_ContractCallbacks_t *cb)
{
    uint16_t eid = join->data.deviceInfo.eid;
    IOT_LOGI(TAG, "Device joined: EID %04x, phase DEVICE_INFO", eid);

    /* The Device store keeps a MAC together with its type and length. */
    uint8_t macWithPrefix[2 + MAC_LEN];
    macWithPrefix[0] = join->data.deviceInfo.macType;
    macWithPrefix[1] = MAC_LEN;
    memcpy(&macWithPrefix[2], join->data.deviceInfo.mac, MAC_LEN);

    /* The same physical device joining again under a new EID: its old entry goes first, through
     * the same hooks as a removal, so the product can release the old one. */
    uint16_t existingEid = 0;
    if (IOT_DaoDeviceIsDeviceExistByMac(macWithPrefix, &existingEid) && existingEid != eid)
    {
        IOT_LOGW(TAG, "Device with this MAC already stored (EID %04x), removing the old entry", existingEid);
        iot_err_t evictErr = removeOneDevice(existingEid, cb);
        removalDone(existingEid, false, evictErr, cb);
    }

    IOT_DevCommonInfo_t commonInfo = {
        .deviceType = join->data.deviceInfo.deviceType,
        .protocol = join->data.deviceInfo.protocol,
        .nwkAddr = join->data.deviceInfo.nwkAddr,
        .rootEid = join->data.deviceInfo.rootEid,
    };
    iot_err_t err = IOT_DaoDeviceSetCommonInfo(eid, &commonInfo);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to set common info for EID %04x", eid);
        return err;
    }

    /* The product indexes the device from its common info (protocol, network address) as soon as
     * it is stored, so a frame from the device can be attributed before the join finishes. */
    if (cb->onDeviceJoined != NULL)
        cb->onDeviceJoined(eid, IOT_DEV_JOIN_PHASE_DEVICE_INFO);

    IOT_DevIdentityInfo_t identityInfo = {
        .mac = macWithPrefix,
        .macType = join->data.deviceInfo.macType,
        .macSize = MAC_LEN,
    };
    memcpy(identityInfo.devId, join->data.deviceInfo.devId, IOT_DAO_DEVICE_ID_LEN);
    err = IOT_DaoDeviceSetIdentityInfo(eid, &identityInfo);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to set identity info for EID %04x", eid);
        return err;
    }

    /* The location id is not a real group: location control reads every stored device, so it
     * needs no group entry. */
    uint16_t groupId = join->data.deviceInfo.groupId;
    if (groupId != IOT_GROUP_EID_BASE)
    {
        err = IOT_DaoGroupAddDeviceToGroup(groupId, eid, NULL, 0);
        if (err != IOT_OK)
        {
            IOT_LOGE(TAG, "Failed to add device EID %04x to group %04x", eid, groupId);
            return err;
        }
    }
    return IOT_OK;
}

/* One element's attribute list with the table's sizes and default values. The caller frees it
 * with freeAttrList() on every path. */
static iot_err_t buildAttrList(uint16_t eid, const IOT_DevJoinElmInfo_t *elmInfo, IOT_DaoDeviceStateAttr_t **out)
{
    (void) eid; /* read only by the log line, which a build can compile out */
    *out = NULL;
    if (elmInfo->numOfAttr == 0)
        return IOT_OK;

    IOT_DaoDeviceStateAttr_t *list = (IOT_DaoDeviceStateAttr_t *) Mem_SafeCalloc(
        elmInfo->numOfAttr, sizeof(IOT_DaoDeviceStateAttr_t), TAG, "join attr list");
    if (list == NULL)
    {
        IOT_LOGE(TAG, "Failed to allocate attr list for element %04x", elmInfo->elemId);
        return IOT_ERR_NO_MEM;
    }
    *out = list;

    for (uint8_t a = 0; a < elmInfo->numOfAttr; a++)
    {
        uint16_t attrId = elmInfo->attrIdList[a];
        uint16_t attrSize = IOT_AttrGetSize(attrId);

        /* The table cannot say "I do not know this one": an attribute it does not define gets the
         * default size, zero-filled, and is stored like any other. Say so, once per attribute per
         * join, in hex and decimal (the cloud's tools speak decimal). */
        if (!IOT_AttrIsKnown(attrId))
        {
            IOT_LOGW(TAG,
                     "EID %04x elem %04x: cloud declared attribute 0x%04x (%u) that the attribute "
                     "table does not define -- assuming %u bytes, the value may be truncated",
                     eid, elmInfo->elemId, attrId, (unsigned) attrId, (unsigned) attrSize);
        }

        list[a].attrId = attrId;
        list[a].attrSize = attrSize;
        if (attrSize == 0)
            continue;
        list[a].attrValue = (uint8_t *) Mem_SafeCalloc(1, attrSize, TAG, "join attr value");
        if (list[a].attrValue == NULL)
        {
            IOT_LOGE(TAG, "Failed to allocate attr value for attr %04x", attrId);
            return IOT_ERR_NO_MEM;
        }
        IOT_AttrFillDefault(attrId, list[a].attrValue);
    }
    return IOT_OK;
}

static void freeAttrList(IOT_DaoDeviceStateAttr_t *list, uint8_t count)
{
    if (list == NULL)
        return;
    for (uint8_t a = 0; a < count; a++)
        SAFE_FREE(list[a].attrValue);
    SAFE_FREE(list);
}

static iot_err_t joinElmInfo(const IOT_CoreDeviceEventNewDeviceJoinedData_t *join)
{
    uint16_t eid = join->data.elmInfo.eid;
    IOT_LOGI(TAG, "Device joined: EID %04x, phase ELM_INFO", eid);

    for (uint8_t i = 0; i < join->data.elmInfo.numOfElms; i++)
    {
        const IOT_DevJoinElmInfo_t *elmInfo = &join->data.elmInfo.elmList[i];

        IOT_DaoDeviceStateAttr_t *attrList = NULL;
        iot_err_t err = buildAttrList(eid, elmInfo, &attrList);
        if (err == IOT_OK)
        {
            IOT_DaoDeviceStateElemState_t elemData = {
                .elemId = elmInfo->elemId,
                .elemType = elmInfo->elemType,
                .numOfAttr = elmInfo->numOfAttr,
                .attrList = attrList,
            };
            err = IOT_DaoDeviceAddElementInfo(eid, &elemData);
            if (err != IOT_OK)
                IOT_LOGE(TAG, "Failed to add element info for EID %04x, elem %04x", eid, elmInfo->elemId);
        }
        freeAttrList(attrList, elmInfo->numOfAttr);
        if (err != IOT_OK)
            return err;
    }
    return IOT_OK;
}

static iot_err_t joinSyncComplete(const IOT_CoreDeviceEventNewDeviceJoinedData_t *join)
{
    uint16_t eid = join->data.syncComplete.eid;
    IOT_LOGI(TAG, "Device joined: EID %04x, phase SYNC_COMPLETE", eid);

    if (join->data.syncComplete.meshKey == NULL || join->data.syncComplete.meshKeyLen == 0)
        return IOT_OK;

    iot_err_t err =
        IOT_DaoDeviceSetMeshKey(eid, join->data.syncComplete.meshKey, (uint8_t) join->data.syncComplete.meshKeyLen);
    if (err != IOT_OK)
        IOT_LOGE(TAG, "Failed to save the network key for EID %04x", eid);
    return err;
}

static iot_err_t handleJoined(const IOT_CoreDeviceEventNewDeviceJoinedData_t *join, const IOT_ContractCallbacks_t *cb)
{
    iot_err_t err = IOT_OK;
    uint16_t eid = 0;

    switch (join->phase)
    {
    case IOT_DEV_JOIN_PHASE_DEVICE_INFO:
        /* Calls onDeviceJoined itself, once the common info is stored. Unchanged from the handler
         * this replaces: a device whose information could not be stored is not answered. */
        err = joinDeviceInfo(join, cb);
        if (err != IOT_OK)
            return err;
        break;
    case IOT_DEV_JOIN_PHASE_ELM_INFO:
        eid = join->data.elmInfo.eid;
        err = joinElmInfo(join);
        if (err == IOT_OK && cb->onDeviceJoined != NULL)
            cb->onDeviceJoined(eid, join->phase);
        break;
    case IOT_DEV_JOIN_PHASE_SYNC_COMPLETE:
        /* The product's follow-up work runs whatever the key save did; core hears the result. */
        eid = join->data.syncComplete.eid;
        err = joinSyncComplete(join);
        if (cb->onDeviceJoined != NULL)
            cb->onDeviceJoined(eid, join->phase);
        break;
    default:
        IOT_LOGW(TAG, "Device joined: unknown phase %d", (int) join->phase);
        break;
    }

    respond(IOT_DEVICE_CMD_SYNC_DEV_JOINED, join->cmdData2, err, join->ctx);

    /* The join is over once its last phase is answered as a success. */
    if (join->phase == IOT_DEV_JOIN_PHASE_SYNC_COMPLETE && err == IOT_OK)
        IOT_ContractEventDeviceAdded(eid);
    return err;
}

/* ---- groups ---- */

static iot_err_t handleGroupBind(const IOT_CoreDeviceEventGroupBindData_t *b)
{
    iot_err_t err = IOT_OK;
    IOT_LOGI(TAG, "Group bind: EID %04x, new group %04x, old group %04x, %u element bytes", b->eid, b->newGroupId,
             b->oldGroupId, (unsigned) b->elemDataLen);

    if (b->oldGroupId != 0)
    {
        err = IOT_DaoGroupRemoveDeviceFromGroup(b->oldGroupId, b->eid);
        if (err != IOT_OK)
            IOT_LOGW(TAG, "Failed to remove EID %04x from old group %04x", b->eid, b->oldGroupId);
    }
    else if (b->newGroupId > IOT_GROUP_EID_SECONDARY)
    {
        err = IOT_DaoGroupRemoveDeviceFromCustomGroups(b->eid);
        if (err != IOT_OK)
            IOT_LOGW(TAG, "Failed to clear stale custom groups for EID %04x", b->eid);
    }

    if (b->newGroupId != 0)
    {
        err = IOT_DaoGroupAddDeviceToGroup(b->newGroupId, b->eid, b->elemData, b->elemDataLen);
        if (err != IOT_OK)
            IOT_LOGE(TAG, "Failed to add EID %04x to group %04x", b->eid, b->newGroupId);
    }

    respond(b->elemDataLen > 0 ? IOT_DEVICE_CMD_BIND_GRP_ELMS : IOT_DEVICE_CMD_BIND_GRP, b->cmdData2, err, b->ctx);
    return err;
}

static iot_err_t handleGroupUnbind(const IOT_CoreDeviceEventGroupUnbindData_t *u)
{
    IOT_LOGI(TAG, "Group unbind: EID %04x, group %04x", u->eid, u->groupId);
    iot_err_t err = IOT_DaoGroupRemoveDeviceFromGroup(u->groupId, u->eid);
    if (err != IOT_OK)
        IOT_LOGW(TAG, "Failed to remove EID %04x from group %04x", u->eid, u->groupId);
    respond(IOT_DEVICE_CMD_UNBOUND_GRP, u->cmdData2, err, u->ctx);
    return err;
}

/* ---- device attribute set: [apply] -> commit -> answer -> Smart ---- */

static iot_err_t handleAttrSet(const IOT_CoreDeviceEventAttrSetData_t *a, const IOT_ContractCallbacks_t *cb)
{
    IOT_LOGI(TAG, "Attribute set: EID %04x, elem %04x, attr %04x, %u bytes", a->eid, a->elm, a->attrId,
             (unsigned) a->attrDataLen);

    iot_err_t err = IOT_OK;
    uint16_t cloudElemId = a->elm;
    IOT_ContractCommit_t commit = {
        .eid = a->eid,
        .elemIds = &cloudElemId,
        .numElems = 1,
        .attrId = a->attrId,
        .value = a->attrData,
        .valueLen = a->attrDataLen,
        .senderFrom = a->ctx.senderFrom,
    };

    IOT_ContractApply_t req = {
        .eid = a->eid,
        .attrId = a->attrId,
        .value = a->attrData,
        .valueLen = a->attrDataLen,
        .source = IOT_ContractSourceFromSender(a->ctx.senderFrom),
    };

    /* Only the device itself is driven. A sub-device's attribute is stored without being sent to
     * it; without the root's EID the two cannot be told apart, so that takes the store-only path
     * too rather than driving hardware that may not be this device's. */
    uint16_t rootEid = 0;
    bool isRoot = (IOT_CoreGetRootEid(&rootEid) == IOT_OK && a->eid == rootEid);

    /* An element the product did not declare is never applied or stored (the map logs it). */
    if (!IOT_ContractElemCloudToDevice(a->eid, cloudElemId, &req.elemId))
    {
        err = IOT_ERR_NOT_FOUND;
    }
    else if (isRoot)
    {
        err = cb->apply(&req);
        /* Not driven by this product: stored all the same, as a state command does. */
        if (err == IOT_ERR_NOT_FOUND)
            err = IOT_OK;
        if (err != IOT_OK)
            IOT_LOGW(TAG, "EID %04x elem %04x attr %04x not applied (%d)", a->eid, cloudElemId, a->attrId, (int) err);
    }

    if (err == IOT_OK)
        err = IOT_ContractCommitStore(&commit);

    respond(IOT_DEVICE_CMD_DEVICE_ATTR, 0, err, a->ctx);

    if (err == IOT_OK)
        IOT_ContractCommitAnnounce(&commit, IOT_CONTRACT_CAUSE_ATTRIBUTE_SET);
    return err;
}

iot_err_t IOT_ContractHandleDeviceEvent(IOT_CoreDeviceEvent_t *event, const IOT_ContractCallbacks_t *cb)
{
    if (event == NULL || cb == NULL)
        return IOT_ERR_INVALID_ARG;

    switch (event->type)
    {
    case DEV_EVENT_NEW_DEVICE_JOINED:
        return handleJoined(&event->data.newDeviceJoined, cb);
    case DEV_EVENT_DEVICE_REMOVED:
        return handleRemoved(&event->data.deviceRemoved, cb);
    case DEV_EVENT_GROUP_BIND:
        return handleGroupBind(&event->data.groupBind);
    case DEV_EVENT_GROUP_UNBIND:
        return handleGroupUnbind(&event->data.groupUnbind);
    case DEV_EVENT_DEVICE_ATTR_SET:
        if (cb->apply == NULL)
            return IOT_ERR_INVALID_ARG;
        return handleAttrSet(&event->data.attrSet, cb);
    default:
        return IOT_ERR_NOT_SUPPORTED;
    }
}

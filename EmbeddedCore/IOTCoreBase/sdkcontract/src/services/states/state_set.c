/*
 * The contract's state command: resolve -> accept -> apply -> commit -> one report -> Smart.
 * See services/IOT_ContractStateSet.h for the rules.
 */
#include "services/IOT_ContractStateSet.h"
#include "IOT_ContractInternal.h"

#include <string.h>

#include "IOT_Common.h"
#include "IOT_ContractCommit.h"
#include "IOT_ContractEcho.h"
#include "IOT_ContractElements.h"
#include "IOT_Core.h"
#include "IOT_CoreReporter.h"
#include "IOT_Log.h"
#include "IOT_Memory.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "dao/IOT_DaoGroupMgmt.h"
#include "defines/IOT_AttrDefines.h" /* IOT_AttrIsNotifyWorthy: the product's attribute table */

static const char *TAG = "ContractStateSet";

/**
 * @brief Build the element list for a group or location member.
 *
 * A group or location command carries no per-device element list. Uses the member's element
 * bindings from the group store when there are any (elemInfoLen > 0), otherwise every element
 * the Device store holds for the device.
 *
 * @param outElemList Allocated elem_set_t array (caller frees).
 */
static iot_err_t resolveGroupMemberElements(uint16_t eid, const uint8_t *groupElemData, uint16_t groupElemLen,
                                            elem_set_t **outElemList, uint8_t *outNumElems)
{
    *outElemList = NULL;
    *outNumElems = 0;

    if (groupElemLen > 0 && groupElemData != NULL)
    {
        // Specific element bindings from the group member's element list
        uint8_t count = groupElemLen / 2;
        elem_set_t *list = (elem_set_t *) Mem_SafeMalloc(sizeof(elem_set_t) * count, TAG, "grpElemBind");
        if (list == NULL)
            return IOT_ERR_NO_MEM;
        for (uint8_t i = 0; i < count; i++)
        {
            list[i].elemId = (groupElemData[i * 2] << 8) | groupElemData[i * 2 + 1];
        }
        *outElemList = list;
        *outNumElems = count;
        return IOT_OK;
    }

    // No element bindings: every element in the Device store
    IOT_DaoDeviceElemsAttrsInfo_t elemsInfo = {0};
    iot_err_t err = IOT_DaoDeviceGetDeviceElemsState(eid, &elemsInfo);
    if (err != IOT_OK || elemsInfo.numOfElem == 0)
    {
        IOT_LOGE(TAG, "Group: no elements found for device EID %04x", eid);
        IOT_DaoDeviceFreeElemsInfo(&elemsInfo);
        return IOT_ERR_NOT_FOUND;
    }

    elem_set_t *list = (elem_set_t *) Mem_SafeMalloc(sizeof(elem_set_t) * elemsInfo.numOfElem, TAG, "grpElemAll");
    if (list == NULL)
    {
        IOT_DaoDeviceFreeElemsInfo(&elemsInfo);
        return IOT_ERR_NO_MEM;
    }
    for (uint16_t i = 0; i < elemsInfo.numOfElem; i++)
    {
        list[i].elemId = elemsInfo.elemList[i].elemId;
    }
    *outElemList = list;
    *outNumElems = (uint8_t) elemsInfo.numOfElem;
    IOT_DaoDeviceFreeElemsInfo(&elemsInfo);
    return IOT_OK;
}

/** What identification learned about the addressed device. Every field is written on
 *  the IOT_OK path — callers read them unconditionally. */
typedef struct
{
    bool isRoot;
    uint16_t deviceType;
    uint16_t protocol;
    uint16_t nwkAddr;
} ElemSetDevInfo_t;

/**
 * @brief Is this EID ours, and may this command touch it?
 *
 * Answered FIRST, before a single allocation. A multi-device control carries one element
 * block per device and reaches every device of the location, so every board receives blocks
 * addressed to its neighbours. A foreign EID is not an error, just someone else's block, and is
 * skipped before any work is done for it.
 *
 * The device-type guard lives here too, so nothing with side effects happens before it.
 */
static iot_err_t elemSetIdentifyDevice(uint16_t eid, uint16_t rootEid, const IOT_EventStateSetRequest_t *setData,
                                       ElemSetDevInfo_t *out)
{
    memset(out, 0, sizeof(*out));
    out->isRoot = (eid == rootEid);

    IOT_DevCommonInfo_t commonInfo = {0};
    if (IOT_DaoDeviceGetCommonInfo(eid, &commonInfo) == IOT_OK)
    {
        out->deviceType = commonInfo.deviceType;
        if (!out->isRoot)
        {
            out->protocol = commonInfo.protocol;
            out->nwkAddr = commonInfo.nwkAddr;
        }
    }
    else if (!out->isRoot)
    {
        /* Not on this device. The caller logs the skip; saying it twice per foreign EID would
         * re-create the log spam this check exists to remove. */
        return IOT_ERR_NOT_FOUND;
    }

    uint8_t targetDeviceType = setData->targetDeviceType;
    if (targetDeviceType != 0 && targetDeviceType != out->deviceType)
    {
        IOT_LOGW(TAG, "Device type mismatch for EID %04x: target=%d, actual=%d", eid, targetDeviceType,
                 out->deviceType);
        return IOT_ERR_NOT_SUPPORTED;
    }

    return IOT_OK;
}

/**
 * @brief Hand each element to the product's apply, in order, with the device's own element id.
 *
 * The device's cloud element list is read once for all its elements. An element the product did
 * not declare is skipped (the element map logs it). IOT_ERR_NOT_FOUND from apply means "not driven
 * by this product" and counts as applied. Any other error stops the device: the elements before
 * it stay applied (they are in @p outApplied, by cloud id) and are committed by the caller.
 *
 * @return IOT_OK, or the error that stopped the device.
 */
static iot_err_t elemSetApply(uint16_t eid, const elem_set_t *elems, uint8_t numElems,
                              const IOT_EventStateSetRequest_t *setData, IOT_ContractApplyFn apply,
                              uint16_t *outApplied, uint8_t *outNumApplied)
{
    const dev_set_info_t *devSetInfo = &setData->devSetInfo;
    *outNumApplied = 0;

    IOT_ContractElemMap_t map;
    IOT_ContractElemMapLoad(eid, &map);

    IOT_ContractApply_t req = {
        .eid = eid,
        .attrId = devSetInfo->attrId,
        .value = devSetInfo->attrValue,
        .valueLen = devSetInfo->attrSize,
        .source = IOT_ContractSourceFromSender(setData->ctx.senderFrom),
    };

    for (uint8_t i = 0; i < numElems; i++)
    {
        if (!IOT_ContractElemMapToDevice(&map, eid, elems[i].elemId, &req.elemId))
            continue;

        iot_err_t err = apply(&req);
        if (err != IOT_OK && err != IOT_ERR_NOT_FOUND)
        {
            IOT_LOGW(TAG, "EID %04x elem %04x not applied (%d): the device's remaining elements are skipped", eid,
                     elems[i].elemId, (int) err);
            return err;
        }
        outApplied[(*outNumApplied)++] = elems[i].elemId;
    }
    return IOT_OK;
}

/**
 * @brief One device of the command: identify -> apply -> commit (store, Record) -> read what the
 *        report needs.
 *
 * On IOT_OK the device committed at least one element: @p outApplied / @p outNumApplied name them,
 * and outFullState / outIdentity are filled (the caller frees them).
 */
static iot_err_t handleSingleDeviceSet(uint16_t eid, uint16_t rootEid, bool rootKnown,
                                       const IOT_EventStateSetRequest_t *setData,
                                       const elem_set_t *elems, uint8_t numElems, IOT_ContractApplyFn apply,
                                       uint16_t *outApplied, uint8_t *outNumApplied,
                                       IOT_DaoDeviceElemsAttrsInfo_t *outFullState, IOT_DevIdentityInfo_t *outIdentity)
{
    *outNumApplied = 0;

    ElemSetDevInfo_t devInfo;
    iot_err_t err = elemSetIdentifyDevice(eid, rootEid, setData, &devInfo);
    if (err != IOT_OK)
        return err;

    iot_err_t applyErr = elemSetApply(eid, elems, numElems, setData, apply, outApplied, outNumApplied);
    if (*outNumApplied == 0)
    {
        /* Nothing moved, so nothing is committed. */
        return applyErr != IOT_OK ? applyErr : IOT_ERR_NOT_SUPPORTED;
    }

    const dev_set_info_t *devSetInfo = &setData->devSetInfo;
    IOT_ContractCommit_t commit = {
        .eid = eid,
        .elemIds = outApplied,
        .numElems = *outNumApplied,
        .attrId = devSetInfo->attrId,
        .value = devSetInfo->attrValue,
        .valueLen = devSetInfo->attrSize,
        .senderFrom = setData->ctx.senderFrom,
    };
    err = IOT_ContractCommitStore(&commit);
    if (err != IOT_OK)
        return err;

    err = IOT_DaoDeviceGetDeviceElemsState(eid, outFullState);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get full device state for EID %04x", eid);
        return err;
    }
    err = IOT_DaoDeviceGetIdentityInfo(eid, outIdentity);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get device identity info for EID %04x", eid);
        return err;
    }

    /* A sub-device reports the new state back once its radio has carried it out. Remember the
     * change so that report confirms it instead of being recorded and reported a second time --
     * only now that this command will report it; otherwise the sub-device's report is the one
     * that tells the cloud. */
    if (rootKnown && eid != rootEid)
        IOT_ContractEchoNote(eid, outApplied, *outNumApplied, devSetInfo->attrId, devSetInfo->attrValue,
                             devSetInfo->attrSize, setData->ctx.senderFrom);

    return IOT_OK;
}

iot_err_t IOT_ContractStateApplySet(const uint16_t *eids, uint16_t numDevices, IOT_EventStateSetRequest_t *setData,
                                    IOT_GroupMemberList_t *groupMembers, IOT_ContractApplyFn apply)
{
    if (setData == NULL || apply == NULL)
        return IOT_ERR_INVALID_ARG;

    iot_err_t iot_err = IOT_OK;
    dev_set_info_t *devSetInfo = &setData->devSetInfo;

    /* The sync-on-off carrier rule runs first, before anything is applied or reported. A refusal
     * is normal, not an error. */
    if (!IOT_ContractAcceptStateSet(setData, eids, numDevices))
        return IOT_OK;

    /* Group and location commands carry no element list: each device's elements are resolved. */
    bool needsPerDeviceElems =
        (setData->targetType == IOT_SET_TARGET_GROUP || setData->targetType == IOT_SET_TARGET_LOCATION);

    uint16_t deviceCount = numDevices;
    uint8_t validDeviceCount = 0;
    IOT_CoreStateChangeDeviceData_t *deviceData = NULL;
    IOT_DaoDeviceElemsAttrsInfo_t *fullStates = NULL;
    IOT_DevIdentityInfo_t *identities = NULL;
    elem_set_t **perDevResolvedElems = NULL;
    uint16_t **perDevChangedElemIds = NULL;
    uint8_t *perDevNumElems = NULL;
    bool *deviceProcessed = NULL;

    uint16_t rootEid = 0;
    bool rootKnown = (IOT_CoreGetRootEid(&rootEid) == IOT_OK);
    if (!rootKnown)
        IOT_LOGW(TAG, "Root EID unknown: no sub-device echo is noted for this command");

    /* Every array is zeroed the moment it exists (calloc), so the cleanup below is safe after a
     * failure at any of these allocations: a pointer slot is either NULL or a real block. */
    deviceData = (IOT_CoreStateChangeDeviceData_t *) Mem_SafeCalloc(
        deviceCount, sizeof(IOT_CoreStateChangeDeviceData_t), TAG, "deviceData");
    fullStates = (IOT_DaoDeviceElemsAttrsInfo_t *) Mem_SafeCalloc(deviceCount, sizeof(IOT_DaoDeviceElemsAttrsInfo_t),
                                                                  TAG, "fullStates");
    identities = (IOT_DevIdentityInfo_t *) Mem_SafeCalloc(deviceCount, sizeof(IOT_DevIdentityInfo_t), TAG,
                                                          "identities");
    if (deviceData == NULL || fullStates == NULL || identities == NULL)
    {
        IOT_LOGE(TAG, "Failed to allocate device data arrays");
        iot_err = IOT_ERR_NO_MEM;
        goto exec_cleanup;
    }

    deviceProcessed = (bool *) Mem_SafeCalloc(deviceCount, sizeof(bool), TAG, "deviceProcessed");
    if (deviceProcessed == NULL)
    {
        iot_err = IOT_ERR_NO_MEM;
        goto exec_cleanup;
    }

    perDevResolvedElems = (elem_set_t **) Mem_SafeCalloc(deviceCount, sizeof(elem_set_t *), TAG, "perDevElems");
    perDevChangedElemIds = (uint16_t **) Mem_SafeCalloc(deviceCount, sizeof(uint16_t *), TAG, "perDevChangedIds");
    perDevNumElems = (uint8_t *) Mem_SafeCalloc(deviceCount, sizeof(uint8_t), TAG, "perDevNumElems");
    if (perDevResolvedElems == NULL || perDevChangedElemIds == NULL || perDevNumElems == NULL)
    {
        iot_err = IOT_ERR_NO_MEM;
        goto exec_cleanup;
    }

    bool pushNotify = IOT_AttrIsNotifyWorthy(devSetInfo->attrId, devSetInfo->attrValue, devSetInfo->attrSize);

    // ---- Apply and commit each device ----

    for (uint16_t d = 0; d < deviceCount; d++)
    {
        uint16_t eid = eids[d];
        const elem_set_t *elems = devSetInfo->elemList;
        uint8_t numElems = devSetInfo->numOfElems;

        if (needsPerDeviceElems)
        {
            elem_set_t *devElems = NULL;
            uint8_t devNumElems = 0;

            /* A group carries per-member element bindings; a location has none and falls back
             * to every element of the device. */
            const uint8_t *memberElemData = NULL;
            uint16_t memberElemDataLen = 0;
            if (setData->targetType == IOT_SET_TARGET_GROUP)
            {
                IOT_GroupMember_t *member = &groupMembers->members[d];
                memberElemData = member->elemData;
                memberElemDataLen = member->elemDataLen;
            }

            iot_err_t resolveErr =
                resolveGroupMemberElements(eid, memberElemData, memberElemDataLen, &devElems, &devNumElems);
            if (resolveErr != IOT_OK || devNumElems == 0)
            {
                IOT_LOGW(TAG, "Skipping device EID %04x — no elements resolved", eid);
                SAFE_FREE(devElems);
                continue;
            }
            perDevResolvedElems[d] = devElems;
            elems = devElems;
            numElems = devNumElems;
            IOT_LOGI(TAG, "Device EID %04x: resolved %d elements", eid, devNumElems);
        }

        /* The cloud ids of the elements this device committed: at most all of them. */
        perDevChangedElemIds[d] =
            (uint16_t *) Mem_SafeMalloc(sizeof(uint16_t) * (numElems > 0 ? numElems : 1), TAG, "perDevChangedIds");
        if (perDevChangedElemIds[d] == NULL)
        {
            IOT_LOGE(TAG, "Skipping device EID %04x — out of memory", eid);
            continue;
        }

        iot_err_t devErr = handleSingleDeviceSet(eid, rootEid, rootKnown, setData, elems, numElems, apply,
                                                 perDevChangedElemIds[d], &perDevNumElems[d],
                                                 &fullStates[validDeviceCount], &identities[validDeviceCount]);
        if (devErr != IOT_OK)
        {
            /* NOT_FOUND means the EID belongs to another board (a multi-device control reaches
             * every device of the location): routine, not a failure. */
            if (devErr == IOT_ERR_NOT_FOUND)
                IOT_LOGD(TAG, "Skipping EID %04x — not on this device", eid);
            else
                IOT_LOGE(TAG, "Failed processing device EID %04x, continuing", eid);
            IOT_DaoDeviceFreeElemsInfo(&fullStates[validDeviceCount]);
            IOT_DaoDeviceFreeIdentityInfo(&identities[validDeviceCount]);
            memset(&fullStates[validDeviceCount], 0, sizeof(IOT_DaoDeviceElemsAttrsInfo_t));
            memset(&identities[validDeviceCount], 0, sizeof(IOT_DevIdentityInfo_t));
            continue;
        }

        deviceData[validDeviceCount] = (IOT_CoreStateChangeDeviceData_t) {
            .eid = eid,
            .mac = identities[validDeviceCount].mac,
            .macLen = identities[validDeviceCount].macSize,
            .devId = identities[validDeviceCount].devId,
            .devIdLen = IOT_DAO_DEVICE_ID_LEN,
            .fullState = (devStateElemsInfo_t *) &fullStates[validDeviceCount],
            .numChangedElems = perDevNumElems[d],
            .changedElemIds = perDevChangedElemIds[d],
            .changedAttrId = devSetInfo->attrId,
            .pushNotify = pushNotify,
        };
        deviceProcessed[d] = true;
        validDeviceCount++;
    }

    // ---- Report: ONE for the whole command ----

    /* Not IOT_ContractReportState(): that reports one device, and N single-device reports would
     * change what the cloud receives for a multi-device command. Adding a field to
     * IOT_CoreStateChangeDeviceData_t means changing both construction sites. */
    if (validDeviceCount > 0)
    {
        IOT_CoreStateChangeReport_t report = {
            .devices = deviceData,
            .numDevices = validDeviceCount,
            .ctx = setData->ctx,
        };
        iot_err = IOT_CoreRequestReportStateChanged(&report);
        if (iot_err != IOT_OK)
            IOT_LOGE(TAG, "Failed to send state report: %d", iot_err);
    }

    // ---- Smart, per committed device, with the request's sender ----

    for (uint16_t d = 0; d < deviceCount; d++)
    {
        if (!deviceProcessed[d])
            continue;
        IOT_ContractCommitAnnounce(&(IOT_ContractCommit_t) {
            .eid = eids[d],
            .elemIds = perDevChangedElemIds[d],
            .numElems = perDevNumElems[d],
            .attrId = devSetInfo->attrId,
            .value = devSetInfo->attrValue,
            .valueLen = devSetInfo->attrSize,
            .senderFrom = setData->ctx.senderFrom,
        }, IOT_CONTRACT_CAUSE_STATE_COMMAND);
    }

    // ---- Cleanup ----

exec_cleanup:
    for (uint8_t d = 0; d < validDeviceCount; d++)
    {
        IOT_DaoDeviceFreeElemsInfo(&fullStates[d]);
        IOT_DaoDeviceFreeIdentityInfo(&identities[d]);
    }
    /* Each array guarded on its own: after a partial allocation failure one can be a valid
     * (zeroed) array while another is NULL. */
    for (uint16_t d = 0; d < deviceCount; d++)
    {
        if (perDevResolvedElems)
            SAFE_FREE(perDevResolvedElems[d]);
        if (perDevChangedElemIds)
            SAFE_FREE(perDevChangedElemIds[d]);
    }
    SAFE_FREE(perDevResolvedElems);
    SAFE_FREE(perDevChangedElemIds);
    SAFE_FREE(perDevNumElems);
    SAFE_FREE(deviceProcessed);
    SAFE_FREE(deviceData);
    SAFE_FREE(fullStates);
    SAFE_FREE(identities);
    return iot_err;
}

iot_err_t IOT_ContractStateHandleSet(IOT_EventStateSetRequest_t *setData, IOT_ContractApplyFn apply)
{
    if (setData == NULL || apply == NULL)
        return IOT_ERR_INVALID_ARG;

    if (setData->targetType == IOT_SET_TARGET_DEVICE)
        IOT_LOGD(TAG, "SET target=DEVICE eid=%04x", setData->target.device.eid);
    else if (setData->targetType == IOT_SET_TARGET_GROUP)
        IOT_LOGD(TAG, "SET target=GROUP group=%04x", setData->target.group.groupEid);
    else if (setData->targetType == IOT_SET_TARGET_LOCATION)
        IOT_LOGD(TAG, "SET target=LOCATION (all devices)");
    else
        IOT_LOGD(TAG, "SET target=VOICE");

    IOT_ContractStateTargets_t targets = {0};
    iot_err_t err = IOT_ContractStateResolveTargets(setData, &targets);
    if (err == IOT_OK && targets.count > 0)
        err = IOT_ContractStateApplySet(targets.eids, targets.count, setData, &targets.members, apply);
    IOT_ContractStateFreeTargets(&targets);
    return err;
}

#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "IOT_Log.h"
#include "IOT_Record.h"
#include "IOT_ContractEvents.h"
#include "IOT_ContractSenderCheck.h"
#include "IOT_ContractAttrsInternal.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "services/IOT_ServiceSmart.h"
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

/* A Kconfig bool that is off is ABSENT from sdkconfig.h, so on ESP-IDF a missing symbol means
 * Record was turned off. Off ESP-IDF (the Linux targets, host tests) there is no Kconfig and
 * Record is always built. */
#ifndef CONFIG_IOT_RECORD_ENABLED
#ifdef ESP_PLATFORM
#define CONFIG_IOT_RECORD_ENABLED 0
#else
#define CONFIG_IOT_RECORD_ENABLED 1
#endif
#endif

/* The Smart feed takes elem_set_t, whose only member is the element id. Passing the caller's
 * uint16_t list as that type avoids a copy (and an allocation that could fail) on every state
 * change; the evaluator deep-copies the list before returning. If elem_set_t ever gains a
 * member, this stops compiling instead of reading the wrong bytes. */
_Static_assert(sizeof(elem_set_t) == sizeof(uint16_t) && offsetof(elem_set_t, elemId) == 0,
               "elem_set_t must be exactly one uint16_t element id");

static const char *TAG = "CONTRACT_STATE";

bool IOT_ContractAcceptStateSet(IOT_EventStateSetRequest_t *set, const uint16_t *eids, uint16_t numDevices)
{
    if (set == NULL)
        return false;

    dev_set_info_t *devSetInfo = &set->devSetInfo;

    /* The sync-on-off carrier attributes are not stored attributes -- they mean "apply this
     * ON/OFF value, but do NOT evaluate your own triggers for it". Resolved here, before
     * anything is applied or reported.
     *
     * The suppression is what makes the stair switch (subSmartType 64) safe: both partners
     * hold a COND_ANY trigger on their own element, so a mirrored write treated as an
     * ordinary control would re-fire the partner's trigger, which would mirror back, forever.
     * senderFrom is the only lever the trigger gate reads, so it is set to AUTOMATION
     * (IOT_SENDER_AUTOMATION is above IOT_SENDER_SCENARIO_UUID_V4, the gate's threshold).
     * That also reports the change's source as AUTOMATION rather than DEVICE, which is
     * honest: an automation on the partner device really did cause it. */
    if (!IOT_AttrResolveSyncOnOffCarrier(&devSetInfo->attrId))
        return true;

    /* Authorise before honouring it. A carrier skips trigger evaluation, so accepting one
     * from any sender would let any device on the location move any of our elements more
     * quietly than an ordinary control could. Every named element must belong to a mirror we
     * actually hold; an empty element list cannot be checked, so it is refused rather than
     * assumed. */
    bool authorised = (devSetInfo->numOfElems > 0 && devSetInfo->elemList != NULL && eids != NULL &&
                       numDevices > 0);
    bool refusedAtElement = false;
    uint16_t refusedEid = 0;
    uint16_t refusedElem = 0;
    for (uint16_t d = 0; d < numDevices && authorised; d++)
    {
        for (int e = 0; e < devSetInfo->numOfElems; e++)
        {
            if (!SmartService_ElementAcceptsMirror(eids[d], devSetInfo->elemList[e].elemId))
            {
                authorised = false;
                refusedAtElement = true;
                refusedEid = eids[d];
                refusedElem = devSetInfo->elemList[e].elemId;
                break;
            }
        }
    }

    if (!authorised)
    {
        /* Not an error. A carrier reaching a device that is not the mirror partner is normal --
         * these frames are addressed by element, and a device holding no pairing for it is the
         * common case. The caller returns IOT_OK: treating a refusal as a failure would log a
         * handler error on every one, and would let anyone spamming the carrier fill the log.
         * This warning is the record. */
        IOT_LOGW(TAG, "Sync-on-off carrier refused: no mirror pairing for the named "
                      "element(s) -- ignoring");
        /* A command that names no element is malformed rather than refused at an element: the
         * log line is its only record. */
        if (refusedAtElement)
            IOT_ContractEventMirrorRefused(refusedEid, refusedElem);
        return false;
    }

    IOT_LOGI(TAG, "Sync-on-off carrier -> ONOFF; trigger evaluation suppressed");
    set->ctx.senderFrom = IOT_SENDER_AUTOMATION;
    return true;
}

static bool changeIsValid(const IOT_ContractStateChange_t *change)
{
    if (change == NULL || change->elemIds == NULL || change->numElems == 0)
        return false;
    if (change->value == NULL && change->valueLen > 0)
        return false;
    return true;
}

iot_err_t IOT_ContractRecordStateChange(const IOT_ContractStateChange_t *change)
{
    if (!changeIsValid(change))
        return IOT_ERR_INVALID_ARG;

#if !CONFIG_IOT_RECORD_ENABLED
    /* Record is compiled out: its append is a no-op, so skip the lookups that only feed it. */
    return IOT_OK;
#else
    /* Record rules match on deviceType and elemType. A failed lookup leaves the field 0,
     * whatever the lookup wrote, so an exact element rule can still match. */
    uint16_t deviceType = 0;
    IOT_DevCommonInfo_t commonInfo = {0};
    if (IOT_DaoDeviceGetCommonInfo(change->eid, &commonInfo) == IOT_OK)
        deviceType = commonInfo.deviceType;

    for (uint16_t i = 0; i < change->numElems; i++)
    {
        uint16_t elemType = 0;
        if (IOT_DaoDeviceGetElementType(change->eid, change->elemIds[i], &elemType) != IOT_OK)
            elemType = 0;

        IOT_RecordAppend(&(IOT_ContractRecordEvent_t) {
            .eid = change->eid,
            .deviceType = deviceType,
            .elemId = change->elemIds[i],
            .elemType = elemType,
            .attrId = change->attrId,
            .value = change->value,
            .valueLen = change->valueLen,
        });
    }
    return IOT_OK;
#endif
}

iot_err_t IOT_ContractEvaluateSmart(const IOT_ContractStateChange_t *change)
{
    if (!changeIsValid(change))
        return IOT_ERR_INVALID_ARG;

    /* An automation's own output that arrives with a manual sender is a partner passing the wrong
     * sender: it would be fed back below. Warn only, behaviour is unchanged. */
    for (uint16_t i = 0; i < change->numElems; i++)
        (void) IOT_ContractSenderCheckIsSuspect(change->eid, change->elemIds[i], change->attrId, change->senderFrom);

    /* Loop guard: scenario, automation, schedule, countdown and voice senders are Smart's own
     * output. Feeding their changes back would re-fire triggers and chain automations. */
    if (change->senderFrom >= IOT_SENDER_SCENARIO_UUID_V4)
        return IOT_OK;

    /* The evaluator deep-copies both lists before returning and never writes through them;
     * the casts only satisfy its non-const signature. */
    dev_set_info_t setInfo = {
        .numOfElems = change->numElems,
        .eid = change->eid,
        .elemList = (elem_set_t *) (uintptr_t) change->elemIds,
        .attrId = change->attrId,
        .attrSize = change->valueLen,
        .attrValue = (uint8_t *) (uintptr_t) change->value,
    };
    return SmartService_TriggerEvaluateDeferred(change->eid, &setInfo);
}

iot_err_t IOT_ContractStateChanged(const IOT_ContractStateChange_t *change)
{
    if (!changeIsValid(change))
        return IOT_ERR_INVALID_ARG;

    iot_err_t recordErr = IOT_ContractRecordStateChange(change);
    iot_err_t smartErr = IOT_ContractEvaluateSmart(change);
    return (recordErr != IOT_OK) ? recordErr : smartErr;
}

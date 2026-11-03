/*
 * IOT_ContractSubDeviceReport: a sub-device's state as the hub's radio received it. Either the echo
 * of a change the contract applied (store only) or the sub-device's own change (store, Record,
 * report, Smart with the sender "device").
 */
#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"
#include "dao/IOT_DaoStoresInternal.h"

#include "IOT_ContractCommit.h"
#include "IOT_ContractEcho.h"
#include "IOT_Log.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "defines/IOT_AttrDefines.h" /* IOT_AttrGetSize: the product's attribute table */

static const char *TAG = "ContractSubDev";

/* The commit's store-and-record step (IOT_ContractCommitStore()) for a reported state: the store
 * write adds the attribute to the element when it does not hold it yet (a sensor's first reading).
 * The element itself must already be known. Record runs only when the store write succeeded. */
static iot_err_t commitReportedState(const IOT_ContractCommit_t *c)
{
    iot_err_t err = IOT_DaoDeviceUpsertAttribute(c->eid, c->elemIds[0], c->attrId, c->value, c->valueLen);
    if (err != IOT_OK)
    {
        IOT_LOGW(TAG, "EID %04x elem %04x attr %04x: state not stored (%d), not recorded or reported", c->eid,
                 c->elemIds[0], c->attrId, (int) err);
        return err;
    }

    const IOT_ContractStateChange_t change = {
        .eid = c->eid,
        .elemIds = c->elemIds,
        .numElems = 1,
        .attrId = c->attrId,
        .value = c->value,
        .valueLen = c->valueLen,
        .senderFrom = c->senderFrom,
    };
    (void) IOT_ContractRecordStateChange(&change);
    return IOT_OK;
}

iot_err_t IOT_ContractSubDeviceReport(uint16_t eid, uint16_t elemId, uint16_t attrId, const uint8_t *value,
                                      uint16_t len, bool pushNotify)
{
    if (value == NULL || len == 0)
        return IOT_ERR_INVALID_ARG;

    /* A report must carry the attribute's declared size. The store keeps whatever length it is
     * given and every later read answers with that length, so one short report would permanently
     * shrink the stored attribute. A wrong size is a decoder bug; padding it would hide the bug
     * while still storing fields in the wrong places. */
    const uint16_t declared = IOT_AttrGetSize(attrId);
    if (declared != 0 && len != declared)
    {
        IOT_LOGW(TAG, "Refusing attr %04x report for EID %04x: %u bytes, expected %u", attrId, eid, (unsigned) len,
                 (unsigned) declared);
        return IOT_ERR_INVALID_SIZE;
    }

    /* The echo of a change the contract applied: it was already recorded, reported and given to
     * Smart with its request's sender. Only the store is brought up to date. */
    if (IOT_ContractEchoConsume(eid, elemId, attrId, value, len, NULL))
    {
        iot_err_t err = IOT_DaoDeviceUpsertAttribute(eid, elemId, attrId, value, len);
        if (err != IOT_OK)
            IOT_LOGW(TAG, "EID %04x elem %04x attr %04x: confirmed state not stored (%d)", eid, elemId, attrId,
                     (int) err);
        return err;
    }

    IOT_ContractCommit_t commit = {
        .eid = eid,
        .elemIds = &elemId,
        .numElems = 1,
        .attrId = attrId,
        .value = value,
        .valueLen = len,
        .senderFrom = IOT_SENDER_DEVICE, /* the sub-device changed itself: below Smart's loop guard */
    };
    iot_err_t err = commitReportedState(&commit);
    if (err != IOT_OK)
        return err;

    IOT_ContractStateReport_t report = {
        .eid = eid,
        .changedElemIds = &elemId,
        .numChangedElems = 1,
        .changedAttrId = attrId,
        .pushNotify = pushNotify,
        .ctx = {0}, /* the sub-device's own change answers no request */
    };
    err = IOT_ContractReportState(&report);
    if (err != IOT_OK)
        IOT_LOGE(TAG, "EID %04x: state report failed (%d)", eid, (int) err);

    /* Skip Smart only when the report could not read the device (DATA_NOT_FOUND). A failed send
     * is not a reason: local control must keep working while the hub is offline. */
    if (err != IOT_ERR_DATA_NOT_FOUND)
        IOT_ContractCommitAnnounce(&commit, IOT_CONTRACT_CAUSE_SUBDEVICE_REPORT);
    return err;
}

/*
 * IOT_ContractLocalChange: a change the product's own hardware made (a button press, a sensor
 * edge). Same order as every other path: store, Record, report, Smart.
 */
#include "IOT_Contract.h"
#include "IOT_ContractInternal.h"

#include "IOT_ContractCommit.h"
#include "IOT_ContractElements.h"
#include "IOT_Core.h"
#include "IOT_Log.h"

static const char *TAG = "ContractLocal";

iot_err_t IOT_ContractLocalChange(uint16_t eid, uint16_t elemId, uint16_t attrId, const uint8_t *value, uint16_t len)
{
    if (value == NULL || len == 0)
    {
        IOT_LOGE(TAG, "Invalid local change");
        return IOT_ERR_INVALID_ARG;
    }

    /* Everything past here speaks the cloud's element numbering. A device element with no cloud
     * counterpart keeps its own id (and the translation logs why). */
    uint16_t cloudElemId = elemId;
    (void) IOT_ContractElemDeviceToCloud(eid, elemId, &cloudElemId);

    IOT_LOGI(TAG, "Local change: EID %04x elem %04x (cloud %04x) attr %04x len %d", eid, elemId, cloudElemId, attrId,
             (int) len);

    IOT_ContractCommit_t commit = {
        .eid = eid,
        .elemIds = &cloudElemId,
        .numElems = 1,
        .attrId = attrId,
        .value = value,
        .valueLen = len,
        .senderFrom = IOT_SENDER_DEVICE,
    };
    iot_err_t err = IOT_ContractCommitStore(&commit);
    if (err != IOT_OK)
        return err;

    IOT_ContractStateReport_t report = {
        .eid = eid,
        .changedElemIds = &cloudElemId,
        .numChangedElems = 1,
        .changedAttrId = attrId,
        .pushNotify = false,
        .ctx = {0}, /* a local change answers no request */
    };
    err = IOT_ContractReportState(&report);
    if (err != IOT_OK)
        IOT_LOGE(TAG, "EID %04x: state report failed (%d)", eid, (int) err);

    /* Skip Smart only when the report could not read the device (DATA_NOT_FOUND). A failed send
     * is not a reason: local control must keep working while the device is offline. */
    if (err != IOT_ERR_DATA_NOT_FOUND)
        IOT_ContractCommitAnnounce(&commit, IOT_CONTRACT_CAUSE_LOCAL_CHANGE);
    return err;
}

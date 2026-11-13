/*
 * IOT_ContractReport / IOT_ContractReportFields / IOT_ContractApplyField: report and read an
 * attribute value as numeric fields, so a product does not build the wire layout itself.
 */
#include <stdbool.h>

#include "IOT_Contract.h"
#include "IOT_ContractPending.h"
#include "IOT_Core.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "IOT_ContractAttrsInternal.h"

/* The root has an EID from the first join step but its elements only from the next one; a value
 * reported in between has nowhere to be stored yet. Only "not found" means that: any other read
 * error is left to the local change, which reports it. */
static bool rootStateStored(uint16_t rootEid)
{
    IOT_DaoDeviceElemsAttrsInfo_t elems = {0};
    iot_err_t err = IOT_DaoDeviceGetDeviceElemsState(rootEid, &elems);
    IOT_DaoDeviceFreeElemsInfo(&elems);
    return err != IOT_ERR_DATA_NOT_FOUND;
}

iot_err_t IOT_ContractReportFields(uint16_t elemId, uint16_t attrId, const uint16_t *fields, uint8_t count)
{
    if (fields == NULL || count == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }
    if (count > IOT_CONTRACT_REPORT_FIELDS_MAX || (uint16_t) (count * 2) != IOT_AttrGetSize(attrId))
    {
        return IOT_ERR_INVALID_SIZE;
    }

    uint8_t bytes[IOT_CONTRACT_REPORT_FIELDS_MAX * 2];
    for (uint8_t i = 0; i < count; i++)
    {
        bytes[2 * i] = (uint8_t) (fields[i] >> 8);
        bytes[2 * i + 1] = (uint8_t) (fields[i] & 0xFF);
    }

    uint16_t len = (uint16_t) (count * 2);
    uint16_t rootEid = 0;
    bool haveRoot = (IOT_CoreGetRootEid(&rootEid) == IOT_OK);
    if (!haveRoot || (!IOT_ContractPendingFlushed() && !rootStateStored(rootEid)))
    {
        iot_err_t held = IOT_ContractPendingHold(elemId, attrId, bytes, len);
        if (held != IOT_ERR_INVALID_STATE)
        {
            return held;
        }
        /* The table was flushed after the checks above: the device is provisioned now. */
        if (!haveRoot)
        {
            iot_err_t err = IOT_CoreGetRootEid(&rootEid);
            if (err != IOT_OK)
            {
                return err;
            }
        }
    }
    /* A value held earlier must not be stored over this newer one when the table flushes. */
    IOT_ContractPendingDiscard(elemId, attrId);
    return IOT_ContractLocalChange(rootEid, elemId, attrId, bytes, len);
}

iot_err_t IOT_ContractReport(uint16_t elemId, uint16_t attrId, uint16_t value)
{
    return IOT_ContractReportFields(elemId, attrId, &value, 1);
}

iot_err_t IOT_ContractApplyField(const IOT_ContractApply_t *req, uint8_t index, uint16_t *out)
{
    if (req == NULL || out == NULL || req->value == NULL || (uint32_t) (index + 1) * 2 > req->valueLen)
    {
        return IOT_ERR_INVALID_ARG;
    }
    *out = (uint16_t) ((req->value[2 * index] << 8) | req->value[2 * index + 1]);
    return IOT_OK;
}

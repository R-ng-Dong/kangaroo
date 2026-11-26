/*
 * Unknown inside, default on the wire. See services/IOT_ContractStateUnknown.h.
 */
#include "services/IOT_ContractStateUnknown.h"

#include "IOT_ContractAttrsInternal.h"
#include "IOT_Log.h"
#include "IOT_Memory.h"

static const char *TAG = "ContractUnknown";

iot_err_t IOT_ContractStateFillUnknown(IOT_DaoDeviceElemsAttrsInfo_t *state)
{
    if (state == NULL)
        return IOT_ERR_INVALID_ARG;

    for (uint16_t e = 0; e < state->numOfElem; e++)
    {
        IOT_DaoDeviceStateElemState_t *el = &state->elemList[e];
        for (uint8_t a = 0; a < el->numOfAttr; a++)
        {
            IOT_DaoDeviceStateAttr_t *at = &el->attrList[a];
            if (at->attrSize != 0 || IOT_AttrIsPersisted(at->attrId))
                continue;
            uint16_t size = IOT_AttrGetSize(at->attrId);
            uint8_t *value = (uint8_t *) Mem_SafeMalloc(size, TAG, "unknown default");
            if (value == NULL)
                return IOT_ERR_NO_MEM;
            (void) IOT_AttrFillDefault(at->attrId, value);
            SAFE_FREE(at->attrValue);
            at->attrValue = value;
            at->attrSize = size;
        }
    }
    return IOT_OK;
}

#include "record.h"

#include <string.h>
#include <time.h>

#include "IOT_AppManager.h"
#include "IOT_Core.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_CoreOta.h"
#include "IOT_CoreState.h"
#include "IOT_ErrorManager.h"
#include "IOT_Log.h"
#include "IOT_Osal.h"
#include "IOT_Platform.h"
#include "IOT_TimerMgmt.h"

#include "IOT_Contract.h"
#include "IOT_ContractRecord.h"
#include "dao/IOT_DaoDeviceMgmt.h"

#include "defines/IOT_AttrDefines.h"

#include "device_element_manager.h"

static const char *TAG = "RECORD";

#define MODEL_ID  "00000101C0040003"

static const UI16 APP_ELEMENTS[] =  {ELEMENT_ID_1, ELEMENT_ID_2, ELEMENT_ID_3, ELEMENT_ID_4, ELEMENT_ID_5};
#define APP_ELEMENT_COUNT  (sizeof(APP_ELEMENTS) / sizeof(APP_ELEMENTS[0]))

iot_err_t AppRecordConfigure(void)
{
    /* Rules match the semantic element type. Both attributes are reported by switch
     * elements; the attrId distinguishes water consumption from energy. */
    const IOT_ContractRecordRule_t rules[] = {
        {.elemType = IOT_DEVTYPE_WATER_PURIFIER, .attrId = ATTR_INFOMATION_WATER_CONSUMER},
        {.elemType = IOT_DEVTYPE_WATER_PURIFIER, .attrId = ATTR_ENERGY},
    };
    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++)
    {
        const iot_err_t err = IOT_ContractRecordRegisterRule(&rules[i]);
        if (err != IOT_OK)
        {
            IOT_LOGE(TAG, "Record rule for element type %u attr %u failed (%s)",
                     rules[i].elemType, rules[i].attrId, iot_err_to_name(err));
            return err;
        }
    }
    return IOT_OK;
}

static iot_err_t app_apply(const IOT_ContractApply_t *req)
{
    (void) req;
    return IOT_OK;
}

static void app_on_core_event(const IOT_CoreEvent_t *event)
{
    (void) event;
}

static void app_on_device_removed(uint16_t eid, bool root)
{
    (void) eid;
    if(root)
    {
        IOT_ApplicationRestartChip();
    }
}

static int64_t s_lastActivation = 0;

static void app_on_contract_event(const IOT_ContractEvent_t *event, void *arg)
{
    (void) arg;
    if (event->type == IOT_CONTRACT_EVT_STATE_COMMITTED &&
        event->data.stateCommitted.cause == IOT_CONTRACT_CAUSE_STATE_COMMAND)
    {
        s_lastActivation = (int64_t) time(NULL);
    }
}

_bool AppRecordInit(void)
{
    iot_err_t err =  AppRecordConfigure();
    // (void) IOT_ContractSubscribe(app_on_contract_event, NULL);

    const IOT_ContractConfig_t config = {
        .modelId = MODEL_ID,
        .elementIds = APP_ELEMENTS,
        .elementCount = APP_ELEMENT_COUNT,
    };
    const IOT_ContractCallbacks_t callbacks = {
        .apply = app_apply,
        // .onCoreEvent = app_on_core_event,
        .onDeviceRemoved = app_on_device_removed,
    };
    err = IOT_ContractStart(&config, &callbacks);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "IOT_ContractStart failed: %s", iot_err_to_name(err));
        if (IOT_CoreOtaIsPending() && IOT_CoreOtaDiscardPending() == IOT_OK)
        {
            IOT_ApplicationRestartChip();
        }
        return D_FALSE;
    }
    return D_TRUE;
}

static iot_err_t AppUpdateAttributeU32(uint16_t elemId, uint16_t attrId, uint32_t value)
{
    uint16_t rootEid = 0;
    iot_err_t err = IOT_CoreGetRootEid(&rootEid);
    if (err != IOT_OK)
    {
        return err;
    }

    const uint8_t bytes[4] = {
        (uint8_t) (value >> 24),
        (uint8_t) (value >> 16),
        (uint8_t) (value >> 8),
        (uint8_t) value,
    };
    return IOT_ContractLocalChange(rootEid, elemId, attrId, bytes, sizeof(bytes));
}

/* value is the current cumulative water consumption, encoded as an unsigned 32-bit value. */
_bool app_update_water_consumption(uint32_t value)
{
    return AppUpdateAttributeU32(ELEMENT_ID_1, IOT_ATTR_APP_WATER_CONSUMPTION, value) == IOT_OK ? D_TRUE : D_FALSE;
}

_bool app_update_energy_consumption(uint32_t value)
{
    return AppUpdateAttributeU32(ELEMENT_ID_2, IOT_ATTR_APP_ENERGY, value) == IOT_OK ? D_TRUE : D_FALSE;
}

_bool app_update_energy_saving(uint32_t value)
{
    return AppUpdateAttributeU32(ELEMENT_ID_3, IOT_ATTR_APP_ENERGY, value) == IOT_OK ? D_TRUE : D_FALSE;
}

//IOT_RecordPushKick


#include "iot_smart.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "rtc_time.h"
#include "water_controller.h"
#include <string.h>

#define IOT_SMART_NVS_NAMESPACE "iot_smart"
#define IOT_SMART_NVS_KEY "trigger_bind"

#define MAX_NUM_IOT_SMART 5

typedef struct
{
    UI8 is_used;
    UI16 ro_flushing_time_s;
    struct
    {
        UI16 smid;
        UI8 enable;
    } bindTriggerElmInfo;
    struct
    {
        UI16 timeStart;
        UI16 timeStop;
        UI8 weekDay;
    } bindTriggerMix;
} SmartTriggerBindCmd;

SmartTriggerBindCmd s_smart[MAX_NUM_IOT_SMART];

_bool IotSmartSave(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(IOT_SMART_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return D_FALSE;
    }

    err = nvs_set_blob(handle, IOT_SMART_NVS_KEY, s_smart, sizeof(s_smart));
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return (err == ESP_OK) ? D_TRUE : D_FALSE;
}

void IotSmartInit(void)
{
    memset(s_smart, 0, sizeof(s_smart));

    nvs_handle_t handle;
    if (nvs_open(IOT_SMART_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    {
        return;
    }

    size_t stored_size = sizeof(s_smart);
    esp_err_t err = nvs_get_blob(handle, IOT_SMART_NVS_KEY, s_smart, &stored_size);
    nvs_close(handle);

    if (err != ESP_OK || stored_size != sizeof(s_smart))
    {
        // Keep the zero-initialized defaults if the key is absent or has an old format.
        memset(s_smart, 0, sizeof(s_smart));
        for (UI8 i = 0; i < MAX_NUM_IOT_SMART; i++)
        {
            SmartTriggerBindCmd *sm_temp = &s_smart[i];
            sm_temp->ro_flushing_time_s = 120; // default 2 minutes
        }
    }
}

_bool IOTSmartClear(void)
{
    nvs_handle_t handle;
    if (nvs_open(IOT_SMART_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return D_FALSE;
    esp_err_t err = nvs_erase_key(handle, IOT_SMART_NVS_KEY); 
    if(err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("SMART", "Key %s not found in namespace %s (nothing to erase)", IOT_SMART_NVS_KEY, IOT_SMART_NVS_NAMESPACE);
        nvs_close(handle);
        return D_FALSE;
    }
    if(err != ESP_OK)
    {
        ESP_LOGE("SMART", "Error erasing key %s: %s", IOT_SMART_NVS_KEY, esp_err_to_name(err));
        nvs_close(handle);
        return D_FALSE;  
    }
    err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK ? D_TRUE : D_FALSE;
}

_bool IotSmartBindCommand(IOT_CoreSmartEventBindTriggerData_t *data)
{
    for (UI8 i = 0; i < MAX_NUM_IOT_SMART; i++)
    {
        SmartTriggerBindCmd *sm_temp = &s_smart[i];
        UI16 smid = sm_temp->bindTriggerElmInfo.smid;
        // Override
        if(smid == data->bindTriggerElmInfo.smid && sm_temp->is_used == 1)
        {
            sm_temp->is_used = 1;
            // sm_temp->bindTriggerElmInfo.enable = 1;
            sm_temp->bindTriggerElmInfo.smid = data->bindTriggerElmInfo.smid;
            sm_temp->bindTriggerMix.timeStart = data->timeStart;
            sm_temp->bindTriggerMix.timeStop = data->timeStop;
            sm_temp->bindTriggerMix.weekDay = data->weekDay;
            return IotSmartSave();
        }
    }

    for (UI8 i = 0; i < MAX_NUM_IOT_SMART; i++)
    {
        SmartTriggerBindCmd *sm_temp = &s_smart[i];

        if(!sm_temp->is_used)
        {
            sm_temp->is_used = 1;
            sm_temp->bindTriggerElmInfo.enable = 1;
            sm_temp->bindTriggerElmInfo.smid = data->bindTriggerElmInfo.smid;
            sm_temp->bindTriggerMix.timeStart = data->timeStart;
            sm_temp->bindTriggerMix.timeStop = data->timeStop;
            sm_temp->bindTriggerMix.weekDay = data->weekDay;
            return IotSmartSave();
        }
    }
    return D_FALSE; // full
}

_bool IotSmartUnBindCommand(UI16 smid)
{
    for (UI8 i = 0; i < MAX_NUM_IOT_SMART; i++)
    {
        SmartTriggerBindCmd *sm_temp = &s_smart[i];
        // WARNING (B4): this local 'smid' shadows the parameter, so the check below compares the
        // slot with itself (always true). The FIRST used slot is unbound, not the requested smid.
        // done
        if(smid == sm_temp->bindTriggerElmInfo.smid && sm_temp->is_used == 1)
        {
            sm_temp->is_used = 0;
            sm_temp->bindTriggerElmInfo.enable = 0;
            return IotSmartSave();
        }
    }

    return D_FALSE; // not found smid
}

_bool IotSmartEnableCommand(UI16 smid, UI8 enable)
{
    for (UI8 i = 0; i < MAX_NUM_IOT_SMART; i++)
    {
        SmartTriggerBindCmd *sm_temp = &s_smart[i];
        // WARNING (B4): this local 'smid' shadows the parameter, so the check below compares the
        // slot with itself (always true). The FIRST used slot is enabled/disabled, not the requested smid.
        //done
        if(smid == sm_temp->bindTriggerElmInfo.smid && sm_temp->is_used == 1)
        {
            sm_temp->bindTriggerElmInfo.enable = enable;
            return IotSmartSave();
        }
    }

    return D_FALSE; // not found smid
}

_bool IOTSetRoFlushingTime(UI16 smid, UI16 time_s)
{
    for (UI8 i = 0; i < MAX_NUM_IOT_SMART; i++)
    {
        SmartTriggerBindCmd *sm_temp = &s_smart[i];
        if(smid == sm_temp->bindTriggerElmInfo.smid && sm_temp->is_used == 1)
        {
            sm_temp->ro_flushing_time_s = time_s;
            return IotSmartSave();
        }
    }

    return D_FALSE; // not found smid
}

// WARNING (B5): nothing calls IotSmartEvaluateCondition(), and ro_flushing_time_s is never set
// (IotSmartBindCommand does not fill it), so the scheduled RO flushing never runs. If it is
// called later, note it fires on every call inside [timeStart, timeStop] with a 0 s duration.
// NOTE: This function is already called in AppTask().
// done
void IotSmartEvaluateCondition(void)
{
    INT year, hour, min, wday;
    GetNowTime(&year, &hour, &min, &wday);
    if(year < 126) return;
    if(WaterProfileGetStatus(RO_STATUS) == RO_STATUS_FLUSHING) return;
    INT count_min = hour * 60 + min;
    for(UI8 i = 0; i < MAX_NUM_IOT_SMART; i++)
    {
        SmartTriggerBindCmd *sm_temp = &s_smart[i];
        if(sm_temp->is_used == 1 && sm_temp->bindTriggerElmInfo.enable \
           && count_min >= sm_temp->bindTriggerMix.timeStart && count_min <= sm_temp->bindTriggerMix.timeStop \
           && (sm_temp->bindTriggerMix.weekDay == 0x00 || 1<<wday & sm_temp->bindTriggerMix.weekDay))
        {
            WaterControllerStartROFlushing(sm_temp->ro_flushing_time_s);
            return;
        }
    }
}
#include "app.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

#include "IOT_Core.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_CoreReporter.h"
#include "IOT_DaoDeviceMgmt.h"

#include "tds_task.h"
#include "water_profile.h"
#include "filter_manager.h"
#include "filter_runtime.h"
#include "system_runtime.h"
#include "water_runtime.h"
#include "status_manager.h"
#include "water_controller.h"
#include "pump.h"
#include "warning_manager.h"
#include "led_manager.h"
#include "event_manager.h"
#include "cloud_event_handler.h"
#include "device_element_manager.h"
#include "energy_saving_manager.h"
#include "energy_consumption.h"
#include "rtc_time.h"
#include "storage.h"
#include "group.h"
#include "iot_smart.h"

#include "record.h"
#include "report_alert.h"

static const char *TAG = "APP";

#define SYSTEM_NVS_NAMESPACE "water_machine"
#define SYSTEM_NVS_KEY "boot_count"

static WaterPurifierType s_model = WATER_PURIFIER_TYPE_STANDARD;

void AppPushWarningToCloud(void);
void FilterManagerReportToCloud(const UI8 *percent_values, FilterManagerReportMode mode, FilterTimeIndex filter_index);
void AppResponseSavingEnergyToCloud(UI32 saving_energy);
void AppPushWaterConsumptionToCloud(void);
void AppPushStatusManagerToCloud(UI8 ro_stt, UI8 ply_stt);
void AppPushFirmwareVersion(UI16 version);
void AppInitAttributeValue(void);

static void AppCheckEnergyChanges(void);
static void AppCheckWaterConsumptionChanges(void);
static void AppCheckStatusChanged(void);
static void AppCheckVersionFirmwareMainboard(void);

WaterPurifierType AppGetModel(void)
{
    return s_model;
}

void AppSetModel(WaterPurifierType model)
{
    if(WATER_PURIFIER_TYPE_STANDARD != model && WATER_PURIFIER_TYPE_HOT_COLD != model)
        return;
    s_model = model;
}

static void ResetBootCount(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SYSTEM_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open failed for boot count reset: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_u32(handle, SYSTEM_NVS_KEY, 0U);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_set_u32 failed while resetting boot count: %s", esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_commit failed while resetting boot count: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
}

static void HandleBootCount(void)
{
    uint32_t boot_count = 0U;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SYSTEM_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open failed while reading boot count: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_get_u32(handle, SYSTEM_NVS_KEY, &boot_count);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        boot_count = 0U;
        err = ESP_OK;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_get_u32 failed while reading boot count: %s", esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    boot_count += 1;
    ESP_LOGI(TAG, "Boot count = %lu", (unsigned long)boot_count);

    if (boot_count >= BOOT_COUNT_RESET_THRESHOLD)
    {
        ESP_LOGW(TAG, "Boot count reached %lu, resetting device", (unsigned long)boot_count);
        ResetBootCount();
        nvs_close(handle);
        if(IOT_CoreIsProvisioned())
            RootDeviceFactoryReset();
        return;
    }

    err = nvs_set_u32(handle, SYSTEM_NVS_KEY, boot_count);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_set_u32 failed while updating boot count: %s", esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_commit failed while updating boot count: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
}

static void BootCountResetTimeoutTask(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BOOT_COUNT_RESET_TIMEOUT_MS));
    ESP_LOGI(TAG, "Boot stable for %u seconds; resetting boot count", BOOT_COUNT_RESET_TIMEOUT_MS / 1000);
    ResetBootCount();
    vTaskDelete(NULL);
}

static void AppTask(void *arg)
{
    UI32 now_ms = (UI32)(esp_timer_get_time() / 1000ULL);
    UI32 update_system_time_count = 0;
    UI32 energy_report_count = 0U;
    UI32 water_report_count = 0U;

    // WATER_PURIFIER_TYPE_HOT_COLD
    INT hour = 0; INT minute = 0; INT year = 0;


    while (1)
    {
        ReportAlertPoll();
        if(WATER_PURIFIER_TYPE_HOT_COLD == s_model)
        {
            StatusManagerRefresh();
            StatusManagerCheckWarnings();
            GetNowTime(&year, &hour, &minute, NULL);
            if (year >= 125)
            {
                UI16 minute_of_day = hour * 60 + minute;
                EnergySavingManagerUpdate(minute_of_day,
                                          (UI64)(esp_timer_get_time() / 1000000ULL));
            }
            AppCheckVersionFirmwareMainboard();
        }
        else if(WATER_PURIFIER_TYPE_STANDARD == s_model)
        {
            WarningManagerUpdate(WARNING_ID_ER00055_RO_PUMP_RUNTIME_OVER_60_MIN, PumpGetElapsedTimeSeconds(now_ms) >= RO_PUMP_OVERRUN_TIMEOUT_SECONDS);
        }
        // check status RO, PLY
        AppCheckStatusChanged();
        // check filter
        FilterManagerTick(now_ms, s_model);

        if (++energy_report_count >= TIMEOUT_RESPONSE_ENERGY_TO_CLOUD_COUNT)
        {
            energy_report_count = 0U;
            AppCheckEnergyChanges();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (++water_report_count >= TIMEOUT_RESPONSE_WATER_CONSUMPTION_TO_CLOUD_MS_COUNT)
        {
            water_report_count = 0U;
            AppCheckWaterConsumptionChanges();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        now_ms = (UI32)(esp_timer_get_time() / 1000ULL);

        // update system time every TIMEOUT_UPDATE_SYSTEM_TIME_MS
        if(++update_system_time_count >= TIMEOUT_UPDATE_SYSTEM_TIME_COUNT)
        {
            update_system_time_count = 0;
            SystemRuntimeUpdate(SYS_UPDATE_ALL, now_ms);
        }

        vTaskDelay(pdMS_TO_TICKS(APP_INTERVAL_MS));
    }
}


static void EventHandler(EventID event_id, void *data)
{
    (void)data;
    switch (event_id)
    {
    case EVENT_INLET_WATER_QUALITY_WARNING:
        ESP_LOGW(TAG, "EVENT_INLET_WATER_QUALITY_WARNING");
        break;
    case EVENT_OUTLET_WATER_QUALITY_FAULT:
        ESP_LOGE(TAG, "EVENT_OUTLET_WATER_QUALITY_FAULT");
        break;
    case EVENT_WATER_LEAK:
        ESP_LOGW(TAG, "EVENT_WATER_LEAK");
        break;
    case EVENT_TANK_LOW:
        ESP_LOGW(TAG, "EVENT_TANK_LOW");
        break;
    case EVENT_RO_PUMP_RUNTIME_OVER_60_MIN:
        ESP_LOGW(TAG, "EVENT_RO_PUMP_RUNTIME_OVER_60_MIN");
        break;
    case EVENT_FILTER1_LIFE_70:
        ESP_LOGI(TAG, "EVENT_FILTER1_LIFE_70");
        break;
    case EVENT_FILTER2_LIFE_70:
        ESP_LOGI(TAG, "EVENT_FILTER2_LIFE_70");
        break;
    case EVENT_FILTER3_LIFE_70:
        ESP_LOGI(TAG, "EVENT_FILTER3_LIFE_70");
        break;
    case EVENT_FILTER4_LIFE_70:
        ESP_LOGI(TAG, "EVENT_FILTER4_LIFE_70");
        break;
    case EVENT_FILTER5_LIFE_70:
        ESP_LOGI(TAG, "EVENT_FILTER5_LIFE_70");
        break;
    case EVENT_FILTER1_LIFE_90:
        ESP_LOGI(TAG, "EVENT_FILTER1_LIFE_90");
        break;
    case EVENT_FILTER2_LIFE_90:
        ESP_LOGI(TAG, "EVENT_FILTER2_LIFE_90");
        break;
    case EVENT_FILTER3_LIFE_90:
        ESP_LOGI(TAG, "EVENT_FILTER3_LIFE_90");
        break;
    case EVENT_FILTER4_LIFE_90:
        ESP_LOGI(TAG, "EVENT_FILTER4_LIFE_90");
        break;
    case EVENT_FILTER5_LIFE_90:
        ESP_LOGI(TAG, "EVENT_FILTER5_LIFE_90");
        break;
    case EVENT_OUTPUT_WATER_QUALITY_WARNING:
        ESP_LOGW(TAG, "EVENT_OUTPUT_WATER_QUALITY_WARNING");
        break;
    case EVENT_HEATER_SENSOR_FAULT:
        ESP_LOGE(TAG, "EVENT_HEATER_SENSOR_FAULT");
        break;
    case EVENT_HEATER_RUNTIME_OVER_1H:
        ESP_LOGW(TAG, "EVENT_HEATER_RUNTIME_OVER_1H");
        break;
    case EVENT_COOLER_SENSOR_FAULT:
        ESP_LOGE(TAG, "EVENT_COOLER_SENSOR_FAULT");
        break;
    case EVENT_COOLER_RUNTIME_OVER_2H:
        ESP_LOGW(TAG, "EVENT_COOLER_RUNTIME_OVER_2H");
        break;
    default:
        ESP_LOGW(TAG, "UNKNOWN EVENT: %d", (int)event_id);
        break;
    }
    
    AppPushWarningToCloud(); // post WARNING to Cloud
    UI32 new_mask = WarningManagerGetMask();
    if(new_mask)
    {
        LedManagerSetError();
    }
    else
    {
        LedManagerSetNormal();
    }
}

static void CloudUpdateHandler(UI16 elemId, UI16 attrId, UI8 *attrValue, UI8 lenAttrValue)
{
    const DeviceElementAttributeInfo *attribute = DeviceElementGetAttribute(elemId, attrId);
    if (attribute == NULL)
    {
        ESP_LOGW(TAG, "Unsupported attribute 0x%04x for element 0x%04x", attrId, elemId);
        return;
    }

    if (attrValue == NULL || lenAttrValue != attribute->value_length)
    {
        ESP_LOGE(TAG,
                 "Invalid value length for %s (element 0x%04x, attribute 0x%04x): got %u, expected %u",
                 attribute->name,
                 elemId,
                 attrId,
                 lenAttrValue,
                 attribute->value_length);
        return;
    }
    ESP_LOGI(TAG, "Received %s for element 0x%04x", attribute->name, elemId);
    if (!DeviceElementSetAttributeValue(elemId, attrId, attrValue, lenAttrValue))
    {
        ESP_LOGE(TAG, "Failed to store %s for element 0x%04x", attribute->name, elemId);
        return;
    }
    else{
        if(ATTR_ONOFF == attrId)
        {
            UI8 onoff = attrValue[1];
            switch (elemId)
            {
            case ELEMENT_ID_1:
                // Unuse
                break;
            case ELEMENT_ID_4:
            {
                EnergySavingManagerSetTank(ENERGY_SAVING_TANK_HOT, onoff, 0xFF,
                                           (UI64)(esp_timer_get_time() / 1000000ULL));
                break;
            }
            case ELEMENT_ID_5:
            {
                EnergySavingManagerSetTank(ENERGY_SAVING_TANK_COLD, onoff, 0xFF,
                                           (UI64)(esp_timer_get_time() / 1000000ULL));
                break;
            }
            default:
                break;
            }
        }
        else if(ATTR_TEMP_SET == attrId)
        {
            UI16 temp = attrValue[0] << 8 | attrValue[1];
            switch (elemId)
            {
            case ELEMENT_ID_4:
            {
                EnergySavingManagerSetTank(ENERGY_SAVING_TANK_HOT, 1, (UI8)temp,
                                           (UI64)(esp_timer_get_time() / 1000000ULL));
                break;
            }
            case ELEMENT_ID_5:
            {
                EnergySavingManagerSetTank(ENERGY_SAVING_TANK_COLD, 1, (UI8)temp,
                                           (UI64)(esp_timer_get_time() / 1000000ULL));
                break;
            }
            default:
                break;
            }
        }
        else if(ATTR_FILTER_SETTING == attrId)
        {
            UI16 isNewProductModel = attrValue[0] << 8 | attrValue[1];
            UI32 expire_time_s = (attrValue[2] << 24) | (attrValue[3] << 16) | (attrValue[4] << 8) | attrValue[5]; 
            UI8 Pid[FILTER_PID_SIZE]; // filterID
            memcpy(Pid, attrValue + 6, FILTER_PID_SIZE);
            UI8 filter_idx = ElementIdToIndex(elemId);
            if(!isNewProductModel) 
            {
                UI8 pid_get[FILTER_PID_SIZE];
                if(!FilterManagerGetPid((FilterTimeIndex)filter_idx, pid_get))
                    return;
                _bool is_equal = (memcmp(pid_get, Pid, FILTER_PID_SIZE) == 0) ? D_TRUE : D_FALSE;
                if(!is_equal)
                    return;
                if(!FilterManagerSetLifetimeSeconds((FilterTimeIndex)filter_idx, expire_time_s))
                    return;
            }
            else
            {
                if(!FilterManagerSetLifetimeSeconds((FilterTimeIndex)filter_idx, expire_time_s))
                    return;
                if(!FilterManagerSetPid((FilterTimeIndex)filter_idx, Pid))
                    return;
            }
            SystemRuntimeReconfig(filter_idx, 0, esp_timer_get_time()/1000);
            
            (void)DeviceElementSetAttributeValueOffset(elemId, ATTR_INFOMATION_PRODUCT_USAGE_TIME, 0, Pid, FILTER_PID_SIZE);
        }
        else if(ATTR_ENERGY_SAVING_SCHEDULE == attrId)
        {
            if(ELEMENT_ID_2 == elemId)
            {
                _bool enabled = (attrValue[0] << 8 | attrValue[1]) ? D_TRUE : D_FALSE;
                UI16 start_minute = (attrValue[2] << 8 | attrValue[3]);
                UI16 end_minute = (attrValue[4] << 8 | attrValue[5]);
                (void)EnergySavingManagerSetSchedule(enabled, start_minute, end_minute);
            }
        }
        else if(ATTR_RO_FLUSHING == attrId)
        {
            if(ELEMENT_ID_2 == elemId)
            {
                UI16 time_s = (attrValue[0] << 8 | attrValue[1]);
                WaterControllerStartROFlushing(time_s);
            }
        }
    }
}

void AppRspToCloud(UI8 elem_id, UI16 attr_id)
{
    static elemStateAttr_t attributes[MAX_NUM_ELEMENT][MAX_NUM_ATTRIBUTE];
    static devStateElemState_t elements[MAX_NUM_ELEMENT];
    static devStateElemsInfo_t full_state;
    static uint16_t changed_element_id;

    if (!g_cloud_is_connected) return;

    iot_err_t iot_err = IOT_OK;
    IOT_DevIdentityInfo_t info = {0};
    uint16_t eid = 0;
    if (IOT_CoreGetRootEid(&eid) != IOT_OK)
    {
            ESP_LOGE(TAG, "Get Eid Failed");
        return;
    }

    iot_err = IOT_DaoDeviceGetIdentityInfo(eid, &info);
    if (iot_err != IOT_OK)
    {
        ESP_LOGE(TAG, "Failed to get device identity info from DAO for EID %04x", eid);
        return;
    }

    UI8 element_count = DeviceElementGetCount();
    for (UI8 i = 0; i < element_count; ++i)
    {
        const DeviceElementInfo *element = DeviceElementGetByIndex(i);
        UI8 attribute_count = 0;
        for (UI8 j = 0; j < element->attribute_count; ++j)
        {
            const DeviceElementAttributeInfo *attribute = &element->attributes[j];
            if (!attribute->has_value)
            {
                continue;
            }

            attributes[i][attribute_count++] = (elemStateAttr_t){
                .attrId = attribute->id,
                .attrSize = attribute->value_length,
                .attrValue = (UI8 *)attribute->value,
            };
        }

        elements[i] = (devStateElemState_t){
            .elemId = element->id,
            .elemType = element->id,
            .numOfAttr = attribute_count,
            .attrList = attributes[i],
        };
    }
    full_state = (devStateElemsInfo_t){.numOfElem = element_count, .elemList = elements};

    changed_element_id = elem_id;
    IOT_CoreStateChangeDeviceData_t dev_data = {
        .eid = eid,
        .mac = info.mac,
        .macLen = info.macSize,
        .devId = info.devId,
        .devIdLen = IOT_DAO_DEVICE_ID_LEN,
        .fullState = &full_state,
        .numChangedElems = 1,
        .changedElemIds = &changed_element_id,
        .changedAttrId = attr_id,
        .pushNotify = false,
    };
    IOT_CoreStateChangeReport_t report = {
        .devices = &dev_data,
        .numDevices = 1,
        .ctx = (IOT_RequestContext_t){0},
    };
    iot_err = IOT_CoreRequestReportStateChanged(&report);
    if (iot_err != IOT_OK)
    {
        ESP_LOGE(TAG, "Failed to report state for element 0x%02x", elem_id);
    }
    IOT_DaoDeviceFreeIdentityInfo(&info);

}

void testTask(void *arg)
{

    _bool test = D_TRUE;
    UI32 value = 0;
    vTaskDelay(pdMS_TO_TICKS(1000));
    while (1)
    { 
        value += 5;
        app_update_water_consumption(value);
        vTaskDelay(pdMS_TO_TICKS(1000));
        app_update_energy_consumption(value);
        vTaskDelay(pdMS_TO_TICKS(1000));
        app_update_tds_in((UI16)value);
        vTaskDelay(pdMS_TO_TICKS(1000));
        app_update_tds_out((UI16)value);
        vTaskDelay(pdMS_TO_TICKS(30000));


        // ESP_LOGI(TAG, "Free heap: %u",
        //         (unsigned int)esp_get_free_heap_size());

        // ESP_LOGI(TAG, "Free internal: %u",
        //         (unsigned int)esp_get_free_internal_heap_size());

        // ESP_LOGI(TAG, "Largest internal block: %u",
        //         (unsigned int)heap_caps_get_largest_free_block(
        //             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        // vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void AppCheckStatusChanged(void)
{
    static UI8 ro_status_old = 0; 
    static UI8 ro_status_new = 0;
    static UI8 ply_status_old = 0;
    static UI8 ply_status_new = 0;

    if(WATER_PURIFIER_TYPE_HOT_COLD == s_model)
    {
        DeviceStatusSnapshot_t status = StatusManagerGetSnapshot();
        ro_status_new = status.ro_status;
        ply_status_new = status.ply_status;
    }
    else if(WATER_PURIFIER_TYPE_STANDARD == s_model)
    {
        ro_status_new = PumpIsActive() ? 1 : 0;
    }

    if(ro_status_old != ro_status_new || ply_status_old != ply_status_new)
    {
        ro_status_old = ro_status_new;
        ply_status_old = ply_status_new;
        AppPushStatusManagerToCloud(ro_status_new, ply_status_new);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void AppCheckVersionFirmwareMainboard(void)
{
    static _bool is_check_done = D_FALSE;
    static UI8 time_out = 0;

    if(is_check_done) return;

    if(++time_out >= 30)
    {
        ESP_LOGW(TAG, "Check version firmware mainboard timeout");
        is_check_done = D_TRUE;
        UI16 firm_version = 0xFFFF;
        AppPushFirmwareVersion(firm_version);
        // WaterControllerResetSystem();
    }

    // if (!g_cloud_is_connected)
    // {
    //     return;
    // }

    if(HasMessageTypeA())
    {
        is_check_done = D_TRUE;
        UI16 firm_version = WaterProfileGetVersionFirmware();
        // post to cloud
        AppPushFirmwareVersion(firm_version);
        // WeaterControllerResetSystem();
        return;
    }
}

static _bool AppValueChangedByPercent(UI32 previous, UI32 current)
{
    if (previous == current)
    {
        return D_FALSE;
    }
    if (previous == 0U)
    {
        return D_TRUE;
    }

    const UI64 difference = (current > previous) ? (UI64)(current - previous)
                                                 : (UI64)(previous - current);
    return (difference * 100U >= (UI64)previous * 5U) ? D_TRUE : D_FALSE;
}

static UI32 AppValueChanged(UI32 previous, UI32 current)
{
    if (previous == current)
    {
        return D_FALSE;
    }
    if (previous == 0U)
    {
        return D_TRUE;
    }

    UI32 difference = (current > previous) ? (current - previous) : (previous - current);

    return difference;
}

static void AppCheckWaterConsumptionChanges(void)
{
    static UI32 last_reported_water_consumption = 0U;

    if (!g_cloud_is_connected)
    {
        // return;
    }

    const UI32 water_consumption = FilterManagerGetWaterConsumption(s_model);
    if (AppValueChanged(last_reported_water_consumption, water_consumption) >= 1U)
    {
        app_update_water_consumption(water_consumption);
        last_reported_water_consumption = water_consumption;
    }
}

static void AppCheckEnergyChanges(void)
{
    static UI32 last_reported_total_energy = 0U;
    static UI32 last_reported_saving_energy = 0U;

    if (!g_cloud_is_connected)
    {
        // return;
    }

    UI64 total_energy = 0;
    UI64 saving_energy = 0;
    UI64 now_ms = esp_timer_get_time() / 1000U;
    
    if(s_model == WATER_PURIFIER_TYPE_HOT_COLD)
    {
        UI64 hot_tank_wh, cold_tank_wh;
        EnergySavingManagerGetSavedEnergy(now_ms/1000U, &hot_tank_wh, &cold_tank_wh);
        total_energy = EnergyConsumptionGetTotal();
        saving_energy = hot_tank_wh + cold_tank_wh;
    }else
    {
        const UI32 system_runtime_seconds = SystemRuntimeGetSeconds(SYS_RUNTIME_INDEX, now_ms);
        const UI32 total_pump_runtime_seconds = PumpGetActiveTotalSeconds(now_ms);
        const UI64 mcu_iot_wh = ((UI64)system_runtime_seconds * MCU_IOT_POWER_W) / SECONDS_PER_HOUR;
        const UI64 pump_valve_wh = ((UI64)total_pump_runtime_seconds * PUMP_VALVE_POWER_W) / SECONDS_PER_HOUR;
        total_energy = mcu_iot_wh + pump_valve_wh;
        saving_energy = 0;
    }


    // Check Energy Consumption Changes
    if (AppValueChanged(last_reported_total_energy, total_energy) >= 200)
    {
        app_update_energy_consumption(total_energy);
        last_reported_total_energy = total_energy;
    }

    // Check saving energy changes
    if (AppValueChanged(last_reported_saving_energy, saving_energy) >= 200)
    {
        AppResponseSavingEnergyToCloud(saving_energy);
        last_reported_saving_energy = saving_energy;
    }
}

WaterPurifierType AppCheckModel(void)
{
    WaterProfileInit(); // init UART
    ESP_LOGI(TAG, " ^-^ Waiting for check product model ... ^-^");
    vTaskDelay(pdMS_TO_TICKS(2500));
    if(HasMessageType1() || HasMessageTypeA())
    {
        AppSetModel(WATER_PURIFIER_TYPE_HOT_COLD);
        WaterControllerResetSystem();
    }
    else
    {
        AppSetModel(WATER_PURIFIER_TYPE_STANDARD);
    }
    ESP_LOGW(TAG, "[MODEL]: %s", s_model == WATER_PURIFIER_TYPE_HOT_COLD ? \
                 "WATER_PURIFIER_TYPE_HOT_COLD" : "WATER_PURIFIER_TYPE_STANDARD");
    return AppGetModel();
}

esp_err_t AppCommonInit(void)
{
    if(EventManagerInit(EventHandler) != D_TRUE)
    {   
        ESP_LOGE(TAG, "Failed to initialize event manager");
        return ESP_FAIL;
    }
    CloudRegisterUpdateStateCallback(CloudUpdateHandler);
    esp_err_t err = LedManagerInit();
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize LED manager");
        return ESP_FAIL;
    }
    WarningManagerInit();
    FilterManagerInit();

    if(SystemRuntimeInit() != D_TRUE)
    {
        ESP_LOGE(TAG, "Failed to initialize System time");
        return ESP_FAIL;
    }

    GroupInit();
    IotSmartInit();
    return ESP_OK;
}

esp_err_t AppInit(WaterPurifierType model)
{
    HandleBootCount();
    if(IOT_CoreIsProvisioned())
        LedManagerSetNetworkConnected();
    
    if(IOT_CoreIsProvisioned())
    {
        RAL_DeinitBle();
    }
    TdsTaskStart();

    FilterManagerSetModel(model);

    if(WATER_PURIFIER_TYPE_HOT_COLD == model)
    {
        EnergySavingManagerInit();
        if (WaterControllerInit() != D_TRUE)
        {
            ESP_LOGE(TAG, "Failed to initialize water controller task");
            return ESP_FAIL;
        }
        StatusManagerInit();
        WaterRuntimeInit();
        // WaterControllerTest();
    }

    AppInitAttributeValue();

    BaseType_t ret = xTaskCreate(AppTask, "AppTask", 4096, NULL, 5, NULL);

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create AppTask");
        return ESP_FAIL;
    }

    ret = xTaskCreate(BootCountResetTimeoutTask, "BootCountReset", 2048, NULL, 4, NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create boot count reset timeout task");
        return ESP_FAIL;
    }

    // xTaskCreate(testTask, "testTask", 2048*2, NULL, 4, NULL);

    ESP_LOGI(TAG, "================= App initialize done =================\n");
    return ESP_OK;
}

// Override
void FilterManagerReportToCloud(const UI8 *percent_values, FilterManagerReportMode mode, FilterTimeIndex filter_index)
{
    ESP_LOGI(TAG, "App report filter used time");
    if ((percent_values == NULL) || (mode > FILTER_MANAGER_REPORT_ONE_FILTER))
    {
        return;
    }

    static const UI16 element_ids[FILTER_RUNTIME_FILTER_COUNT] = {
        ELEMENT_ID_1, ELEMENT_ID_2, ELEMENT_ID_3, ELEMENT_ID_4, ELEMENT_ID_5
    };
    UI8 value[24] = {0U};
    UI8 value_length = 0;

    if(mode == FILTER_MANAGER_REPORT_ONE_FILTER)
    {
        if(filter_index > FILTER_TIME_INDEX_POST) return;
        DeviceElementGetAttributeValue(element_ids[(UI8)filter_index], ATTR_INFOMATION_PRODUCT_USAGE_TIME,
                                            value, 24, &value_length);
        if(value_length != 24) return;

        value[22] = 0;
        value[23] = percent_values[(UI8)filter_index];

        if (DeviceElementSetAttributeValue(element_ids[(UI8)filter_index], ATTR_INFOMATION_PRODUCT_USAGE_TIME,
                                        value, sizeof(value)) == D_TRUE)
        {
            AppRspToCloud((UI8)element_ids[(UI8)filter_index], ATTR_INFOMATION_PRODUCT_USAGE_TIME);
        }
    }
    else if(mode == FILTER_MANAGER_REPORT_ALL_FILTERS)
    {
        for (UI8 idx = 0; idx < FILTER_RUNTIME_FILTER_COUNT; idx++)
        {

            DeviceElementGetAttributeValue(element_ids[idx], ATTR_INFOMATION_PRODUCT_USAGE_TIME,
                                                value, 24, &value_length);
            if(value_length != 24) continue;

            value[22] = 0;
            value[23] = percent_values[(UI8)idx];
            if (DeviceElementSetAttributeValue(element_ids[idx], ATTR_INFOMATION_PRODUCT_USAGE_TIME,
                                            value, sizeof(value)) == D_TRUE)
            {
                AppRspToCloud((UI8)element_ids[idx], ATTR_INFOMATION_PRODUCT_USAGE_TIME);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

// Override
void AppPushTdsReportToCloud(UI16 inlet_ppm, UI16 ro_ppm)
{
    UI8 value[2];
    value[0] = (inlet_ppm >> 8) & 0xff;
    value[1] = inlet_ppm & 0xff;

    if (DeviceElementSetAttributeValue(ELEMENT_ID_2, ATTR_EVENT_TDS_STATUS,
                                    value, 2) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_2, ATTR_EVENT_TDS_STATUS);
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    value[0] = (ro_ppm >> 8) & 0xff;
    value[1] = ro_ppm & 0xff;

    if (DeviceElementSetAttributeValue(ELEMENT_ID_3, ATTR_EVENT_TDS_STATUS,
                                    value, 2) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_3, ATTR_EVENT_TDS_STATUS);
    }
}

// Override
void TdsInputUpdateRecord(UI16 inlet_ppm)
{
    app_update_tds_in(inlet_ppm);
}

// Override
void TdsOutputUpdateRecord(UI16 outlet_ppm)
{
    app_update_tds_out(outlet_ppm);
}

// Override
#if 0
void AppResponseTankStateToCloud(EnergySavingTank_t tank, UI8 enable)
{
    ESP_LOGW(TAG, "[%s] AppResponseTankStateToCloud: %d", tank == ENERGY_SAVING_TANK_HOT ? "HOT" : "TANK", enable);
    UI8 value[2];
    value[0] = 0;
    value[1] = enable ? 1 : 0;
    if (tank == ENERGY_SAVING_TANK_HOT)
    {
        if (DeviceElementSetAttributeValue(ELEMENT_ID_4, ATTR_ONOFF,
                                        value, 2) == D_TRUE)
        {
            AppRspToCloud(ELEMENT_ID_4, ATTR_ONOFF);
        }
    }else
    {
        if (DeviceElementSetAttributeValue(ELEMENT_ID_5, ATTR_ONOFF,
                                        value, 2) == D_TRUE)
        {
            AppRspToCloud(ELEMENT_ID_5, ATTR_ONOFF);
        }
    }
}
#endif

#if 0
void AppResponseTankTemperatureToCloud(EnergySavingTank_t tank, UI16 temperature)
{
    UI8 value[2];
    value[0] = (temperature >> 8) & 0xff;
    value[1] = temperature & 0xff;
    if (tank == ENERGY_SAVING_TANK_HOT)
    {
        if (DeviceElementSetAttributeValue(ELEMENT_ID_4, ATTR_TEMP_SET,
                                        value, 2) == D_TRUE)
        {
            AppRspToCloud(ELEMENT_ID_4, ATTR_TEMP_SET);
        }
    }else
    {
        if (DeviceElementSetAttributeValue(ELEMENT_ID_5, ATTR_TEMP_SET,
                                        value, 2) == D_TRUE)
        {
            AppRspToCloud(ELEMENT_ID_5, ATTR_TEMP_SET);
        }
    }
}
#endif

// Override
void AppReportHotTankTemperatureToCloud(const UI8 state, const UI8 temperature)
{
    ESP_LOGW(TAG, "AppReportHotTankTemperatureToCloud: %d %d/2", state, temperature);
    UI8 value[2];
    value[0] = (temperature >> 8) & 0xff;
    value[1] = temperature & 0xff;
    if (DeviceElementSetAttributeValue(ELEMENT_ID_4, ATTR_TEMP_SET,
                                    value, 2) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_4, ATTR_TEMP_SET);
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    value[0] = 0;
    value[1] = state ? 1 : 0;
    if (DeviceElementSetAttributeValue(ELEMENT_ID_4, ATTR_ONOFF,
                                    value, 2) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_4, ATTR_ONOFF);
    }
}

// Override
void AppReportColdTankTemperatureToCloud(const UI8 state, const UI8 temperature)
{
    ESP_LOGW(TAG, "AppReportColdTankTemperatureToCloud: %d %d/2 ", state,  temperature);
    UI8 value[2];
    value[0] = (temperature >> 8) & 0xff;
    value[1] = temperature & 0xff;
    if (DeviceElementSetAttributeValue(ELEMENT_ID_5, ATTR_TEMP_SET,
                                        value, 2) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_5, ATTR_TEMP_SET);
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    value[0] = 0;
    value[1] = state ? 1 : 0;
    if (DeviceElementSetAttributeValue(ELEMENT_ID_5, ATTR_ONOFF,
                                    value, 2) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_5, ATTR_ONOFF);
    }
}

void AppPushWaterConsumptionToCloud(void)
{
    ESP_LOGI(TAG, "App report water consumption");
    UI32 water_consumption = FilterManagerGetWaterConsumption(s_model);
    UI8 value[4];
    value[0] = (water_consumption >> 24) & 0xff;
    value[1] = (water_consumption >> 16) & 0xff;
    value[2] = (water_consumption >> 8) & 0xff;
    value[3] = water_consumption & 0xff;

    if (DeviceElementSetAttributeValue(ELEMENT_ID_1, ATTR_INFOMATION_WATER_CONSUMER,
                                    value, 4) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_1, ATTR_INFOMATION_WATER_CONSUMER);
    }
}

void AppPushWarningToCloud(void)
{
    UI32 warning_mask = WarningManagerGetMask();
    UI8 value[4];
    value[0] = (warning_mask >> 24) & 0xff;
    value[1] = (warning_mask >> 16) & 0xff;
    value[2] = (warning_mask >> 8) & 0xff;
    value[3] = warning_mask & 0xff;

    if (DeviceElementSetAttributeValue(ELEMENT_ID_1, ATTR_INFO_CURRENT_ISSUES_STATUS,
                                    value, 4) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_1, ATTR_INFO_CURRENT_ISSUES_STATUS);
    }    
}

void AppResponseSavingEnergyToCloud(UI32 saving_energy)
{
    UI8 value[4];
    value[0] = (saving_energy >> 24) & 0xff;
    value[1] = (saving_energy >> 16) & 0xff;
    value[2] = (saving_energy >> 8) & 0xff;
    value[3] = saving_energy & 0xff;   

    ESP_LOGI(TAG, "App response energy to cloud: saving: %lu Wh", saving_energy); 

    if (DeviceElementSetAttributeValue(ELEMENT_ID_3, ATTR_ENERGY,
                                    value, 4) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_3, ATTR_ENERGY);
    }
}

void AppPushStatusManagerToCloud(UI8 ro_stt, UI8 ply_stt)
{
    UI8 value[4];
    value[1] = ro_stt; 
    value[3] = ply_stt;

    if (DeviceElementSetAttributeValue(ELEMENT_ID_1, ATTR_STATUS,
                                    value, 4) == D_TRUE)
    {
        ESP_LOGI(TAG, "App report status to cloud");
        AppRspToCloud(ELEMENT_ID_1, ATTR_STATUS);
    } 
}

void AppPushFirmwareVersion(UI16 version)
{
    UI8 value[2];
    value[0] = (version >> 8) & 0xff; 
    value[1] = version & 0xff;
    if (DeviceElementSetAttributeValue(ELEMENT_ID_1, ATTR_FIRMWARE_VERSION,
                                    value, 2) == D_TRUE)
    {
        ESP_LOGW(TAG, "App report firmware version of mainboard %04x", version);
        AppRspToCloud(ELEMENT_ID_1, ATTR_FIRMWARE_VERSION);
    } 
}

void AppInitAttributeValue(void)
{
    DeviceElementResetAttributeValues();
    for(UI8 idx = 0; idx < FILTER_RUNTIME_FILTER_COUNT; idx++)
    {
        UI8 filter_id[FILTER_PID_SIZE];
        UI16 element_ids[FILTER_RUNTIME_FILTER_COUNT] = {
            ELEMENT_ID_1, ELEMENT_ID_2, ELEMENT_ID_3, ELEMENT_ID_4, ELEMENT_ID_5
        };
        if(FilterManagerGetPid((FilterTimeIndex)idx, filter_id))
        {
            // Set Filter ID
            ESP_LOGI(TAG, "[Filter idx %d] ", idx);
            for (size_t i = 0; i < FILTER_PID_SIZE; i++)
            {
                printf("%02x ", filter_id[i]);
            }
            printf("\n");

            DeviceElementSetAttributeValueOffset(element_ids[idx], ATTR_INFOMATION_PRODUCT_USAGE_TIME, 0, filter_id, FILTER_PID_SIZE);
        }
        UI32 lifetime = 0;
        if(FilterManagerGetLifetimeSeconds((FilterTimeIndex)idx, &lifetime)) 
        {
            UI8 expire_time_s[4];
            expire_time_s[0] = (lifetime>>24) & 0xff;
            expire_time_s[1] = (lifetime>>16) & 0xff;
            expire_time_s[2] = (lifetime>>8) & 0xff;
            expire_time_s[3] = lifetime & 0xff;
            DeviceElementSetAttributeValueOffset(element_ids[idx], ATTR_INFOMATION_PRODUCT_USAGE_TIME, 16, filter_id, FILTER_PID_SIZE);
        }
    }

    UI8 value[8];
    UI32 water_consumption = FilterManagerGetWaterConsumption(s_model);
    value[0] = (water_consumption >> 24) & 0xff;
    value[1] = (water_consumption >> 16) & 0xff;
    value[2] = (water_consumption >> 8) & 0xff;
    value[3] = water_consumption & 0xff;
    (void)DeviceElementSetAttributeValue(ELEMENT_ID_1, ATTR_INFOMATION_WATER_CONSUMER, value, 4);
  
    UI32 warning_mask = WarningManagerGetMask();
    value[0] = (warning_mask >> 24) & 0xff;
    value[1] = (warning_mask >> 16) & 0xff;
    value[2] = (warning_mask >> 8) & 0xff;
    value[3] = warning_mask & 0xff;
    (void)DeviceElementSetAttributeValue(ELEMENT_ID_1, ATTR_INFO_CURRENT_ISSUES_STATUS, value, 4);


    if(WATER_PURIFIER_TYPE_HOT_COLD == s_model)
    {
        DeviceStatusSnapshot_t status = StatusManagerGetSnapshot();
        value[1] = status.ro_status;
        value[3] = status.ply_status;
    }
    else if(WATER_PURIFIER_TYPE_STANDARD == s_model)
    {
        value[1] = PumpIsActive() ? 1 : 0;
    }
    (void)DeviceElementSetAttributeValue(ELEMENT_ID_1, ATTR_STATUS, value, 4);
    

    UI64 now_s = esp_timer_get_time() / 1000000U;
    UI64 hot_tank_wh, cold_tank_wh;
    EnergySavingManagerGetSavedEnergy(now_s, &hot_tank_wh, &cold_tank_wh);
    UI64 total_energy = EnergyConsumptionGetTotal();
    UI64 saving_energy = hot_tank_wh + cold_tank_wh;

    if(WATER_PURIFIER_TYPE_STANDARD == s_model)
    {
        saving_energy = 0;
    }

    value[0] = (total_energy >> 24) & 0xff;
    value[1] = (total_energy >> 16) & 0xff;
    value[2] = (total_energy >> 8) & 0xff;
    value[3] = total_energy & 0xff; 
    (void)DeviceElementSetAttributeValue(ELEMENT_ID_2, ATTR_ENERGY, value, 4);

    value[0] = (saving_energy >> 24) & 0xff;
    value[1] = (saving_energy >> 16) & 0xff;
    value[2] = (saving_energy >> 8) & 0xff;
    value[3] = saving_energy & 0xff;   
    (void)DeviceElementSetAttributeValue(ELEMENT_ID_3, ATTR_ENERGY, value, 4);


    if(WATER_PURIFIER_TYPE_HOT_COLD == s_model)
    {
        // target temperature of HOT/COLD tank
        UI8 temperature = WaterProfileGetHotTargetTemperature();
        value[0] = (temperature >> 8) & 0xff;
        value[1] = temperature & 0xff;
        (void)DeviceElementSetAttributeValue(ELEMENT_ID_4, ATTR_TEMP_SET, value, 2);

        temperature = WaterProfileGetColdTargetTemperature();
        value[0] = (temperature >> 8) & 0xff;
        value[1] = temperature & 0xff;
        (void)DeviceElementSetAttributeValue(ELEMENT_ID_5, ATTR_TEMP_SET, value, 2);

        // HOT/COLD tank state
        UI8 setting = WaterProfileGetSetting();
        value[0] = 0;
        value[1] = setting & HOT_SWITCH_BIT ? 1 : 0;
        (void)DeviceElementSetAttributeValue(ELEMENT_ID_4, ATTR_ONOFF, value, 2);

        value[0] = 0;
        value[1] = setting & COLD_SWITCH_BIT ? 1 : 0;
        (void)DeviceElementSetAttributeValue(ELEMENT_ID_5, ATTR_ONOFF, value, 2);
    }    

}


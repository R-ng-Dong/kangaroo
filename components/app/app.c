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

static const char *TAG = "APP";

#define SYSTEM_NVS_NAMESPACE "water_machine"
#define SYSTEM_NVS_KEY "boot_count"

void AppPushWarningToCloud(void);
void FilterManagerReportToCloud(const UI8 *percent_values, FilterManagerReportMode mode, FilterTimeIndex filter_index);
void AppResponseEnergyToCloud(UI32 total_energy, UI32 saving_energy);
void AppPushWaterConsumptionToCloud(void);
void AppPushStatusManagerToCloud(UI8 ro_stt, UI8 ply_stt);
void AppPushFirmwareVersion(UI16 version);

static void AppCheckEnergyConsumptionChanges(void);
static void AppCheckWaterConsumptionChanges(void);
static void AppCheckStatusChanged(void);
static void AppCheckVersionFirmwareMainboard(void);

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

#if DEVICE_TYPE == WATER_PURIFIER_TYPE_HOT_COLD
    INT hour = 0; INT minute = 0; INT year = 0;
#endif

    while (1)
    {
#if DEVICE_TYPE == WATER_PURIFIER_TYPE_HOT_COLD
        StatusManagerRefresh();
        StatusManagerCheckWarnings();
        GetNowTime(&year, &hour, &minute, NULL);
        if (year >= 125)
        {
            UI16 minute_of_day = hour * 60 + minute;
            EnergySavingManagerUpdate(minute_of_day,
                                      (UI64)(esp_timer_get_time() / 1000000ULL));
        }
#elif DEVICE_TYPE == WATER_PURIFIER_TYPE_STANDARD
        WarningManagerUpdate(WARNING_ID_ER00055_RO_PUMP_RUNTIME_OVER_60_MIN, PumpGetElapsedTimeSeconds(now_ms) >= RO_PUMP_OVERRUN_TIMEOUT_SECONDS);
#endif
        // check status RO, PLY
        AppCheckStatusChanged();
        // check filter
        FilterManagerTick(now_ms);

        if (++energy_report_count >= TIMEOUT_RESPONSE_ENERGY_TO_CLOUD_COUNT)
        {
            energy_report_count = 0U;
            AppCheckEnergyConsumptionChanges();
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
            SystemRuntimeUpdate(now_ms);
        }
        vTaskDelay(pdMS_TO_TICKS(APP_INTERVAL_MS));
#if DEVICE_TYPE == WATER_PURIFIER_TYPE_HOT_COLD
        AppCheckVersionFirmwareMainboard();
#endif
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
            // SystemRuntimeReconfig(0, esp_timer_get_time()/1000);
            
            (void)DeviceElementSetAttributeValueOffset(elemId, ATTR_INFOMATION_PRODUCT_USAGE_TIME, 2, Pid, FILTER_PID_SIZE);
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
        .devIdLen = DEVICE_ID_LEN,
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
    // UI8 value[24] = {0x46, 0x4C, 0x54, 0x2D, 0x52, 0x4F, 0x2D, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x6B, 0xBE, 0xB7, 0x00, 0x00, 0x00, 0x03, 0xE8};
    // UI8 life = 0;
    // vTaskDelay(pdMS_TO_TICKS(1000));
    // UI16 inlet = 10 ;
    // UI16 outlet = 35;
    // UI8 percent_values[5]= {0,1,2,3,4};
    _bool test = D_TRUE;
    // UI32 total_energy = 1000;
    // UI32 saving_energy = 500;
    vTaskDelay(pdMS_TO_TICKS(5000));
    while (1)
    {
        // AppPushTdsReportToCloud(inlet, outlet);
        // inlet++; outlet++;
        // vTaskDelay(pdMS_TO_TICKS(3000));
        // FilterManagerReportToCloud(percent_values, FILTER_MANAGER_REPORT_ALL_FILTERS, (FilterTimeIndex)0);
        // vTaskDelay(pdMS_TO_TICKS(3000));
        // for (size_t i = 0; i < 5; i++)
        // {
        //     percent_values[i]++;
        //     if(percent_values[i]>100) percent_values[i] = 0;
        // } 
        // AppResponseEnergyToCloud(total_energy, saving_energy); total_energy += 5; saving_energy += 5;
        // vTaskDelay(pdMS_TO_TICKS(3000));
        // WarningManagerUpdate(WARNING_ID_ER00051_INLET_WATER_QUALITY_WARNING, test); test = !test;
        // AppPushWaterConsumptionToCloud();
        WaterControllerSetFeatureHot(1, 80); test = !test;
        // WaterControllerSetFilterRuntimeMinutes(FILTER_TIME_INDEX_1ST, life); life++;
        vTaskDelay(pdMS_TO_TICKS(5000));
        WaterControllerSetFeatureHot(0, 80); test = !test;
        vTaskDelay(pdMS_TO_TICKS(5000));

        WaterControllerSetFeatureHot(1, 50); test = !test;
        // WaterControllerSetFilterRuntimeMinutes(FILTER_TIME_INDEX_1ST, life); life++;
        vTaskDelay(pdMS_TO_TICKS(5000));
        WaterControllerSetFeatureHot(0, 50); test = !test;
        vTaskDelay(pdMS_TO_TICKS(5000));

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

#if DEVICE_TYPE == WATER_PURIFIER_TYPE_HOT_COLD
    DeviceStatusSnapshot_t status = StatusManagerGetSnapshot();
    ro_status_new = status.ro_status;
    ply_status_new = status.ply_status;
#elif DEVICE_TYPE == WATER_PURIFIER_TYPE_STANDARD
    ro_status_new = PumpIsActive() ? 1 : 0;
#endif
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
    if (!g_cloud_is_connected)
    {
        return;
    }

    if(is_check_done) return;
    if(HasMessageTypeA())
    {
        is_check_done = D_TRUE;
        UI16 firm_version = WaterProfileGetVersionFirmware();
        // post to cloud
        AppPushFirmwareVersion(firm_version);
        WaterControllerResetSystem();
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

static void AppCheckWaterConsumptionChanges(void)
{
    static UI32 last_reported_water_consumption = 0U;

    if (!g_cloud_is_connected)
    {
        return;
    }

    const UI32 water_consumption = FilterManagerGetWaterConsumption();
    if (AppValueChangedByPercent(last_reported_water_consumption, water_consumption))
    {
        AppPushWaterConsumptionToCloud();
        last_reported_water_consumption = water_consumption;
    }
}

static void AppCheckEnergyConsumptionChanges(void)
{
    static UI32 last_reported_total_energy = 0U;
    static UI32 last_reported_saving_energy = 0U;

    if (!g_cloud_is_connected)
    {
        return;
    }

    UI64 now_s = esp_timer_get_time() / 1000000U;
    UI64 hot_tank_wh, cold_tank_wh;
    EnergySavingManagerGetSavedEnergy(now_s, &hot_tank_wh, &cold_tank_wh);
    const UI64 total_energy = EnergyConsumptionGetTotal();
    const UI64 saving_energy = hot_tank_wh + cold_tank_wh;

    if (AppValueChangedByPercent(last_reported_total_energy, total_energy) ||
        AppValueChangedByPercent(last_reported_saving_energy, saving_energy))
    {
        AppResponseEnergyToCloud(total_energy, saving_energy);
        last_reported_total_energy = total_energy;
        last_reported_saving_energy = saving_energy;
    }
}

esp_err_t AppInit(void)
{
    // HandleBootCount();

    if(EventManagerInit(EventHandler) != D_TRUE)
    {   
        ESP_LOGE(TAG, "Failed to initialize event manager");
        return ESP_FAIL;
    }
    esp_err_t err = LedManagerInit();
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize LED manager");
        return ESP_FAIL;
    }
        if(IOT_CoreIsProvisioned())
            LedManagerSetNetworkConnected();


    WarningManagerInit();
    DeviceElementResetAttributeValues();
    FilterManagerInit();
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

            DeviceElementSetAttributeValueOffset(element_ids[idx], ATTR_INFOMATION_PRODUCT_USAGE_TIME, 2, filter_id, FILTER_PID_SIZE);
        }
    }

    SystemRuntimeInit();
    EnergySavingManagerInit();

    CloudRegisterUpdateStateCallback(CloudUpdateHandler);

#if DEVICE_TYPE == WATER_PURIFIER_TYPE_HOT_COLD
    WaterProfileInit();
    if (WaterControllerInit() != D_TRUE)
    {
        ESP_LOGE(TAG, "Failed to initialize water controller task");
        return ESP_FAIL;
    }
    StatusManagerInit();
    WaterRuntimeInit();
    WaterControllerTest();
#endif
    if(IOT_CoreIsProvisioned())
    {
        RAL_DeinitBle();
    }
    TdsTaskStart();

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
            value[23] = percent_values[(UI8)filter_index];
            if (DeviceElementSetAttributeValue(element_ids[idx], ATTR_INFOMATION_PRODUCT_USAGE_TIME,
                                            value, sizeof(value)) == D_TRUE)
            {
                AppRspToCloud((UI8)element_ids[idx], ATTR_INFOMATION_PRODUCT_USAGE_TIME);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
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

    vTaskDelay(pdMS_TO_TICKS(1000));

    value[0] = (ro_ppm >> 8) & 0xff;
    value[1] = ro_ppm & 0xff;

    if (DeviceElementSetAttributeValue(ELEMENT_ID_3, ATTR_EVENT_TDS_STATUS,
                                    value, 2) == D_TRUE)
    {
        AppRspToCloud(ELEMENT_ID_3, ATTR_EVENT_TDS_STATUS);
    }
}
// Override
void AppResponseTankStateToCloud(EnergySavingTank_t tank, UI8 enable)
{
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

void AppPushWaterConsumptionToCloud(void)
{
    ESP_LOGI(TAG, "App report water consumption");
    UI32 water_consumption = FilterManagerGetWaterConsumption();
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

void AppResponseEnergyToCloud(UI32 total_energy, UI32 saving_energy)
{
    UI8 value[8];
    value[0] = (total_energy >> 24) & 0xff;
    value[1] = (total_energy >> 16) & 0xff;
    value[2] = (total_energy >> 8) & 0xff;
    value[3] = total_energy & 0xff; 
    value[4] = (saving_energy >> 24) & 0xff;
    value[5] = (saving_energy >> 16) & 0xff;
    value[6] = (saving_energy >> 8) & 0xff;
    value[7] = saving_energy & 0xff;   
    if (DeviceElementSetAttributeValue(ELEMENT_ID_2, ATTR_ENERGY,
                                    value, 8) == D_TRUE)
    {
        ESP_LOGI(TAG, "App response energy to cloud: total: %lu Wh- saving: %lu Wh", total_energy, saving_energy);
        AppRspToCloud(ELEMENT_ID_2, ATTR_ENERGY);
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
        ESP_LOGI(TAG, "App report firmware version of mainboard %04x", version);
        AppRspToCloud(ELEMENT_ID_1, ATTR_FIRMWARE_VERSION);
    } 
}

#include "report_alert.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "IOT_Core.h"
#include "IOT_CoreServiceMsg.h"
#include "IOT_DaoDeviceMgmt.h"
#include "device_element_manager.h"
#include "warning_manager.h"
#include "esp_random.h"

#define REPORT_JSON_CAPACITY IOT_SERVICE_MSG_PAYLOAD_MAX
#define ALERT_EVENT_CAPACITY 8U

typedef struct
{
    WarningId id;
    uint16_t code;
    const char *severity;
    char aid[9];
    bool active;
} OpenAlert;

static OpenAlert s_alerts[ALERT_EVENT_CAPACITY];
static uint16_t s_aid_counter;
static uint16_t s_aid_prefix;

static const WarningId s_warning_ids[] = {
    WARNING_ID_ER00051_INLET_WATER_QUALITY_WARNING,
    WARNING_ID_ER00052_OUTLET_WATER_QUALITY_FAULT,
    WARNING_ID_ER00053_WATER_LEAK, WARNING_ID_ER00054_TANK_LOW,
    WARNING_ID_ER00055_RO_PUMP_RUNTIME_OVER_60_MIN,
    WARNING_ID_ER00056_FILTER1_LIFE_70, WARNING_ID_ER00057_FILTER2_LIFE_70,
    WARNING_ID_ER00058_FILTER3_LIFE_70, WARNING_ID_ER00059_FILTER4_LIFE_70,
    WARNING_ID_ER00060_FILTER5_LIFE_70, WARNING_ID_ER00061_FILTER1_LIFE_90,
    WARNING_ID_ER00062_FILTER2_LIFE_90, WARNING_ID_ER00063_FILTER3_LIFE_90,
    WARNING_ID_ER00064_FILTER4_LIFE_90, WARNING_ID_ER00065_FILTER5_LIFE_90,
    WARNING_ID_ER00066_OUTPUT_WATER_QUALITY_WARNING,
    WARNING_ID_ER00067_HEATER_SENSOR_FAULT, WARNING_ID_ER00068_HEATER_RUNTIME_OVER_1H,
    WARNING_ID_ER00069_COOLER_SENSOR_FAULT, WARNING_ID_ER00070_COOLER_RUNTIME_OVER_2H,
    WARNING_ID_ER00071, WARNING_ID_ER00072, WARNING_ID_ER00073, WARNING_ID_ER00074,
};

static const char *severity_for(uint16_t code)
{
    switch (code)
    {
        case 51: case 56: case 57: case 58: case 59: case 60:
        case 61: case 62: case 63: case 64: case 65: case 66:
        case 67: case 68: case 69: case 70: case 71: case 72: case 73: case 74:
            return "warning";
        default: return "error";
    }
}

static bool device_identity(char mac[13], char device_id[25])
{
    uint16_t eid = 0;
    IOT_DevIdentityInfo_t identity = {0};
    if (IOT_CoreGetRootEid(&eid) != IOT_OK || IOT_DaoDeviceGetIdentityInfo(eid, &identity) != IOT_OK)
        return false;
    if (identity.mac == NULL || identity.macSize != 6U)
    {
        IOT_DaoDeviceFreeIdentityInfo(&identity);
        return false;
    }
    for (size_t i = 0; i < 6; ++i) snprintf(mac + i * 2, 3, "%02x", identity.mac[i]);
    for (size_t i = 0; i < IOT_DAO_DEVICE_ID_LEN; ++i)
        snprintf(device_id + i * 2, 3, "%02x", identity.devId[i]);
    IOT_DaoDeviceFreeIdentityInfo(&identity);
    return true;
}

static OpenAlert *find_alert(WarningId id)
{
    for (size_t i = 0; i < ALERT_EVENT_CAPACITY; ++i)
        if (s_alerts[i].id == id) return &s_alerts[i];
    return NULL;
}

static OpenAlert *allocate_alert(WarningId id)
{
    OpenAlert *slot = NULL;
    for (size_t i = 0; i < ALERT_EVENT_CAPACITY; ++i)
        if (s_alerts[i].id == WARNING_ID_NONE) { slot = &s_alerts[i]; break; }
    if (slot == NULL) return NULL;
    if (s_aid_prefix == 0U) s_aid_prefix = (uint16_t)esp_random();
    slot->id = id;
    slot->code = (uint16_t)(51U + __builtin_ctz((uint32_t)id));
    slot->severity = severity_for(slot->code);
    slot->active = true;
    snprintf(slot->aid, sizeof(slot->aid), "%04x%04x", s_aid_prefix, ++s_aid_counter);
    return slot;
}

static iot_err_t publish_alert(const OpenAlert *alert, bool active, uint8_t tier)
{
    char mac[13] = {0}, device_id[25] = {0};
    if (!device_identity(mac, device_id)) return IOT_ERR_DATA_NOT_FOUND;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return IOT_ERR_NO_MEM;
    cJSON_AddNumberToObject(root, "v", 2);
    cJSON_AddStringToObject(root, "mac", mac);
    cJSON_AddStringToObject(root, "deviceId", device_id);
    cJSON_AddStringToObject(root, "aid", alert->aid);
    char code[8];
    snprintf(code, sizeof(code), "ER%05u", alert->code);
    cJSON_AddStringToObject(root, "code", code);
    cJSON_AddStringToObject(root, "severity", alert->severity);
    cJSON_AddStringToObject(root, "state", active ? "active" : "cleared");
    cJSON_AddNumberToObject(root, "tier", tier);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) return IOT_ERR_NO_MEM;
    size_t len = strlen(json);
    iot_err_t err = len <= 2048U ? IOT_CoreServiceMsgPublish("alerts", json, len) : IOT_ERR_INVALID_SIZE;
    cJSON_free(json);
    return err;
}

/* Hook called by WarningManager on every warning-bit transition. */
// Override
void WarningManagerOnChanged(UI32 active_mask, UI32 changed_mask)
{
    for (size_t i = 0; i < sizeof(s_warning_ids) / sizeof(s_warning_ids[0]); ++i)
    {
        WarningId id = s_warning_ids[i];
        if (((UI32)id & changed_mask) == 0U) continue;
        bool active = ((UI32)id & active_mask) != 0U;
        OpenAlert *alert = find_alert(id);
        if (active && alert == NULL) alert = allocate_alert(id);
        if (alert == NULL) continue;
        alert->active = active;
        if (IOT_CoreIsCloudConnected() && publish_alert(alert, active, 1U) == IOT_OK && !active)
            memset(alert, 0, sizeof(*alert));
    }
}

iot_err_t ReportAlertPublishReport(void)
{
    char mac[13] = {0}, device_id[25] = {0};
    if (!device_identity(mac, device_id)) return IOT_ERR_DATA_NOT_FOUND;
    cJSON *root = cJSON_CreateObject();
    cJSON *state = cJSON_CreateObject();
    cJSON *alerts = cJSON_CreateArray();
    if (root == NULL || state == NULL || alerts == NULL)
    {
        cJSON_Delete(root);
        cJSON_Delete(state);
        cJSON_Delete(alerts);
        return IOT_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "v", 2);
    cJSON_AddStringToObject(root, "mac", mac);

    UI8 element_count = DeviceElementGetCount();
    for (UI8 i = 0; i < element_count; ++i)
    {
        const DeviceElementInfo *element = DeviceElementGetByIndex(i);
        if (element == NULL) continue;
        char element_key[6];
        snprintf(element_key, sizeof(element_key), "%u", element->id);
        cJSON *element_state = cJSON_CreateObject();
        if (element_state == NULL) { cJSON_Delete(root); cJSON_Delete(state); cJSON_Delete(alerts); return IOT_ERR_NO_MEM; }
        for (UI8 j = 0; j < element->attribute_count; ++j)
        {
            const DeviceElementAttributeInfo *attr = &element->attributes[j];
            if (!attr->has_value) continue;
            char attr_key[6];
            snprintf(attr_key, sizeof(attr_key), "%u", attr->id);
            cJSON *values = cJSON_CreateArray();
            if (values == NULL) { cJSON_Delete(element_state); cJSON_Delete(root); cJSON_Delete(state); cJSON_Delete(alerts); return IOT_ERR_NO_MEM; }
            cJSON_AddItemToArray(values, cJSON_CreateNumber(attr->id));
            for (UI8 k = 0; k + 1U < attr->value_length; k += 2U)
            {
                uint16_t value = (uint16_t)(((uint16_t)attr->value[k] << 8) | attr->value[k + 1U]);
                cJSON_AddItemToArray(values, cJSON_CreateNumber(value));
            }
            cJSON_AddItemToObject(element_state, attr_key, values);
        }
        cJSON_AddItemToObject(state, element_key, element_state);
    }
    for (size_t i = 0; i < ALERT_EVENT_CAPACITY; ++i)
    {
        const OpenAlert *alert = &s_alerts[i];
        if (alert->id == WARNING_ID_NONE || !alert->active) continue;
        cJSON *entry = cJSON_CreateObject();
        if (entry == NULL) { cJSON_Delete(root); cJSON_Delete(state); cJSON_Delete(alerts); return IOT_ERR_NO_MEM; }
        char code[8];
        snprintf(code, sizeof(code), "ER%05u", alert->code);
        cJSON_AddStringToObject(entry, "code", code);
        cJSON_AddStringToObject(entry, "severity", alert->severity);
        cJSON_AddStringToObject(entry, "aid", alert->aid);
        cJSON_AddItemToArray(alerts, entry);
    }
    cJSON_AddItemToObject(root, "state", state);
    cJSON_AddItemToObject(root, "alerts", alerts);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) return IOT_ERR_NO_MEM;
    size_t len = strlen(json);
    iot_err_t err = !IOT_CoreIsCloudConnected() ? IOT_ERR_NO_NETWORK_CONNECT :
                    (len <= REPORT_JSON_CAPACITY ? IOT_CoreServiceMsgPublish("report", json, len) : IOT_ERR_INVALID_SIZE);
    cJSON_free(json);
    return err;
}

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
#include "esp_timer.h"
#include "nvs.h"

#define REPORT_JSON_CAPACITY IOT_SERVICE_MSG_PAYLOAD_MAX
#define ALERT_EVENT_CAPACITY 8U

typedef struct
{
    WarningId id;
    uint16_t code;
    char aid[9];
    bool used;
    bool state_active;
    bool pending;
    bool pending_active;
    uint8_t tier;
    uint8_t sends_in_tier;
    int64_t next_send_us;
} OpenAlert;

static OpenAlert s_alerts[ALERT_EVENT_CAPACITY];
static uint16_t s_aid_counter;
static uint16_t s_aid_prefix;
static int64_t s_last_publish_us;

#define ALERT_GLOBAL_GAP_US (10LL * 1000000LL)
#define CONFIG_NAMESPACE "ral_config"
#define CONFIG_KEY "snapshot"

typedef struct
{
    uint32_t version[4];
    int32_t values[36];
} ReportConfig;

typedef struct
{
    const char *key;
    uint8_t scope;
    int32_t def, min, max;
} ConfigKey;

enum
{
    SCOPE_CORE,
    SCOPE_STATE,
    SCOPE_ALERT,
    SCOPE_GENERAL
};

static const char *s_scopes[] = {"core", "state", "alert", "general"};
static const ConfigKey s_config_keys[] = {
    {"alert.tier1.intervalMs", 0, 15000, 5000, INT32_MAX}, 
    {"alert.tier1.maxSends", 0, 5, 1, INT32_MAX}, 
    {"alert.tier2.intervalMs", 0, 60000, 5000, INT32_MAX}, 
    {"alert.tier2.maxSends", 0, 5, 1, INT32_MAX}, 
    {"alert.tier3.intervalMs", 0, 6000000, 5000, INT32_MAX}, 
    {"report.intervalMs", 0, 900000, 60000, 604800000}, 
    {"state.tds.deltaPct", 1, 5, 1, 50}, 
    {"state.tds.floorPpm", 1, 2, 0, 50}, 
    {"state.tds.minGapSec", 1, 60, 10, 3600}, 
    {"state.tds.runPeriodSec", 1, 300, 60, 3600}, 
    {"state.tds.idlePeriodSec", 1, 3600, 300, 86400}, 
    {"state.temp.hotBandC", 1, 5, 1, 30}, 
    {"state.temp.coldBandC", 1, 3, 1, 20}, 
    {"state.temp.periodSec", 1, 3600, 300, 86400}, 
    {"state.filter.stepPct", 1, 1, 1, 20}, 
    {"state.water.deltaMl", 1, 1000, 100, 100000}, 
    {"state.water.minGapSec", 1, 900, 60, 86400}, 
    {"state.energy.deltaWh", 1, 200, 10, 10000}, 
    {"state.energy.minGapSec", 1, 900, 60, 86400}, 
    {"state.live.intervalSec", 1, 60, 10, 600}, 
    {"state.live.ttlSec", 1, 120, 30, 1800}, 
    {"alert.tdsIn.maxPpm", 2, 0, 1, 2000}, 
    {"alert.tdsOut.maxPpm", 2, 0, 1, 2000}, 
    {"alert.tdsOut.warnPpm", 2, 0, 1, 2000}, 
    {"alert.quality.onSec", 2, 60, 1, 3600}, 
    {"alert.quality.offSec", 2, 300, 1, 3600}, 
    {"alert.leak.onSec", 2, 10, 1, 3600}, 
    {"alert.leak.offSec", 2, 60, 1, 3600}, 
    {"alert.sensor.onSec", 2, 30, 1, 3600}, 
    {"alert.sensor.offSec", 2, 60, 1, 3600}, 
    {"alert.pump.maxRunMin", 2, 60, 5, 600}, 
    {"alert.hot.maxRunMin", 2, 60, 5, 600}, 
    {"alert.cold.maxRunMin", 2, 120, 5, 600}, 
    {"alert.filter.warnPct", 2, 30, 1, 99}, 
    {"alert.filter.criticalPct", 2, 10, 1, 99}, 
    {"general.tzOffsetMin", 3, 420, -720, 840}
};
static ReportConfig s_config;
static bool s_config_loaded;

static void config_load(void)
{
    if (s_config_loaded)
        return;
    for (size_t i = 0; i < sizeof(s_config_keys) / sizeof(s_config_keys[0]); ++i)
        s_config.values[i] = s_config_keys[i].def;
    nvs_handle_t h;
    if (nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &h) == ESP_OK)
    {
        size_t size = sizeof(s_config);
        if (nvs_get_blob(h, CONFIG_KEY, &s_config, &size) != ESP_OK || size != sizeof(s_config))
        {
            memset(s_config.version, 0, sizeof(s_config.version));
            for (size_t i = 0; i < sizeof(s_config_keys) / sizeof(s_config_keys[0]); ++i)
                s_config.values[i] = s_config_keys[i].def;
        }
        nvs_close(h);
    }
    s_config_loaded = true;
}

static bool config_apply(const cJSON *root)
{
    const cJSON *scope = cJSON_GetObjectItemCaseSensitive(root, "scope"); //cJSON_GetObjectItem
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "configVersion");
    const cJSON *values = cJSON_GetObjectItemCaseSensitive(root, "values");
    uint8_t si = SCOPE_CORE;
    if (scope)
    {
        if (!cJSON_IsString(scope))
            return false;
        for (si = 0; si < 4 && strcmp(scope->valuestring, s_scopes[si]) != 0; ++si)
        {
        }
        if (si == 4)
        {
            si = SCOPE_CORE;
            // return false;
        }
    }
    if (!cJSON_IsNumber(ver) || ver->valuedouble < 0 || ver->valuedouble > UINT32_MAX || ver->valuedouble != (double)(uint32_t)ver->valuedouble || !cJSON_IsObject(values))
        return false;
    config_load();
    uint32_t version = (uint32_t)ver->valuedouble;
    if (version <= s_config.version[si])
        return true;
    ReportConfig next = s_config;
    for (size_t i = 0; i < sizeof(s_config_keys) / sizeof(s_config_keys[0]); ++i)
    {
        const ConfigKey *k = &s_config_keys[i];
        if (k->scope != si)
            continue;
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(values, k->key);
        int32_t n = k->def;
        if (v)
        {
            if (!cJSON_IsNumber(v) || v->valuedouble < (double)INT32_MIN || v->valuedouble > (double)INT32_MAX || v->valuedouble != (double)(int32_t)v->valuedouble)
                return false;
            n = (int32_t)v->valuedouble;
        }
        if (!(k->scope == SCOPE_ALERT && !v && k->def == 0))
        {
            if (n < k->min)
                n = k->min;
            if (n > k->max)
                n = k->max;
        }
        next.values[i] = n;
    }
    if (si == SCOPE_ALERT)
    {
        int wi = -1, ci = -1, mi = -1;
        for (size_t i = 0; i < sizeof(s_config_keys) / sizeof(s_config_keys[0]); ++i)
        {
            if (!strcmp(s_config_keys[i].key, "alert.filter.warnPct"))
                wi = (int)i;
            if (!strcmp(s_config_keys[i].key, "alert.filter.criticalPct"))
                ci = (int)i;
            if (!strcmp(s_config_keys[i].key, "alert.tdsOut.maxPpm"))
                mi = (int)i;
        }
        int oi = -1;
        for (size_t i = 0; i < sizeof(s_config_keys) / sizeof(s_config_keys[0]); ++i)
            if (!strcmp(s_config_keys[i].key, "alert.tdsOut.warnPpm"))
                oi = (int)i;
        if (next.values[ci] >= next.values[wi])
        {
            next.values[ci] = 10;
            next.values[wi] = 30;
        }
        if (next.values[oi] >= next.values[mi])
        {
            next.values[oi] = 0;
            next.values[mi] = 0;
        }
    }
    next.version[si] = version;
    nvs_handle_t h;
    if (nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return false;
    esp_err_t e = nvs_set_blob(h, CONFIG_KEY, &next, sizeof(next));
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK)
        return false;
    s_config = next;
    return true;
}

static const WarningId s_warning_ids[] = {
    WARNING_ID_ER00051_INLET_WATER_QUALITY_WARNING,
    WARNING_ID_ER00052_OUTLET_WATER_QUALITY_FAULT,
    WARNING_ID_ER00053_WATER_LEAK,
    WARNING_ID_ER00054_TANK_LOW,
    WARNING_ID_ER00055_RO_PUMP_RUNTIME_OVER_60_MIN,
    WARNING_ID_ER00056_FILTER1_LIFE_70,
    WARNING_ID_ER00057_FILTER2_LIFE_70,
    WARNING_ID_ER00058_FILTER3_LIFE_70,
    WARNING_ID_ER00059_FILTER4_LIFE_70,
    WARNING_ID_ER00060_FILTER5_LIFE_70,
    WARNING_ID_ER00061_FILTER1_LIFE_90,
    WARNING_ID_ER00062_FILTER2_LIFE_90,
    WARNING_ID_ER00063_FILTER3_LIFE_90,
    WARNING_ID_ER00064_FILTER4_LIFE_90,
    WARNING_ID_ER00065_FILTER5_LIFE_90,
    WARNING_ID_ER00066_OUTPUT_WATER_QUALITY_WARNING,
    WARNING_ID_ER00067_HEATER_SENSOR_FAULT,
    WARNING_ID_ER00068_HEATER_RUNTIME_OVER_1H,
    WARNING_ID_ER00069_COOLER_SENSOR_FAULT,
    WARNING_ID_ER00070_COOLER_RUNTIME_OVER_2H,
    WARNING_ID_ER00071,
    WARNING_ID_ER00072,
    WARNING_ID_ER00073,
    WARNING_ID_ER00074,
};

static int64_t now_us(void) { return esp_timer_get_time(); }

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
    for (size_t i = 0; i < 6; ++i)
        snprintf(mac + i * 2, 3, "%02x", identity.mac[i]);
    for (size_t i = 0; i < IOT_DAO_DEVICE_ID_LEN; ++i)
        snprintf(device_id + i * 2, 3, "%02x", identity.devId[i]);
    IOT_DaoDeviceFreeIdentityInfo(&identity);
    return true;
}

static OpenAlert *find_alert(WarningId id)
{
    for (size_t i = 0; i < ALERT_EVENT_CAPACITY; ++i)
        if (s_alerts[i].used && s_alerts[i].id == id)
            return &s_alerts[i];
    return NULL;
}

static OpenAlert *allocate_alert(WarningId id)
{
    OpenAlert *slot = NULL;
    for (size_t i = 0; i < ALERT_EVENT_CAPACITY; ++i)
        if (!s_alerts[i].used)
        {
            slot = &s_alerts[i];
            break;
        }
    if (slot == NULL)
        return NULL;
    if (s_aid_prefix == 0U)
    {
        s_aid_prefix = (uint16_t)esp_random();
        if (s_aid_prefix == 0U)
            s_aid_prefix = 1U;
    }
    if (s_aid_counter == UINT16_MAX)
        return NULL;
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->id = id;
    slot->code = (uint16_t)(51U + __builtin_ctz((uint32_t)id));
    snprintf(slot->aid, sizeof(slot->aid), "%04x%04x", s_aid_prefix, ++s_aid_counter);
    slot->tier = 1U;
    slot->state_active = true;
    slot->pending = true;
    slot->pending_active = true;
    slot->next_send_us = now_us();
    return slot;
}

static iot_err_t publish_alert(const OpenAlert *alert, bool active, uint8_t tier)
{
    char mac[13] = {0}, device_id[25] = {0};
    if (!device_identity(mac, device_id))
        return IOT_ERR_DATA_NOT_FOUND;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
        return IOT_ERR_NO_MEM;
    cJSON_AddNumberToObject(root, "v", 2);
    cJSON_AddStringToObject(root, "mac", mac);
    cJSON_AddStringToObject(root, "deviceId", device_id);
    cJSON_AddStringToObject(root, "aid", alert->aid);
    char code[8];
    snprintf(code, sizeof(code), "ER%05u", alert->code);
    cJSON_AddStringToObject(root, "code", code);
    cJSON_AddStringToObject(root, "state", active ? "active" : "cleared");
    cJSON_AddNumberToObject(root, "tier", tier);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL)
        return IOT_ERR_NO_MEM;
    size_t len = strlen(json);
    iot_err_t err = len <= 2048U ? IOT_CoreServiceMsgPublish("alerts", json, len) : IOT_ERR_INVALID_SIZE;
    cJSON_free(json);
    return err;
}

void ReportAlertPoll(void)
{
    config_load();
    if (!IOT_CoreIsCloudConnected())
        return;
    int64_t now = now_us();
    if (now - s_last_publish_us < ALERT_GLOBAL_GAP_US)
        return;
    for (size_t i = 0; i < ALERT_EVENT_CAPACITY; ++i)
    {
        OpenAlert *a = &s_alerts[i];
        if (!a->used || !a->pending || now < a->next_send_us)
            continue;
        if (publish_alert(a, a->pending_active, a->tier) != IOT_OK)
            return;
        s_last_publish_us = now;
        ++a->sends_in_tier;
        uint32_t max_sends = a->tier == 1U ? (uint32_t)s_config.values[1] : (a->tier == 2U ? (uint32_t)s_config.values[3] : UINT32_MAX);
        // WARNING (B6): config values are already in ms. "* 1000U" here turns them into us, then
        // next_send_us adds "* 1000LL" again -> retries every 4.2 h / 16.7 h / ~473 h instead of
        // 15 s / 60 s / 100 min. tier 3 (6000000 * 1000) also overflows uint32_t.
        // done
        uint32_t interval_ms = a->tier == 1U ? (uint32_t)s_config.values[0] : (a->tier == 2U ? (uint32_t)s_config.values[2] : (uint32_t)s_config.values[4]);
        if (a->tier < 3U && a->sends_in_tier >= max_sends)
        {
            ++a->tier;
            a->sends_in_tier = 0U;
        }
        a->next_send_us = now + (int64_t)interval_ms * 1000LL;
        return;
    }
}

void ReportAlertHandleServiceEvent(IOT_CoreServiceEvent_t *event)
{
    if (event == NULL || event->json == NULL || event->jsonLen == 0U)
        return;
    cJSON *root = cJSON_Parse(event->json);
    if (root == NULL)
        return;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *aid = cJSON_GetObjectItemCaseSensitive(root, "aid");
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "config.set") == 0)
    {
        event->result = config_apply(root) ? IOT_OK : IOT_ERR_INVALID_ARG;
    }
    else if (cJSON_IsString(type) && strcmp(type->valuestring, "alert.ack") == 0 &&
             cJSON_IsString(aid) && cJSON_IsString(code) && cJSON_IsString(state))
    {
        for (size_t i = 0; i < ALERT_EVENT_CAPACITY; ++i)
        {
            OpenAlert *a = &s_alerts[i];
            char expected[8];
            snprintf(expected, sizeof(expected), "ER%05u", a->code);
            if (a->used && a->pending && strcmp(a->aid, aid->valuestring) == 0 &&
                strcmp(expected, code->valuestring) == 0 &&
                strcmp(a->pending_active ? "active" : "cleared", state->valuestring) == 0)
            {
                if (a->pending_active)
                {
                    a->pending = false;
                }
                else
                    memset(a, 0, sizeof(*a));
                event->result = IOT_OK;
                break;
            }
        }
    }
    cJSON_Delete(root);
}

/* Hook called by WarningManager on every warning-bit transition. */// Override
void WarningManagerOnChanged(UI32 active_mask, UI32 changed_mask)
{
    for (size_t i = 0; i < sizeof(s_warning_ids) / sizeof(s_warning_ids[0]); ++i)
    {
        WarningId id = s_warning_ids[i];
        if (((UI32)id & changed_mask) == 0U)
            continue;
        bool active = ((UI32)id & active_mask) != 0U;
        OpenAlert *alert = find_alert(id);
        if (active && alert == NULL)
            alert = allocate_alert(id);
        if (alert == NULL)
            continue;
        alert->state_active = active;
        alert->pending = true;
        alert->pending_active = active;
        alert->tier = 1U;
        alert->sends_in_tier = 0U;
        alert->next_send_us = now_us();
        /* A clear supersedes any unsent active notification for this same aid. */
    }
}

// WARNING (B8): nothing calls ReportAlertPublishReport(), so the "report" message (after connect
// + every report.intervalMs, see report_alert.md) is never sent. Also the full state built here is
// likely larger than IOT_SERVICE_MSG_PAYLOAD_MAX (1024) -> IOT_ERR_INVALID_SIZE.
// // NOTE: This function is already called in CloudSyncTick().
// done
iot_err_t ReportAlertPublishReport(void)
{
    char mac[13] = {0}, device_id[25] = {0};
    if (!device_identity(mac, device_id))
        return IOT_ERR_DATA_NOT_FOUND;
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
    config_load();
    cJSON *extra = cJSON_CreateObject();
    if (extra == NULL)
    {
        cJSON_Delete(root);
        cJSON_Delete(state);
        cJSON_Delete(alerts);
        return IOT_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(extra, "configVersion", s_config.version[SCOPE_CORE]);
    cJSON_AddNumberToObject(extra, "cfg.state", s_config.version[SCOPE_STATE]);
    cJSON_AddNumberToObject(extra, "cfg.alert", s_config.version[SCOPE_ALERT]);
    cJSON_AddNumberToObject(extra, "cfg.general", s_config.version[SCOPE_GENERAL]);

    UI8 element_count = DeviceElementGetCount();
    DeviceElementInfo element_snapshot[MAX_NUM_ELEMENT];
    DeviceElementAttributeInfo attribute_snapshot[MAX_NUM_ELEMENT][MAX_NUM_ATTRIBUTE];
    DeviceElementGetSnapshot(element_snapshot, attribute_snapshot);
    for (UI8 i = 0; i < element_count; ++i)
    {
        const DeviceElementInfo *element = &element_snapshot[i];
        if (element == NULL)
            continue;
        char element_key[6];
        snprintf(element_key, sizeof(element_key), "%u", element->id);
        cJSON *element_state = cJSON_CreateObject();
        if (element_state == NULL)
        {
            cJSON_Delete(root);
            cJSON_Delete(state);
            cJSON_Delete(alerts);
            return IOT_ERR_NO_MEM;
        }
        for (UI8 j = 0; j < element->attribute_count; ++j)
        {
            const DeviceElementAttributeInfo *attr = &element->attributes[j];
            if (!attr->has_value)
                continue;
            char attr_key[6];
            snprintf(attr_key, sizeof(attr_key), "%u", attr->id);
            cJSON *values = cJSON_CreateArray();
            if (values == NULL)
            {
                cJSON_Delete(element_state);
                cJSON_Delete(root);
                cJSON_Delete(state);
                cJSON_Delete(alerts);
                return IOT_ERR_NO_MEM;
            }
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
        if (!alert->used || !alert->state_active)
            continue;
        cJSON *entry = cJSON_CreateObject();
        if (entry == NULL)
        {
            cJSON_Delete(root);
            cJSON_Delete(state);
            cJSON_Delete(alerts);
            return IOT_ERR_NO_MEM;
        }
        char code[8];
        snprintf(code, sizeof(code), "ER%05u", alert->code);
        cJSON_AddStringToObject(entry, "code", code);
        cJSON_AddStringToObject(entry, "aid", alert->aid);
        cJSON_AddItemToArray(alerts, entry);
    }
    cJSON_AddItemToObject(root, "state", state);
    cJSON_AddItemToObject(root, "extra", extra);
    cJSON_AddItemToObject(root, "alerts", alerts);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL)
        return IOT_ERR_NO_MEM;
    size_t len = strlen(json);
    iot_err_t err = !IOT_CoreIsCloudConnected() ? IOT_ERR_NO_NETWORK_CONNECT : (len <= REPORT_JSON_CAPACITY ? IOT_CoreServiceMsgPublish("report", json, len) : IOT_ERR_INVALID_SIZE);
    cJSON_free(json);
    return err;
}

/* Report period from the cloud config (report.intervalMs is index 5 in
 * s_config_keys). config_load() clamps it to 60 s .. 7 days. */
UI32 ReportAlertGetReportIntervalMs(void)
{
    config_load();
    return (UI32)s_config.values[5];
}

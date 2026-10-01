#include "sdkconfig.h"
#ifdef CONFIG_BT_BLUEDROID_ENABLED

/* BLE - Bluedroid Implementation */
#include "ble/IOT_BleMgmt.h"
#include "ble/IOT_BleDriver.h"
#include "ble/IOT_BleAdvBuild.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "IOT_Log.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "IOT_Common.h"

static const char *TAG = "BLE_BD";

/* ============================
 *  INTERNAL STRUCTURES
 * ============================ */

typedef struct
{
    uint8_t descriptor_index;
    uint8_t uuid_len;
    uint8_t *uuid;
    uint16_t handle;
    IOT_BleMgmtGattAttrAccessCb_t *access_cb;
    void *arg;
} iot_gatt_descriptor_access_diff_t;

typedef struct
{
    uint8_t characteristic_index;
    uint8_t uuid_len;
    uint8_t *uuid;
    uint16_t def_handle;
    uint16_t val_handle;
    uint8_t numOfDescriptors;
    iot_gatt_descriptor_access_diff_t *descriptors;
    IOT_BleMgmtGattAttrAccessCb_t *access_cb;
    void *arg;
    IOT_BleMgmtGattCharFlags_t flags;
} iot_gatt_characteristic_access_diff_t;

typedef struct
{
    uint8_t service_index;
    uint8_t *uuid;
    uint8_t uuid_len;
    uint16_t service_handle;
    uint8_t numOfCharacteristics;
    iot_gatt_characteristic_access_diff_t *characteristics;
} iot_gatt_service_access_diff_t;

typedef struct
{
    IOT_BleEventCb_t cb;
    void *arg;
    bool isRegistered;
} ble_cb_mgmt_t;

/* ============================
 *  GLOBAL STATE
 * ============================ */

static ble_cb_mgmt_t *callbackMgmt = NULL;
static uint8_t numberOfGattServices = 0;
static iot_gatt_service_access_diff_t *gatt_svc_access_diff = NULL;
static uint16_t currConnectionHandle = 0xFFFF;

/* Bluedroid GATTS app */
#define BLUEDROID_APP_ID 0
static esp_gatt_if_t gatts_if_global = ESP_GATT_IF_NONE;

/* Async service registration state machine */
typedef enum
{
    SVC_REG_IDLE = 0,
    SVC_REG_CREATING_SERVICE,
    SVC_REG_STARTING_SERVICE,
    SVC_REG_ADDING_CHARS,
    SVC_REG_ADDING_DESCS,
    SVC_REG_COMPLETE
} svc_reg_state_t;

static volatile svc_reg_state_t svc_reg_state = SVC_REG_IDLE;
static SemaphoreHandle_t svc_reg_semaphore = NULL;

/* Current indices during async registration */
static size_t reg_svc_idx = 0;
static size_t reg_char_idx = 0;
static size_t reg_desc_idx = 0;

/* Advertising params stored for start after adv data set.
 * With a scan response there are two async sets in flight (adv data + scan rsp);
 * advertising may only start once BOTH have completed, so the pending state is a
 * bitmask rather than a single flag. */
#define ADV_PENDING_ADV_DATA  0x01
#define ADV_PENDING_SCAN_RSP  0x02

static esp_ble_adv_params_t pending_adv_params;
static uint8_t adv_pending_mask = 0;

/**
 * @brief Clear one pending adv-data set and start advertising once none remain
 */
static void advPendingComplete(uint8_t which)
{
    if ((adv_pending_mask & which) == 0)
    {
        return;
    }

    adv_pending_mask &= (uint8_t) ~which;
    if (adv_pending_mask == 0)
    {
        esp_ble_gap_start_advertising(&pending_adv_params);
    }
}

/**
 * @brief Configure raw adv data, and a scan response when the caller supplied a name
 *
 * @param adv        Advertising data to publish
 * @param allowScanRsp true for scannable advertising types; a non-connectable
 *                     beacon cannot answer scan requests, so a name is dropped
 *
 * @return IOT_OK when the sets were accepted; advertising starts from the GAP callback
 */
static iot_err_t configureAdvPayloads(IOT_BleMgmtAdvData_t *adv, bool allowScanRsp)
{
    uint8_t raw[IOT_BLE_ADV_MAX_LEN];
    uint8_t rawLen = 0;

    iot_err_t err = IOT_BleAdvBuildPayload(adv, raw, sizeof(raw), &rawLen);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Adv data exceeds %d bytes", IOT_BLE_ADV_MAX_LEN);
        return err;
    }

    uint8_t rsp[IOT_BLE_ADV_MAX_LEN];
    uint8_t rspLen = 0;

    if (allowScanRsp)
    {
        err = IOT_BleAdvBuildScanRsp(adv, rsp, sizeof(rsp), &rspLen);
        if (err != IOT_OK)
        {
            IOT_LOGE(TAG, "Scan response exceeds %d bytes", IOT_BLE_ADV_MAX_LEN);
            return err;
        }
    }
    else if (adv->name != NULL && adv->nameLen > 0)
    {
        IOT_LOGW(TAG, "Ignoring adv name: this advertising type cannot answer scan requests");
    }

    adv_pending_mask = ADV_PENDING_ADV_DATA;
    if (rspLen > 0)
    {
        adv_pending_mask |= ADV_PENDING_SCAN_RSP;

        esp_err_t ret = esp_ble_gap_config_scan_rsp_data_raw(rsp, rspLen);
        if (ret != ESP_OK)
        {
            IOT_LOGE(TAG, "Config scan rsp data failed: %s", esp_err_to_name(ret));
            adv_pending_mask = 0;
            return IOT_ERR_FAIL;
        }
    }

    esp_err_t ret = esp_ble_gap_config_adv_data_raw(raw, rawLen);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "Config adv data failed: %s", esp_err_to_name(ret));
        adv_pending_mask = 0;
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

/* ============================
 *  HELPER FUNCTIONS
 * ============================ */

static esp_bt_uuid_t convertToBluedroidUUID(const uint8_t *uuid, uint8_t uuid_len)
{
    esp_bt_uuid_t bt_uuid;
    memset(&bt_uuid, 0, sizeof(bt_uuid));

    if (uuid_len == 2)
    {
        bt_uuid.len = ESP_UUID_LEN_16;
        bt_uuid.uuid.uuid16 = ((uint16_t)uuid[0] << 8) | uuid[1];
    }
    else if (uuid_len == 4)
    {
        bt_uuid.len = ESP_UUID_LEN_32;
        bt_uuid.uuid.uuid32 = ((uint32_t)uuid[0] << 24) | ((uint32_t)uuid[1] << 16) |
                               ((uint32_t)uuid[2] << 8) | uuid[3];
    }
    else if (uuid_len == 16)
    {
        bt_uuid.len = ESP_UUID_LEN_128;
        /* Bluedroid expects 128-bit UUID in little-endian byte order */
        for (int i = 0; i < 16; i++)
        {
            bt_uuid.uuid.uuid128[i] = uuid[15 - i];
        }
    }
    return bt_uuid;
}

static esp_gatt_char_prop_t mapFlagsToProperty(IOT_BleMgmtGattCharFlags_t flags)
{
    esp_gatt_char_prop_t prop = 0;
    if (flags & IOT_BLE_GATT_CHR_F_BROADCAST)
        prop |= ESP_GATT_CHAR_PROP_BIT_BROADCAST;
    if (flags & IOT_BLE_GATT_CHR_F_READ)
        prop |= ESP_GATT_CHAR_PROP_BIT_READ;
    if (flags & IOT_BLE_GATT_CHR_F_WRITE_NO_RSP)
        prop |= ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
    if (flags & IOT_BLE_GATT_CHR_F_WRITE)
        prop |= ESP_GATT_CHAR_PROP_BIT_WRITE;
    if (flags & IOT_BLE_GATT_CHR_F_NOTIFY)
        prop |= ESP_GATT_CHAR_PROP_BIT_NOTIFY;
    if (flags & IOT_BLE_GATT_CHR_F_INDICATE)
        prop |= ESP_GATT_CHAR_PROP_BIT_INDICATE;
    if (flags & IOT_BLE_GATT_CHR_F_AUTH_SIGN_WRITE)
        prop |= ESP_GATT_CHAR_PROP_BIT_AUTH;
    return prop;
}

static esp_gatt_perm_t mapFlagsToPermission(IOT_BleMgmtGattCharFlags_t flags)
{
    esp_gatt_perm_t perm = 0;
    if (flags & IOT_BLE_GATT_CHR_F_READ)
        perm |= ESP_GATT_PERM_READ;
    if (flags & IOT_BLE_GATT_CHR_F_WRITE)
        perm |= ESP_GATT_PERM_WRITE;
    if (flags & IOT_BLE_GATT_CHR_F_WRITE_NO_RSP)
        perm |= ESP_GATT_PERM_WRITE;
    if (flags & IOT_BLE_GATT_CHR_F_READ_ENC)
        perm |= ESP_GATT_PERM_READ_ENCRYPTED;
    if (flags & IOT_BLE_GATT_CHR_F_WRITE_ENC)
        perm |= ESP_GATT_PERM_WRITE_ENCRYPTED;
    if (flags & IOT_BLE_GATT_CHR_F_READ_AUTHEN)
        perm |= ESP_GATT_PERM_READ_ENC_MITM;
    if (flags & IOT_BLE_GATT_CHR_F_WRITE_AUTHEN)
        perm |= ESP_GATT_PERM_WRITE_ENC_MITM;
    return perm;
}

static iot_err_t getAttrIndex(uint16_t attr_handle, uint8_t numberOfService,
                              uint8_t *svc_idx, uint8_t *char_idx, uint8_t *desc_idx)
{
    if (gatt_svc_access_diff == NULL || numberOfService == 0)
    {
        return IOT_ERR_NOT_FOUND;
    }

    for (uint8_t i = 0; i < numberOfService; i++)
    {
        if (gatt_svc_access_diff[i].service_handle == attr_handle)
        {
            *svc_idx = i;
            *char_idx = 0xFF;
            *desc_idx = 0xFF;
            return IOT_OK;
        }
        for (uint8_t j = 0; j < gatt_svc_access_diff[i].numOfCharacteristics; j++)
        {
            if (gatt_svc_access_diff[i].characteristics[j].def_handle == attr_handle ||
                gatt_svc_access_diff[i].characteristics[j].val_handle == attr_handle)
            {
                *svc_idx = i;
                *char_idx = j;
                *desc_idx = 0xFF;
                return IOT_OK;
            }
            for (uint8_t k = 0; k < gatt_svc_access_diff[i].characteristics[j].numOfDescriptors; k++)
            {
                if (gatt_svc_access_diff[i].characteristics[j].descriptors[k].handle == attr_handle)
                {
                    *svc_idx = i;
                    *char_idx = j;
                    *desc_idx = k;
                    return IOT_OK;
                }
            }
        }
    }
    return IOT_ERR_NOT_FOUND;
}

static iot_err_t searchAttrhandleByUuid(const uint8_t *charUuid, uint16_t *char_handle)
{
    if (charUuid == NULL || char_handle == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    for (int i = 0; i < numberOfGattServices; i++)
    {
        for (int j = 0; j < gatt_svc_access_diff[i].numOfCharacteristics; j++)
        {
            if (memcmp(gatt_svc_access_diff[i].characteristics[j].uuid,
                       charUuid,
                       gatt_svc_access_diff[i].characteristics[j].uuid_len) == 0)
            {
                *char_handle = gatt_svc_access_diff[i].characteristics[j].val_handle;
                return IOT_OK;
            }
        }
    }
    return IOT_ERR_NOT_FOUND;
}

/* ============================
 *  ASYNC SERVICE REGISTRATION
 * ============================ */

static void addNextCharacteristic(void);
static void addNextDescriptor(void);

static void addNextCharacteristic(void)
{
    iot_gatt_service_access_diff_t *svc = &gatt_svc_access_diff[reg_svc_idx];
    if (reg_char_idx >= svc->numOfCharacteristics)
    {
        /* All chars added for this service — done */
        svc_reg_state = SVC_REG_COMPLETE;
        if (svc_reg_semaphore)
            xSemaphoreGive(svc_reg_semaphore);
        return;
    }

    iot_gatt_characteristic_access_diff_t *chr = &svc->characteristics[reg_char_idx];
    esp_bt_uuid_t char_uuid = convertToBluedroidUUID(chr->uuid, chr->uuid_len);
    esp_gatt_char_prop_t property = mapFlagsToProperty(chr->flags);
    esp_gatt_perm_t perm = mapFlagsToPermission(chr->flags);

    esp_err_t ret = esp_ble_gatts_add_char(svc->service_handle, &char_uuid, perm, property, NULL, NULL);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to add char %d: %s", (int)reg_char_idx, esp_err_to_name(ret));
        svc_reg_state = SVC_REG_COMPLETE;
        if (svc_reg_semaphore)
            xSemaphoreGive(svc_reg_semaphore);
    }
}

static void addNextDescriptor(void)
{
    iot_gatt_service_access_diff_t *svc = &gatt_svc_access_diff[reg_svc_idx];
    iot_gatt_characteristic_access_diff_t *chr = &svc->characteristics[reg_char_idx];
    iot_gatt_descriptor_access_diff_t *dsc = &chr->descriptors[reg_desc_idx];

    esp_bt_uuid_t desc_uuid = convertToBluedroidUUID(dsc->uuid, dsc->uuid_len);
    esp_gatt_perm_t perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE;

    esp_err_t ret = esp_ble_gatts_add_char_descr(svc->service_handle, &desc_uuid, perm, NULL, NULL);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to add desc %d: %s", (int)reg_desc_idx, esp_err_to_name(ret));
        /* Move to next char */
        reg_char_idx++;
        svc_reg_state = SVC_REG_ADDING_CHARS;
        addNextCharacteristic();
    }
}

/* ============================
 *  READ/WRITE EVENT HANDLERS
 * ============================ */

static void handleReadEvent(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    uint8_t svc_idx, char_idx, desc_idx;
    if (getAttrIndex(param->read.handle, numberOfGattServices, &svc_idx, &char_idx, &desc_idx) != IOT_OK)
    {
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_NOT_FOUND, NULL);
        return;
    }

    if (char_idx == 0xFF)
    {
        /* Service handle read — not typical, send empty response */
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, NULL);
        return;
    }

    iot_gatt_characteristic_access_diff_t *chr = &gatt_svc_access_diff[svc_idx].characteristics[char_idx];
    if (chr->access_cb == NULL)
    {
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_READ_NOT_PERMIT, NULL);
        return;
    }

    /* Build platform context */
    struct IOT_BleMgmtGattAccessContext_t *convertCtxt =
        Mem_SafeMalloc(sizeof(struct IOT_BleMgmtGattAccessContext_t), TAG, "access context");
    if (convertCtxt == NULL)
    {
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_ERROR, NULL);
        return;
    }
    memset(convertCtxt, 0, sizeof(*convertCtxt));

    if (desc_idx != 0xFF)
    {
        convertCtxt->op = IOT_BLE_GATT_ACCESS_OP_READ_DSC;
    }
    else
    {
        convertCtxt->op = IOT_BLE_GATT_ACCESS_OP_READ_CHR;
    }

    /* Build characteristic info */
    IOT_BleMgmtGattChar_t *charInfo = Mem_SafeMalloc(sizeof(IOT_BleMgmtGattChar_t), TAG, "char context");
    if (charInfo == NULL)
    {
        SAFE_FREE(convertCtxt);
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_ERROR, NULL);
        return;
    }
    memset(charInfo, 0, sizeof(*charInfo));
    charInfo->flags = chr->flags;
    charInfo->uuid_len = chr->uuid_len;
    charInfo->uuid = Mem_SafeMalloc(chr->uuid_len, TAG, "char UUID");
    if (charInfo->uuid == NULL)
    {
        SAFE_FREE(charInfo);
        SAFE_FREE(convertCtxt);
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_ERROR, NULL);
        return;
    }
    memcpy(charInfo->uuid, chr->uuid, chr->uuid_len);
    convertCtxt->characteristic = charInfo;

    /* Allocate om buffer for callback to fill */
    uint_data_t *om = Mem_SafeMalloc(sizeof(uint_data_t), TAG, "operation buffer");
    if (om == NULL)
    {
        SAFE_FREE(charInfo->uuid);
        SAFE_FREE(charInfo);
        SAFE_FREE(convertCtxt);
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_ERROR, NULL);
        return;
    }
    om->data = Mem_SafeMalloc(512, TAG, "read response buf");
    om->len = 0;
    if (om->data == NULL)
    {
        SAFE_FREE(om);
        SAFE_FREE(charInfo->uuid);
        SAFE_FREE(charInfo);
        SAFE_FREE(convertCtxt);
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_ERROR, NULL);
        return;
    }
    convertCtxt->om = om;

    int rc = chr->access_cb(convertCtxt, chr->arg);

    /* Send response */
    if (rc == 0 && om->data != NULL && om->len > 0)
    {
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(rsp));
        rsp.attr_value.handle = param->read.handle;
        uint16_t copy_len = om->len;
        if (copy_len > sizeof(rsp.attr_value.value))
            copy_len = sizeof(rsp.attr_value.value);
        rsp.attr_value.len = copy_len;
        memcpy(rsp.attr_value.value, om->data, copy_len);
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
    }
    else
    {
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_ERROR, NULL);
    }

    SAFE_FREE(om->data);
    SAFE_FREE(om);
    SAFE_FREE(charInfo->uuid);
    SAFE_FREE(charInfo);
    SAFE_FREE(convertCtxt);
}

static void handleWriteEvent(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    uint8_t svc_idx, char_idx, desc_idx;
    if (getAttrIndex(param->write.handle, numberOfGattServices, &svc_idx, &char_idx, &desc_idx) != IOT_OK)
    {
        if (param->write.need_rsp)
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_NOT_FOUND, NULL);
        return;
    }

    if (char_idx == 0xFF)
    {
        if (param->write.need_rsp)
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_OK, NULL);
        return;
    }

    iot_gatt_characteristic_access_diff_t *chr = &gatt_svc_access_diff[svc_idx].characteristics[char_idx];

    /* Check for CCCD write (notification enable/disable) */
    if (desc_idx != 0xFF)
    {
        if (param->write.len == 2)
        {
            uint16_t cccd_val = param->write.value[0] | ((uint16_t)param->write.value[1] << 8);
            if (callbackMgmt && callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_ENABLE_STATUS].isRegistered)
            {
                IOT_BleMgmtEventNotifyIndicateEnable_t sub_info;
                memset(&sub_info, 0, sizeof(sub_info));
                sub_info.success = (cccd_val & 0x0001) ? true : false;
                callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_ENABLE_STATUS].cb(
                    callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_ENABLE_STATUS].arg,
                    BLE_MGMT_EVENT_STATUS_TYPE_ENABLE_STATUS, &sub_info);
            }
        }

        /* Also forward to descriptor access callback if registered */
        if (chr->numOfDescriptors > desc_idx && chr->descriptors[desc_idx].access_cb != NULL)
        {
            struct IOT_BleMgmtGattAccessContext_t ctxt;
            memset(&ctxt, 0, sizeof(ctxt));
            ctxt.op = IOT_BLE_GATT_ACCESS_OP_WRITE_DSC;

            uint_data_t om_data = {
                .data = param->write.value,
                .len = param->write.len};
            ctxt.om = &om_data;

            chr->descriptors[desc_idx].access_cb(&ctxt, chr->descriptors[desc_idx].arg);
        }

        if (param->write.need_rsp)
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_OK, NULL);
        return;
    }

    if (chr->access_cb == NULL)
    {
        if (param->write.need_rsp)
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_WRITE_NOT_PERMIT, NULL);
        return;
    }

    /* Build platform context with write data */
    struct IOT_BleMgmtGattAccessContext_t *convertCtxt =
        Mem_SafeMalloc(sizeof(struct IOT_BleMgmtGattAccessContext_t), TAG, "write context");
    if (convertCtxt == NULL)
    {
        if (param->write.need_rsp)
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_ERROR, NULL);
        return;
    }
    memset(convertCtxt, 0, sizeof(*convertCtxt));
    convertCtxt->op = IOT_BLE_GATT_ACCESS_OP_WRITE_CHR;

    uint_data_t *om = Mem_SafeMalloc(sizeof(uint_data_t), TAG, "write op buffer");
    if (om == NULL)
    {
        SAFE_FREE(convertCtxt);
        if (param->write.need_rsp)
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_ERROR, NULL);
        return;
    }
    om->data = Mem_SafeMalloc(param->write.len, TAG, "write data");
    if (om->data == NULL)
    {
        SAFE_FREE(om);
        SAFE_FREE(convertCtxt);
        if (param->write.need_rsp)
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_ERROR, NULL);
        return;
    }
    om->len = param->write.len;
    memcpy(om->data, param->write.value, param->write.len);
    convertCtxt->om = om;

    IOT_BleMgmtGattChar_t *charInfo = Mem_SafeMalloc(sizeof(IOT_BleMgmtGattChar_t), TAG, "write char info");
    if (charInfo == NULL)
    {
        SAFE_FREE(om->data);
        SAFE_FREE(om);
        SAFE_FREE(convertCtxt);
        if (param->write.need_rsp)
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_ERROR, NULL);
        return;
    }
    memset(charInfo, 0, sizeof(*charInfo));
    charInfo->flags = chr->flags;
    charInfo->uuid_len = chr->uuid_len;
    charInfo->uuid = Mem_SafeMalloc(chr->uuid_len, TAG, "write char UUID");
    if (charInfo->uuid == NULL)
    {
        SAFE_FREE(charInfo);
        SAFE_FREE(om->data);
        SAFE_FREE(om);
        SAFE_FREE(convertCtxt);
        if (param->write.need_rsp)
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_ERROR, NULL);
        return;
    }
    memcpy(charInfo->uuid, chr->uuid, chr->uuid_len);
    convertCtxt->characteristic = charInfo;

    int rc = chr->access_cb(convertCtxt, chr->arg);

    if (param->write.need_rsp)
    {
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                    (rc == 0) ? ESP_GATT_OK : ESP_GATT_ERROR, NULL);
    }

    SAFE_FREE(om->data);
    SAFE_FREE(om);
    SAFE_FREE(charInfo->uuid);
    SAFE_FREE(charInfo);
    SAFE_FREE(convertCtxt);
}

/* ============================
 *  GATTS EVENT HANDLER
 * ============================ */

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            gatts_if_global = gatts_if;
            IOT_LOGI(TAG, "GATTS app registered, if=%d", gatts_if);
            if (callbackMgmt && callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE].isRegistered)
            {
                callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE].cb(
                    callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE].arg,
                    BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE, NULL);
            }
        }
        else
        {
            IOT_LOGE(TAG, "GATTS app register failed, status %d", param->reg.status);
        }
        break;
    }

    case ESP_GATTS_CREATE_EVT:
    {
        if (param->create.status == ESP_GATT_OK && svc_reg_state == SVC_REG_CREATING_SERVICE)
        {
            gatt_svc_access_diff[reg_svc_idx].service_handle = param->create.service_handle;
            IOT_LOGI(TAG, "Service created, handle=%d", param->create.service_handle);
            /* Start the service */
            esp_ble_gatts_start_service(param->create.service_handle);
            svc_reg_state = SVC_REG_STARTING_SERVICE;
        }
        else if (param->create.status != ESP_GATT_OK)
        {
            IOT_LOGE(TAG, "Service create failed, status %d", param->create.status);
            svc_reg_state = SVC_REG_COMPLETE;
            if (svc_reg_semaphore)
                xSemaphoreGive(svc_reg_semaphore);
        }
        break;
    }

    case ESP_GATTS_START_EVT:
    {
        if (param->start.status == ESP_GATT_OK && svc_reg_state == SVC_REG_STARTING_SERVICE)
        {
            IOT_LOGI(TAG, "Service started, handle=%d", param->start.service_handle);
            /* Now add characteristics one by one */
            reg_char_idx = 0;
            reg_desc_idx = 0;
            svc_reg_state = SVC_REG_ADDING_CHARS;
            addNextCharacteristic();
        }
        else if (param->start.status != ESP_GATT_OK)
        {
            IOT_LOGE(TAG, "Service start failed, status %d", param->start.status);
            svc_reg_state = SVC_REG_COMPLETE;
            if (svc_reg_semaphore)
                xSemaphoreGive(svc_reg_semaphore);
        }
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT:
    {
        if (param->add_char.status == ESP_GATT_OK && svc_reg_state == SVC_REG_ADDING_CHARS)
        {
            /* Store the handle */
            gatt_svc_access_diff[reg_svc_idx].characteristics[reg_char_idx].val_handle =
                param->add_char.attr_handle;
            IOT_LOGI(TAG, "Char added, handle=%d", param->add_char.attr_handle);

            /* If this char has descriptors, add them */
            reg_desc_idx = 0;
            if (gatt_svc_access_diff[reg_svc_idx].characteristics[reg_char_idx].numOfDescriptors > 0)
            {
                svc_reg_state = SVC_REG_ADDING_DESCS;
                addNextDescriptor();
            }
            else
            {
                /* Move to next char */
                reg_char_idx++;
                addNextCharacteristic();
            }
        }
        else if (param->add_char.status != ESP_GATT_OK)
        {
            IOT_LOGE(TAG, "Add char failed, status %d", param->add_char.status);
            reg_char_idx++;
            addNextCharacteristic();
        }
        break;
    }

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
    {
        if (param->add_char_descr.status == ESP_GATT_OK && svc_reg_state == SVC_REG_ADDING_DESCS)
        {
            gatt_svc_access_diff[reg_svc_idx]
                .characteristics[reg_char_idx]
                .descriptors[reg_desc_idx]
                .handle = param->add_char_descr.attr_handle;
            IOT_LOGI(TAG, "Desc added, handle=%d", param->add_char_descr.attr_handle);

            reg_desc_idx++;
            if (reg_desc_idx < gatt_svc_access_diff[reg_svc_idx]
                                   .characteristics[reg_char_idx]
                                   .numOfDescriptors)
            {
                addNextDescriptor();
            }
            else
            {
                /* Done with descriptors, move to next char */
                reg_char_idx++;
                svc_reg_state = SVC_REG_ADDING_CHARS;
                addNextCharacteristic();
            }
        }
        else if (param->add_char_descr.status != ESP_GATT_OK)
        {
            IOT_LOGE(TAG, "Add desc failed, status %d", param->add_char_descr.status);
            reg_char_idx++;
            svc_reg_state = SVC_REG_ADDING_CHARS;
            addNextCharacteristic();
        }
        break;
    }

    case ESP_GATTS_READ_EVT:
    {
        handleReadEvent(gatts_if, param);
        break;
    }

    case ESP_GATTS_WRITE_EVT:
    {
        handleWriteEvent(gatts_if, param);
        break;
    }

    case ESP_GATTS_CONNECT_EVT:
    {
        currConnectionHandle = param->connect.conn_id;
        IOT_LOGI(TAG, "Connected, conn_id=%d", param->connect.conn_id);
        if (callbackMgmt && callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED].isRegistered)
        {
            IOT_BleMgmtEventConnectData_t conn_info;
            memset(&conn_info, 0, sizeof(conn_info));
            conn_info.status = 0;
            callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED].cb(
                callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED].arg,
                BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED, &conn_info);
        }
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT:
    {
        IOT_LOGI(TAG, "Disconnected, reason=0x%x", param->disconnect.reason);
        if (callbackMgmt && callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT].isRegistered)
        {
            IOT_BleMgmtEventDisconnectData_t dis_info;
            memset(&dis_info, 0, sizeof(dis_info));
            dis_info.reason = param->disconnect.reason;
            callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT].cb(
                callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT].arg,
                BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT, &dis_info);
        }
        currConnectionHandle = 0xFFFF;
        break;
    }

    case ESP_GATTS_CONF_EVT:
    {
        if (callbackMgmt && callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_SEND_NOTIFICATION].isRegistered)
        {
            IOT_BleMgmtNotifySendData_t notif;
            memset(&notif, 0, sizeof(notif));
            notif.success = (param->conf.status == ESP_GATT_OK);
            callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_SEND_NOTIFICATION].cb(
                callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_SEND_NOTIFICATION].arg,
                BLE_MGMT_EVENT_STATUS_TYPE_SEND_NOTIFICATION, &notif);
        }
        break;
    }

    case ESP_GATTS_MTU_EVT:
    {
        IOT_LOGI(TAG, "MTU exchange, MTU=%d", param->mtu.mtu);
        break;
    }

    default:
        IOT_LOGD(TAG, "Unhandled GATTS event: %d", event);
        break;
    }
}

/* ============================
 *  GAP EVENT HANDLER
 * ============================ */

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    {
        advPendingComplete(ADV_PENDING_ADV_DATA);
        break;
    }

    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
    {
        advPendingComplete(ADV_PENDING_ADV_DATA);
        break;
    }

    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
    {
        advPendingComplete(ADV_PENDING_SCAN_RSP);
        break;
    }

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    {
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            IOT_LOGE(TAG, "Advertising start failed, status %d", param->adv_start_cmpl.status);
        }
        else
        {
            IOT_LOGI(TAG, "Advertising started");
        }
        break;
    }

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
    {
        if (callbackMgmt && callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_ADV_STOP].isRegistered)
        {
            callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_ADV_STOP].cb(
                callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_ADV_STOP].arg,
                BLE_MGMT_EVENT_STATUS_TYPE_ADV_STOP, NULL);
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
    {
        /* Scan params set, now start scanning — duration stored in param */
        break;
    }

    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT)
        {
            if (callbackMgmt && callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT].isRegistered)
            {
                IOT_BleMgmtEventScanResultData_t result;
                memset(&result, 0, sizeof(result));
                memcpy(result.addr, param->scan_rst.bda, 6);
                result.rssi = param->scan_rst.rssi;
                result.addr_type = param->scan_rst.ble_addr_type;
                result.data_len = param->scan_rst.adv_data_len;
                if (result.data_len > 0)
                {
                    result.data = Mem_SafeMalloc(result.data_len, TAG, "scan data");
                    if (result.data != NULL)
                    {
                        memcpy(result.data, param->scan_rst.ble_adv, result.data_len);
                    }
                    else
                    {
                        result.data_len = 0;
                    }
                }
                callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT].cb(
                    callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT].arg,
                    BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT, &result);
            }
        }
        else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT)
        {
            if (callbackMgmt && callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE].isRegistered)
            {
                callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE].cb(
                    callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE].arg,
                    BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE, NULL);
            }
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
    {
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            IOT_LOGE(TAG, "Scan start failed, status %d", param->scan_start_cmpl.status);
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        break;

    default:
        break;
    }
}

/* ============================
 *  PUBLIC API IMPLEMENTATION
 * ============================ */

static iot_err_t bluedroid_init(void)
{
    /* Allocate callback management */
    callbackMgmt = Mem_SafeMalloc(BLE_MGMT_EVENT_STATUS_TYPE_MAX * sizeof(ble_cb_mgmt_t), TAG, "callback mgmt");
    if (callbackMgmt == NULL)
    {
        return IOT_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < BLE_MGMT_EVENT_STATUS_TYPE_MAX; i++)
    {
        callbackMgmt[i].isRegistered = false;
        callbackMgmt[i].cb = NULL;
        callbackMgmt[i].arg = NULL;
    }

    /* Create semaphore for service registration sync */
    svc_reg_semaphore = xSemaphoreCreateBinary();
    if (svc_reg_semaphore == NULL)
    {
        SAFE_FREE(callbackMgmt);
        return IOT_ERR_NO_MEM;
    }

    /* Release BT classic memory (BLE only) */
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK)
    {
        IOT_LOGW(TAG, "BT classic mem release: %s (may be normal on BLE-only chips)", esp_err_to_name(ret));
    }

    /* Init BT controller */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* Init Bluedroid */
    ret = esp_bluedroid_init();
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* Register callbacks */
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "GATTS register callback failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* Register GATTS app — triggers ESP_GATTS_REG_EVT */
    ret = esp_ble_gatts_app_register(BLUEDROID_APP_ID);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* Set local MTU */
    esp_ble_gatt_set_local_mtu(256);

    IOT_LOGI(TAG, "BLE Bluedroid initialized");
    return IOT_OK;

fail:
    SAFE_FREE(callbackMgmt);
    if (svc_reg_semaphore)
    {
        vSemaphoreDelete(svc_reg_semaphore);
        svc_reg_semaphore = NULL;
    }
    return IOT_ERR_FAIL;
}

static iot_err_t bluedroid_deinit(void)
{
    if (gatts_if_global != ESP_GATT_IF_NONE)
    {
        esp_ble_gatts_app_unregister(gatts_if_global);
        gatts_if_global = ESP_GATT_IF_NONE;
    }
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    SAFE_FREE(callbackMgmt);
    if (svc_reg_semaphore)
    {
        vSemaphoreDelete(svc_reg_semaphore);
        svc_reg_semaphore = NULL;
    }

    numberOfGattServices = 0;
    currConnectionHandle = 0xFFFF;

    return IOT_OK;
}

static iot_err_t bluedroid_addServices(const IOT_BleMgmtGattServ_t *service)
{
    if (service == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    if (gatts_if_global == ESP_GATT_IF_NONE)
    {
        IOT_LOGE(TAG, "GATTS not ready, call IOT_BleInit first");
        return IOT_ERR_INVALID_STATE;
    }

    /* Count incoming services */
    size_t add_count = 0;
    const IOT_BleMgmtGattServ_t *svc_ptr = service;
    while (svc_ptr->type != 0 && svc_ptr->uuid != NULL)
    {
        add_count++;
        svc_ptr++;
    }
    if (add_count == 0)
    {
        return IOT_ERR_INVALID_ARG;
    }

    /* Expand gatt_svc_access_diff storage */
    size_t old_count = numberOfGattServices;
    size_t total_count = old_count + add_count;
    iot_gatt_service_access_diff_t *new_diff =
        Mem_SafeRealloc(gatt_svc_access_diff, total_count * sizeof(iot_gatt_service_access_diff_t), TAG, "gatt svc diff grow");
    if (new_diff == NULL)
    {
        return IOT_ERR_NO_MEM;
    }
    if (total_count > old_count)
    {
        memset(&new_diff[old_count], 0, (total_count - old_count) * sizeof(iot_gatt_service_access_diff_t));
    }
    gatt_svc_access_diff = new_diff;

    /* Populate metadata for each service */
    for (size_t s = 0; s < add_count; s++)
    {
        const IOT_BleMgmtGattServ_t *in_svc = &service[s];
        size_t svc_index = old_count + s;

        /* Copy service UUID */
        gatt_svc_access_diff[svc_index].service_index = (uint8_t)svc_index;
        gatt_svc_access_diff[svc_index].uuid_len = in_svc->uuid_len;
        if (in_svc->uuid_len > 0 && in_svc->uuid != NULL)
        {
            gatt_svc_access_diff[svc_index].uuid = Mem_SafeMalloc(in_svc->uuid_len, TAG, "svc uuid");
            if (gatt_svc_access_diff[svc_index].uuid == NULL)
            {
                goto fail_cleanup;
            }
            memcpy(gatt_svc_access_diff[svc_index].uuid, in_svc->uuid, in_svc->uuid_len);
        }
        else
        {
            gatt_svc_access_diff[svc_index].uuid = NULL;
        }

        /* Count characteristics */
        size_t char_count = 0;
        const IOT_BleMgmtGattChar_t *char_ptr = in_svc->characteristics;
        if (char_ptr != NULL)
        {
            while (char_ptr[char_count].uuid != NULL)
            {
                char_count++;
            }
        }

        /* Allocate characteristics metadata */
        gatt_svc_access_diff[svc_index].characteristics =
            Mem_SafeCalloc(char_count, sizeof(iot_gatt_characteristic_access_diff_t), TAG, "char access diff");
        if (gatt_svc_access_diff[svc_index].characteristics == NULL && char_count > 0)
        {
            goto fail_cleanup;
        }
        gatt_svc_access_diff[svc_index].numOfCharacteristics = (uint8_t)char_count;

        /* Populate each characteristic and descriptors */
        for (size_t ci = 0; ci < char_count; ci++)
        {
            const IOT_BleMgmtGattChar_t *in_char = &char_ptr[ci];

            /* Copy characteristic UUID */
            gatt_svc_access_diff[svc_index].characteristics[ci].uuid_len = in_char->uuid_len;
            if (in_char->uuid_len > 0 && in_char->uuid != NULL)
            {
                gatt_svc_access_diff[svc_index].characteristics[ci].uuid = Mem_SafeMalloc(in_char->uuid_len, TAG, "char uuid");
                if (gatt_svc_access_diff[svc_index].characteristics[ci].uuid == NULL)
                {
                    goto fail_cleanup;
                }
                memcpy(gatt_svc_access_diff[svc_index].characteristics[ci].uuid, in_char->uuid, in_char->uuid_len);
            }
            else
            {
                gatt_svc_access_diff[svc_index].characteristics[ci].uuid = NULL;
            }

            gatt_svc_access_diff[svc_index].characteristics[ci].characteristic_index = (uint8_t)ci;
            gatt_svc_access_diff[svc_index].characteristics[ci].arg = in_char->arg;
            gatt_svc_access_diff[svc_index].characteristics[ci].access_cb = in_char->access_cb;
            gatt_svc_access_diff[svc_index].characteristics[ci].flags = in_char->flags;

            /* Count explicit descriptors */
            size_t desc_count = 0;
            const IOT_BleMgmtGattDesc_t *desc_ptr = in_char->descriptor;
            if (desc_ptr != NULL)
            {
                while (desc_ptr[desc_count].uuid != NULL)
                    desc_count++;
            }

            /* Auto-inject CCCD for NOTIFY/INDICATE chars (Bluedroid doesn't do this automatically) */
            bool needs_cccd = (in_char->flags & (IOT_BLE_GATT_CHR_F_NOTIFY | IOT_BLE_GATT_CHR_F_INDICATE)) != 0;
            size_t total_desc_count = desc_count + (needs_cccd ? 1 : 0);

            if (total_desc_count > 0)
            {
                gatt_svc_access_diff[svc_index].characteristics[ci].numOfDescriptors = (uint8_t)total_desc_count;
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors =
                    Mem_SafeCalloc(total_desc_count, sizeof(iot_gatt_descriptor_access_diff_t), TAG, "desc access diff");
                if (gatt_svc_access_diff[svc_index].characteristics[ci].descriptors == NULL)
                {
                    goto fail_cleanup;
                }
            }
            else
            {
                gatt_svc_access_diff[svc_index].characteristics[ci].numOfDescriptors = 0;
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors = NULL;
            }

            /* Populate explicit descriptors */
            for (size_t di = 0; di < desc_count; di++)
            {
                const IOT_BleMgmtGattDesc_t *in_desc = &desc_ptr[di];

                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[di].uuid_len = in_desc->uuid_len;
                if (in_desc->uuid_len > 0 && in_desc->uuid != NULL)
                {
                    gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[di].uuid =
                        Mem_SafeMalloc(in_desc->uuid_len, TAG, "desc uuid");
                    if (gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[di].uuid == NULL)
                    {
                        goto fail_cleanup;
                    }
                    memcpy(gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[di].uuid,
                           in_desc->uuid, in_desc->uuid_len);
                }
                else
                {
                    gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[di].uuid = NULL;
                }

                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[di].descriptor_index = (uint8_t)di;
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[di].arg = in_desc->arg;
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[di].access_cb = in_desc->access_cb;
            }

            /* Append auto-CCCD descriptor (UUID 0x2902) */
            if (needs_cccd)
            {
                size_t cccd_idx = desc_count;
                static const uint8_t cccd_uuid_be[2] = {0x29, 0x02};
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[cccd_idx].uuid_len = 2;
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[cccd_idx].uuid = Mem_SafeMalloc(2, TAG, "cccd uuid");
                if (gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[cccd_idx].uuid == NULL)
                {
                    goto fail_cleanup;
                }
                memcpy(gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[cccd_idx].uuid,
                       cccd_uuid_be, 2);
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[cccd_idx].descriptor_index =
                    (uint8_t)cccd_idx;
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[cccd_idx].access_cb = NULL;
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[cccd_idx].arg = NULL;
            }
        }
    }

    numberOfGattServices = (uint8_t)total_count;

    /* Create and register services one at a time (async with semaphore sync) */
    for (size_t s = 0; s < add_count; s++)
    {
        reg_svc_idx = old_count + s;
        reg_char_idx = 0;
        reg_desc_idx = 0;

        iot_gatt_service_access_diff_t *svc = &gatt_svc_access_diff[reg_svc_idx];
        esp_bt_uuid_t svc_uuid = convertToBluedroidUUID(svc->uuid, svc->uuid_len);

        /* Calculate total handles needed:
           1 (service) + per-char: 2 (decl+val) + per-desc: 1 */
        uint16_t num_handles = 1;
        for (uint8_t ci = 0; ci < svc->numOfCharacteristics; ci++)
        {
            num_handles += 2; /* char decl + char val */
            num_handles += svc->characteristics[ci].numOfDescriptors;
        }

        svc_reg_state = SVC_REG_CREATING_SERVICE;

        esp_gatt_srvc_id_t srvc_id = {
            .is_primary = (service[s].type == IOT_GATT_SERVICE_TYPE_PRIMARY),
            .id = {
                .uuid = svc_uuid,
                .inst_id = (uint8_t)(reg_svc_idx),
            },
        };

        esp_err_t ret = esp_ble_gatts_create_service(gatts_if_global, &srvc_id, num_handles);
        if (ret != ESP_OK)
        {
            IOT_LOGE(TAG, "Create service failed: %s", esp_err_to_name(ret));
            goto fail_cleanup;
        }

        /* Block until this service is fully registered */
        if (xSemaphoreTake(svc_reg_semaphore, pdMS_TO_TICKS(5000)) != pdTRUE)
        {
            IOT_LOGE(TAG, "Service registration timeout for service %d", (int)s);
            goto fail_cleanup;
        }
    }

    IOT_LOGI(TAG, "All %d services registered successfully", (int)add_count);
    return IOT_OK;

fail_cleanup:
    /* Free persistent copies created by this call */
    for (size_t s = 0; s < add_count; s++)
    {
        size_t svc_index = old_count + s;
        if (svc_index >= total_count)
            break;
        SAFE_FREE(gatt_svc_access_diff[svc_index].uuid);
        if (gatt_svc_access_diff[svc_index].characteristics)
        {
            for (size_t ci = 0; ci < gatt_svc_access_diff[svc_index].numOfCharacteristics; ci++)
            {
                SAFE_FREE(gatt_svc_access_diff[svc_index].characteristics[ci].uuid);
                if (gatt_svc_access_diff[svc_index].characteristics[ci].descriptors)
                {
                    for (size_t di = 0;
                         di < gatt_svc_access_diff[svc_index].characteristics[ci].numOfDescriptors; di++)
                    {
                        SAFE_FREE(gatt_svc_access_diff[svc_index].characteristics[ci].descriptors[di].uuid);
                    }
                    SAFE_FREE(gatt_svc_access_diff[svc_index].characteristics[ci].descriptors);
                }
            }
            SAFE_FREE(gatt_svc_access_diff[svc_index].characteristics);
            gatt_svc_access_diff[svc_index].numOfCharacteristics = 0;
        }
    }

    numberOfGattServices = (uint8_t)old_count;
    return IOT_ERR_FAIL;
}

static iot_err_t bluedroid_startScan(uint32_t intervalTime)
{
    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    };

    esp_err_t ret = esp_ble_gap_set_scan_params(&scan_params);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "Set scan params failed: %s", esp_err_to_name(ret));
        return IOT_ERR_FAIL;
    }

    /* Bluedroid takes duration in seconds; 0 = scan forever */
    uint32_t duration_sec = (intervalTime > 0) ? (intervalTime / 1000) : 0;
    if (intervalTime > 0 && duration_sec == 0)
        duration_sec = 1; /* minimum 1 second */

    ret = esp_ble_gap_start_scanning(duration_sec);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "Start scanning failed: %s", esp_err_to_name(ret));
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

static iot_err_t bluedroid_stopScan(void)
{
    esp_err_t ret = esp_ble_gap_stop_scanning();
    return (ret == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

static iot_err_t bluedroid_setDeviceName(const char *name)
{
    esp_err_t ret = esp_ble_gap_set_device_name(name);
    return (ret == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

static iot_err_t bluedroid_startBeaconAdvertising(IOT_BleMgmtAdvData_t *adv_requirements)
{
    /* Store adv params — advertising starts after adv data is set (in GAP callback) */
    memset(&pending_adv_params, 0, sizeof(pending_adv_params));
    pending_adv_params.adv_int_min = 0x20;
    pending_adv_params.adv_int_max = 0x40;
    pending_adv_params.adv_type = ADV_TYPE_NONCONN_IND; /* Non-connectable */
    pending_adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    pending_adv_params.channel_map = ADV_CHNL_ALL;
    pending_adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    /* ADV_NONCONN_IND is not scannable — no scan response is possible here */
    return configureAdvPayloads(adv_requirements, false);
}

static iot_err_t bluedroid_stopBeaconAdvertising(void)
{
    esp_err_t ret = esp_ble_gap_stop_advertising();
    return (ret == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

static iot_err_t bluedroid_startAdvertising(IOT_BleMgmtAdvData_t *adv_requirements)
{
    if (gatts_if_global == ESP_GATT_IF_NONE)
    {
        IOT_LOGE(TAG, "startAdvertising called on a deinitialised stack");
        return IOT_ERR_INVALID_STATE;
    }

    /* Ensure local MTU is set (may have been altered by BLE mesh stack) */
    esp_ble_gatt_set_local_mtu(256);

    IOT_LOGI(TAG, "Adv productId:");
    LOG_HEX_INLINE(LOG_LEVEL_DEBUG, adv_requirements->manufactureData, adv_requirements->manufactureDataLen, TAG);
    if (adv_requirements->name != NULL && adv_requirements->nameLen > 0)
    {
        IOT_LOGI(TAG, "Adv scan-response name: %.*s", adv_requirements->nameLen, adv_requirements->name);
    }

    /* Store adv params — advertising starts after adv data is set (in GAP callback) */
    memset(&pending_adv_params, 0, sizeof(pending_adv_params));
    pending_adv_params.adv_int_min = 0x20;
    pending_adv_params.adv_int_max = 0x40;
    pending_adv_params.adv_type = ADV_TYPE_IND; /* Connectable undirected */
    pending_adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    pending_adv_params.channel_map = ADV_CHNL_ALL;
    pending_adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    /* ADV_IND is scannable — the device name rides in the scan response */
    return configureAdvPayloads(adv_requirements, true);
}

static iot_err_t bluedroid_stopAdvertising(void)
{
    esp_err_t ret = esp_ble_gap_stop_advertising();
    if (ret == ESP_ERR_INVALID_STATE)
    {
        IOT_LOGW(TAG, "BLE advertising already stopped");
        return IOT_OK;
    }
    return (ret == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

static iot_err_t bluedroid_notifyToCharacteristic(const char *addr, const uint8_t *charUuid,
                                                  const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0)
    {
        IOT_LOGE(TAG, "Cannot notify: data is NULL or len is 0");
        return IOT_ERR_INVALID_ARG;
    }
    if (currConnectionHandle == 0xFFFF)
    {
        IOT_LOGE(TAG, "No active connection for notification");
        return IOT_ERR_FAIL;
    }
    if (gatts_if_global == ESP_GATT_IF_NONE)
    {
        IOT_LOGE(TAG, "GATTS not initialized");
        return IOT_ERR_FAIL;
    }

    uint16_t char_handle = 0;
    iot_err_t res = searchAttrhandleByUuid(charUuid, &char_handle);
    if (res != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to find characteristic handle for notification");
        return res;
    }

    /* false = notification (no ACK), true = indication (with ACK) */
    esp_err_t ret = esp_ble_gatts_send_indicate(
        gatts_if_global, currConnectionHandle, char_handle,
        len, (uint8_t *)data, false);
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "Send notification failed: %s", esp_err_to_name(ret));
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

static iot_err_t bluedroid_isConnected(bool *isConnected)
{
    if (isConnected == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    *isConnected = (currConnectionHandle != 0xFFFF);
    return IOT_OK;
}

static iot_err_t bluedroid_disconnect(void)
{
    if (currConnectionHandle == 0xFFFF || gatts_if_global == ESP_GATT_IF_NONE)
    {
        return IOT_ERR_INVALID_STATE;
    }
    return (esp_ble_gatts_close(gatts_if_global, currConnectionHandle) == ESP_OK) ? IOT_OK : IOT_ERR_FAIL;
}

static iot_err_t bluedroid_registerEventCallback(IOT_BleMgmtEventStatusType_t event, IOT_BleEventCb_t cb, void *arg)
{
    if (event >= BLE_MGMT_EVENT_STATUS_TYPE_MAX)
    {
        return IOT_ERR_INVALID_ARG;
    }
    callbackMgmt[event].cb = cb;
    callbackMgmt[event].isRegistered = true;
    callbackMgmt[event].arg = arg;
    return IOT_OK;
}

static iot_err_t bluedroid_unregisterEventCallback(IOT_BleMgmtEventStatusType_t event)
{
    if (event >= BLE_MGMT_EVENT_STATUS_TYPE_MAX)
    {
        return IOT_ERR_INVALID_ARG;
    }
    if (!callbackMgmt[event].isRegistered)
    {
        return IOT_ERR_FAIL;
    }
    callbackMgmt[event].cb = NULL;
    callbackMgmt[event].isRegistered = false;
    callbackMgmt[event].arg = NULL;
    return IOT_OK;
}

// ============================================================================
// Driver registration
// ============================================================================

static const IOT_BleDriver_t s_bluedroidDriver = {
    .init                    = bluedroid_init,
    .deinit                  = bluedroid_deinit,
    .addServices             = bluedroid_addServices,
    .startAdvertising        = bluedroid_startAdvertising,
    .stopAdvertising         = bluedroid_stopAdvertising,
    .startBeaconAdvertising  = bluedroid_startBeaconAdvertising,
    .stopBeaconAdvertising   = bluedroid_stopBeaconAdvertising,
    .notifyToCharacteristic  = bluedroid_notifyToCharacteristic,
    .isConnected             = bluedroid_isConnected,
    .disconnect              = bluedroid_disconnect,
    .startScan               = bluedroid_startScan,
    .stopScan                = bluedroid_stopScan,
    .setDeviceName           = bluedroid_setDeviceName,
    .registerEventCallback   = bluedroid_registerEventCallback,
    .unregisterEventCallback = bluedroid_unregisterEventCallback,
};

void __attribute__((constructor)) IOT_BleMgmtBluedroidRegister(void)
{
    IOT_BleRegisterDriver(&s_bluedroidDriver);
}

#endif /* CONFIG_BT_BLUEDROID_ENABLED */


#include "sdkconfig.h"
#ifdef CONFIG_BT_NIMBLE_ENABLED

/* BLE - NimBLE Implementation */
/* IOT_Log.h must come before NimBLE headers: log_common.h (pulled in by ble_hs.h
   via modlog.h) defines LOG_LEVEL_* as integer macros that collide with our enum. */
#include "IOT_Log.h"
#include "ble/IOT_BleMgmt.h"
#include "ble/IOT_BleDriver.h"
#include "ble/IOT_BleAdvBuild.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/util/util.h"
#include "modlog/modlog.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/ble.h"
#include "services/ans/ble_svc_ans.h"

/* Undo NimBLE's log_common.h macro redefinitions so our enum values are used. */
#undef LOG_LEVEL_DEBUG
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_CRITICAL
#undef LOG_LEVEL_NONE
#undef LOG_LEVEL_MAX

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "IOT_Common.h"

static const char *TAG = "BLE-NE";

typedef struct
{
    uint8_t descriptor_index;                 // Index of the descriptor in the characteristic
    uint8_t uuid_len;                         // Length of the UUID in bytes
    uint8_t *uuid;                            // Pointer to the UUID of the descriptor
    uint16_t handle;                          // Handle of the descriptor definition
    uint16_t descriptor_handle;               // Handle of the descriptor
    IOT_BleMgmtGattAttrAccessCb_t *access_cb; // Access callback for this descriptor
    void *arg;                                // Argument to pass to the access callback
} iot_gatt_descriptor_access_diff_t;

typedef struct
{
    uint8_t characteristic_index;                   // Index of the characteristic in the service
    uint8_t uuid_len;                               // Length of the UUID in bytes
    uint8_t *uuid;                                  // Pointer to the UUID of the characteristic
    uint16_t def_handle;                            // Handle of the characteristic
    uint16_t val_handle;                            // Handle of the characteristic value
    uint8_t numOfDescriptors;                       // Number of descriptors in this characteristic
    iot_gatt_descriptor_access_diff_t *descriptors; // Array of descriptor access diffs
    IOT_BleMgmtGattAttrAccessCb_t *access_cb;       // Access callback for this characteristic
    void *arg;                                      // Argument to pass to the access callback
} iot_gatt_characteristic_access_diff_t;

typedef struct
{
    uint8_t service_index;
    uint8_t *uuid; // Pointer to the UUID of the service
    uint8_t uuid_len;
    uint16_t service_handle;                                // Handle of the service
    uint8_t numOfCharacteristics;                           // Number of characteristics in this service
    iot_gatt_characteristic_access_diff_t *characteristics; // Array of characteristic access diffs
} iot_gatt_service_access_diff_t;

typedef struct
{
    IOT_BleEventCb_t cb;
    void *arg;
    bool isRegistered;
} ble_cb_mgmt_t;

static ble_cb_mgmt_t *callbackMgmt = NULL;
static uint8_t numberOfGattServices = 0;
static iot_gatt_service_access_diff_t *gatt_svc_access_diff = NULL;
static uint16_t currConnectionHandle = 0;
static bool isBleConnected = false;

/* Persisted wrappers for services added via IOT_BleAddServices */
static struct ble_gatt_svc_def **persistent_services = NULL;
static size_t *persistent_services_sizes = NULL;
static size_t persistent_services_count = 0;

/* True only between a successful nimble_init() and nimble_deinit(). Guards entry
 * points that call into the NimBLE host so a caller hitting a torn-down stack
 * (e.g. OTA deinitializes BLE to free heap) gets an error instead of a crash. */
static bool s_nimbleInitialized = false;

/**
 * @brief Find attribute indexes by handle
 * @param attr_handle Handle to search for
 * @param numberOfService Number of services to search
 * @param svc_idx Output: service index
 * @param char_idx Output: characteristic index (0xFF if not a characteristic)
 * @param desc_idx Output: descriptor index (0xFF if not a descriptor)
 * @return IOT_OK if found, IOT_ERR_NOT_FOUND otherwise
 */
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
static int
internalCharAccessCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t svc_idx, char_idx, desc_idx;
    if (getAttrIndex(attr_handle, numberOfGattServices, &svc_idx, &char_idx, &desc_idx) != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to get attribute index");
        return -1;
    }

    iot_gatt_service_access_diff_t *service = &gatt_svc_access_diff[svc_idx];
    if (char_idx >= service->numOfCharacteristics)
    {
        IOT_LOGE(TAG, "Invalid characteristic index: %d", char_idx);
        return -1;
    }

    iot_gatt_characteristic_access_diff_t *characteristic = &service->characteristics[char_idx];
    if (characteristic->access_cb == NULL)
    {
        IOT_LOGE(TAG, "No access callback defined for characteristic index: %d", char_idx);
        return -1;
    }

    struct IOT_BleMgmtGattAccessContext_t *convertCtxt =
        Mem_SafeMalloc(sizeof(struct IOT_BleMgmtGattAccessContext_t), TAG, "access context");
    if (convertCtxt == NULL)
    {
        return -1;
    }

    convertCtxt->op = ctxt->op;
    convertCtxt->characteristic = NULL;
    convertCtxt->om = NULL;

    if (ctxt->chr != NULL)
    {
        convertCtxt->characteristic = Mem_SafeMalloc(sizeof(IOT_BleMgmtGattChar_t), TAG, "char context");
        if (convertCtxt->characteristic == NULL)
        {
            goto cleanup_error;
        }
    }
    else if (ctxt->dsc != NULL)
    {
        IOT_LOGW(TAG, "Descriptor access in char callback");
    }
    else
    {
        IOT_LOGE(TAG, "Invalid context in characteristic access callback");
        goto cleanup_error;
    }

    convertCtxt->characteristic->flags = ctxt->chr->flags;
    convertCtxt->characteristic->uuid_len = characteristic->uuid_len;
    convertCtxt->characteristic->uuid = Mem_SafeMalloc(characteristic->uuid_len, TAG, "char UUID");
    if (convertCtxt->characteristic->uuid == NULL)
    {
        goto cleanup_error;
    }
    memcpy(convertCtxt->characteristic->uuid, characteristic->uuid, characteristic->uuid_len);

    convertCtxt->om = Mem_SafeMalloc(sizeof(uint_data_t), TAG, "operation buffer");
    if (convertCtxt->om == NULL)
    {
        goto cleanup_error;
    }

    convertCtxt->om->data = Mem_SafeMalloc(ctxt->om->om_len, TAG, "operation data");
    if (convertCtxt->om->data == NULL)
    {
        goto cleanup_error;
    }
    convertCtxt->om->len = ctxt->om->om_len;
    memcpy(convertCtxt->om->data, ctxt->om->om_data, convertCtxt->om->len);

    iot_err_t err = characteristic->access_cb(convertCtxt, characteristic->arg);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Characteristic access callback failed: %s", iot_err_to_name(err));
        goto cleanup_error;
    }

    /* Cleanup on success */
    SAFE_FREE(convertCtxt->om->data);
    SAFE_FREE(convertCtxt->om);
    if (convertCtxt->characteristic)
    {
        SAFE_FREE(convertCtxt->characteristic->uuid);
        SAFE_FREE(convertCtxt->characteristic);
    }
    SAFE_FREE(convertCtxt);
    return 0;

cleanup_error:
    if (convertCtxt)
    {
        if (convertCtxt->om)
        {
            SAFE_FREE(convertCtxt->om->data);
            SAFE_FREE(convertCtxt->om);
        }
        if (convertCtxt->characteristic)
        {
            SAFE_FREE(convertCtxt->characteristic->uuid);
            SAFE_FREE(convertCtxt->characteristic);
        }
        SAFE_FREE(convertCtxt);
    }
    return -1;
}

static int
internalDescAccessCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    IOT_LOGW(TAG, "Descriptor access callback invoked for handle: %d", attr_handle);
    /* TODO: Implement descriptor access logic */
    return 0;
}

static ble_uuid_t *convertToESP32UUID(const uint8_t *uuid, size_t uuid_len)
{
    if (uuid == NULL || uuid_len == 0)
    {
        return NULL;
    }

    if (uuid_len == 2)
    {
        ble_uuid16_t *uuid16 = Mem_SafeMalloc(sizeof(ble_uuid16_t), TAG, "UUID16");
        if (uuid16 == NULL)
        {
            return NULL;
        }
        uint16_t uuid16_val = BYTES_TO_U16_BE(uuid[0], uuid[1]);
        *uuid16 = (ble_uuid16_t) BLE_UUID16_INIT(uuid16_val);
        return (ble_uuid_t *) uuid16;
    }
    else if (uuid_len == 4)
    {
        ble_uuid32_t *uuid32 = Mem_SafeMalloc(sizeof(ble_uuid32_t), TAG, "UUID32");
        if (uuid32 == NULL)
        {
            return NULL;
        }
        uint32_t uuid32_val = ((uint32_t) uuid[0] << 24) | ((uint32_t) uuid[1] << 16) |
                              ((uint32_t) uuid[2] << 8) | uuid[3];
        *uuid32 = (ble_uuid32_t) {.u = {.type = BLE_UUID_TYPE_32}, .value = uuid32_val};
        return (ble_uuid_t *) uuid32;
    }
    else if (uuid_len == 16)
    {
        ble_uuid128_t *uuid128 = Mem_SafeMalloc(sizeof(ble_uuid128_t), TAG, "UUID128");
        if (uuid128 == NULL)
        {
            return NULL;
        }
        uuid128->u.type = BLE_UUID_TYPE_128;
        memcpy(uuid128->value, uuid, 16);
        return (ble_uuid_t *) uuid128;
    }

    return NULL;
}

static void onSvcRegisterCb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    if (ctxt == NULL)
    {
        IOT_LOGE(TAG, "Invalid context in service registration callback");
        return;
    }

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
    {
        if (gatt_svc_access_diff != NULL)
        {
            for (int i = 0; i < numberOfGattServices; i++)
            {
                ble_uuid_t *stored_uuid = NULL;
                if (gatt_svc_access_diff[i].uuid != NULL && gatt_svc_access_diff[i].uuid_len > 0)
                {
                    stored_uuid = convertToESP32UUID(gatt_svc_access_diff[i].uuid, gatt_svc_access_diff[i].uuid_len);
                }
                if (stored_uuid != NULL)
                {
                    if (ble_uuid_cmp(ctxt->svc.svc_def->uuid, stored_uuid) == 0)
                    {
                        gatt_svc_access_diff[i].service_handle = ctxt->svc.handle;
                        SAFE_FREE(stored_uuid);
                        return;
                    }
                    SAFE_FREE(stored_uuid);
                }
            }
        }
        break;
    }
    case BLE_GATT_REGISTER_OP_CHR:
    {
        for (int i = 0; i < numberOfGattServices; i++)
        {
            for (int j = 0; j < gatt_svc_access_diff[i].numOfCharacteristics; j++)
            {
                ble_uuid_t *stored_uuid = NULL;
                if (gatt_svc_access_diff[i].characteristics[j].uuid != NULL &&
                    gatt_svc_access_diff[i].characteristics[j].uuid_len > 0)
                {
                    stored_uuid = convertToESP32UUID(gatt_svc_access_diff[i].characteristics[j].uuid,
                                                     gatt_svc_access_diff[i].characteristics[j].uuid_len);
                }
                if (stored_uuid != NULL)
                {
                    if (ble_uuid_cmp(ctxt->chr.chr_def->uuid, stored_uuid) == 0)
                    {
                        gatt_svc_access_diff[i].characteristics[j].def_handle = ctxt->chr.def_handle;
                        gatt_svc_access_diff[i].characteristics[j].val_handle = ctxt->chr.val_handle;
                        SAFE_FREE(stored_uuid);
                        return;
                    }
                    SAFE_FREE(stored_uuid);
                }
            }
        }
        break;
    }
    case BLE_GATT_REGISTER_OP_DSC:
    {
        for (int i = 0; i < numberOfGattServices; i++)
        {
            for (int j = 0; j < gatt_svc_access_diff[i].numOfCharacteristics; j++)
            {
                for (int k = 0; k < gatt_svc_access_diff[i].characteristics[j].numOfDescriptors; k++)
                {
                    ble_uuid_t *stored_uuid = NULL;
                    if (gatt_svc_access_diff[i].characteristics[j].descriptors[k].uuid != NULL &&
                        gatt_svc_access_diff[i].characteristics[j].descriptors[k].uuid_len > 0)
                    {
                        stored_uuid = convertToESP32UUID(
                            gatt_svc_access_diff[i].characteristics[j].descriptors[k].uuid,
                            gatt_svc_access_diff[i].characteristics[j].descriptors[k].uuid_len);
                    }
                    if (stored_uuid != NULL)
                    {
                        if (ble_uuid_cmp(ctxt->dsc.dsc_def->uuid, stored_uuid) == 0)
                        {
                            gatt_svc_access_diff[i].characteristics[j].descriptors[k].handle = ctxt->dsc.handle;
                            SAFE_FREE(stored_uuid);
                            return;
                        }
                        SAFE_FREE(stored_uuid);
                    }
                }
            }
        }
        break;
    }
    default:
    {
        IOT_LOGE(TAG, "Unknown GATT registration operation: %d", ctxt->op);
        break;
    }
    }
}

static IOT_BleMgmtEventStatusType_t getEventTypeFromEspBleEvent(int event_type)
{
    switch (event_type)
    {
    case BLE_GAP_EVENT_ADV_COMPLETE:
        return BLE_MGMT_EVENT_STATUS_TYPE_ADV_STOP;
    case BLE_GAP_EVENT_DISC:
        return BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        return BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE;
    case BLE_GAP_EVENT_CONNECT:
        return BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED;
    case BLE_GAP_EVENT_DISCONNECT:
        return BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT;
    case BLE_GAP_EVENT_SUBSCRIBE:
        return BLE_MGMT_EVENT_STATUS_TYPE_ENABLE_STATUS;
    case BLE_GAP_EVENT_NOTIFY_TX:
        return BLE_MGMT_EVENT_STATUS_TYPE_SEND_NOTIFICATION;
    default:
        return BLE_MGMT_EVENT_STATUS_TYPE_MAX; // Unknown event type
    }
}
static int internalGapCb(struct ble_gap_event *event, void *arg)
{
    if (event == NULL)
    {
        return 0;
    }

    IOT_BleMgmtEventStatusType_t evType = getEventTypeFromEspBleEvent(event->type);

    /* Special-case CONNECT: if connect failed, treat as DISCONNECT callback */
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status != 0)
    {
        evType = BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT;
    }

    if (callbackMgmt == NULL)
    {
        return 0;
    }

    switch (evType)
    {
    case BLE_MGMT_EVENT_STATUS_TYPE_ADV_STOP:
    {
        if (callbackMgmt[evType].isRegistered)
        {
            callbackMgmt[evType].cb(callbackMgmt[evType].arg, evType, NULL);
        }
        break;
    }

    case BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT:
    {
        if (callbackMgmt[evType].isRegistered)
        {
            IOT_BleMgmtEventScanResultData_t result;
            memset(&result, 0, sizeof(result));
            result.addr_type = event->disc.addr.type;
            memcpy(result.addr, event->disc.addr.val, 6);
            result.rssi = event->disc.rssi;
            result.data_len = event->disc.length_data;
            if (result.data_len > 0 && event->disc.data != NULL)
            {
                result.data = Mem_SafeMalloc(result.data_len, TAG, "scan result data");
                if (result.data != NULL)
                {
                    memcpy(result.data, event->disc.data, result.data_len);
                }
                else
                {
                    result.data_len = 0;
                }
            }
            callbackMgmt[evType].cb(callbackMgmt[evType].arg, evType, &result);
        }
        break;
    }
    case BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE:
    {
        if (callbackMgmt[evType].isRegistered)
        {
            callbackMgmt[evType].cb(callbackMgmt[evType].arg, evType, NULL);
        }
        break;
    }

    case BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED:
    {
        if (callbackMgmt[evType].isRegistered)
        {
            IOT_BleMgmtEventConnectData_t conn_info;
            memset(&conn_info, 0, sizeof(conn_info));
            currConnectionHandle = event->connect.conn_handle;
            isBleConnected = true;
            callbackMgmt[evType].cb(callbackMgmt[evType].arg, evType, &conn_info);
        }
        break;
    }

    case BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT:
    {
        currConnectionHandle = 0;
        isBleConnected = false;
        if (callbackMgmt[evType].isRegistered)
        {
            IOT_BleMgmtEventDisconnectData_t dis_info;
            memset(&dis_info, 0, sizeof(dis_info));
            if (event->type == BLE_GAP_EVENT_DISCONNECT)
            {
                dis_info.reason = event->disconnect.reason;
                IOT_LOGI(TAG, "Disconnected, reason: %d", dis_info.reason);
            }
            callbackMgmt[evType].cb(callbackMgmt[evType].arg, evType, &dis_info);
        }
        break;
    }

    case BLE_MGMT_EVENT_STATUS_TYPE_ENABLE_STATUS:
    {
        if (callbackMgmt[evType].isRegistered)
        {
            IOT_BleMgmtEventNotifyIndicateEnable_t sub_info;
            memset(&sub_info, 0, sizeof(sub_info));
            if (event->type == BLE_GAP_EVENT_SUBSCRIBE)
            {
                if (event->subscribe.prev_notify == 0 && event->subscribe.cur_notify == 1)
                {
                    sub_info.success = 1;
                }
                else if (event->subscribe.prev_notify == 1 && event->subscribe.cur_notify == 0)
                {
                    sub_info.success = 0;
                }
                /* can populate conn_handle/attr_handle etc if needed */
            }
            callbackMgmt[evType].cb(callbackMgmt[evType].arg, evType, &sub_info);
        }
        break;
    }

    case BLE_MGMT_EVENT_STATUS_TYPE_SEND_NOTIFICATION:
    {
        if (callbackMgmt[evType].isRegistered)
        {
            IOT_BleMgmtNotifySendData_t notif_info;
            memset(&notif_info, 0, sizeof(notif_info));
            /* can populate fields from event->notify_tx if desired */
            callbackMgmt[evType].cb(callbackMgmt[evType].arg, evType, &notif_info);
        }
        break;
    }

    default:
        /* Unknown or unhandled event mapped by getEventTypeFromEspBleEvent */
        break;
    }

    return 0;
}

static iot_err_t nimble_startScan(uint32_t intervalTime)
{
    struct ble_gap_disc_params disc_params = {
        .itvl = 0x0010,
        .window = 0x0010,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = false,
        .passive = false,
    };
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        return IOT_ERR_FAIL;
    }
    rc = ble_gap_disc(own_addr_type,
                      intervalTime / 10,
                      &disc_params,
                      internalGapCb,
                      NULL); // Convert duration from ms to 0.625ms units, handling args
    if (rc != 0)
    {
        return IOT_ERR_FAIL;
    }

    return IOT_OK;
}

static iot_err_t nimble_setDeviceName(const char *name)
{
    int rc = ble_svc_gap_device_name_set(name);
    if (rc != 0)
    {
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}
static iot_err_t nimble_startBeaconAdvertising(IOT_BleMgmtAdvData_t *adv_requirements)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    ble_uuid16_t *uuid16 = NULL;
    uint8_t *mfg = NULL;

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* 16-bit Service UUIDs */
    if (adv_requirements->uuids != NULL && adv_requirements->numUuids > 0)
    {
        uuid16 = Mem_SafeMalloc(sizeof(ble_uuid16_t) * adv_requirements->numUuids, TAG, "beacon adv UUID16");
        if (uuid16 == NULL)
        {
            return IOT_ERR_NO_MEM;
        }
        for (uint8_t i = 0; i < adv_requirements->numUuids; i++)
        {
            uint16_t val = BYTES_TO_U16_BE(adv_requirements->uuids[i][0], adv_requirements->uuids[i][1]);
            uuid16[i] = (ble_uuid16_t) BLE_UUID16_INIT(val);
        }
        fields.uuids16           = uuid16;
        fields.num_uuids16       = adv_requirements->numUuids;
        fields.uuids16_is_complete = 1;
    }

    /* Manufacturer Specific Data — prepend company ID in LE order */
    if (adv_requirements->manufactureData != NULL && adv_requirements->manufactureDataLen > 0)
    {
        mfg = Mem_SafeMalloc(adv_requirements->manufactureDataLen + 2, TAG, "beacon adv mfg data");
        if (mfg == NULL)
        {
            SAFE_FREE(uuid16);
            return IOT_ERR_NO_MEM;
        }
        mfg[0] = adv_requirements->manufactureId & 0xFF;        /* company ID low byte */
        mfg[1] = (adv_requirements->manufactureId >> 8) & 0xFF; /* company ID high byte */
        memcpy(mfg + 2, adv_requirements->manufactureData, adv_requirements->manufactureDataLen);
        fields.mfg_data = mfg;
        fields.mfg_data_len = adv_requirements->manufactureDataLen + 2;
    }

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        SAFE_FREE(uuid16);
        SAFE_FREE(mfg);
        return IOT_ERR_FAIL;
    }

    // Start advertising
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON; // Non-connectable advertising
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // General discoverable mode

    uint8_t own_addr_type;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        SAFE_FREE(uuid16);
        SAFE_FREE(mfg);
        return IOT_ERR_FAIL;
    }

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, internalGapCb, NULL);
    if (rc != 0)
    {
        SAFE_FREE(uuid16);
        SAFE_FREE(mfg);
        return IOT_ERR_FAIL;
    }

    /* uuid16 and mfg must remain valid while advertising is active */
    return IOT_OK;
}
static iot_err_t nimble_stopBeaconAdvertising(void)
{
    int rc = ble_gap_adv_stop();
    if (rc != 0)
    {
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}
static void blecent_host_task(void *param)
{
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();
    IOT_LOGI(TAG, "BLE host task stopped");
    nimble_port_freertos_deinit();
}

static iot_err_t nimble_init(void)
{
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

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        IOT_LOGE(TAG, "Failed to init nimble: %d", ret);
        SAFE_FREE(callbackMgmt);
        return IOT_ERR_FAIL;
    }

    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_ans_init();

    nimble_port_freertos_init(blecent_host_task);
    s_nimbleInitialized = true;
    return IOT_OK;
}

/* Deep-frees the nested characteristic/descriptor UUID allocations owned by our
 * OWN bookkeeping struct (iot_gatt_service_access_diff_t), for every service
 * ever added (index 0..numberOfGattServices). This is the same walk
 * nimble_addServices()'s fail_cleanup label already does for the range it just
 * failed to add (see the loop over [old_count, old_count+add_count) there) —
 * here it runs over every persisted service instead, because deinit tears
 * down the whole session rather than one failed add. */
static void freeGattSvcAccessDiff(void)
{
    for (size_t svc_index = 0; svc_index < numberOfGattServices; svc_index++)
    {
        SAFE_FREE(gatt_svc_access_diff[svc_index].uuid);
        if (gatt_svc_access_diff[svc_index].characteristics)
        {
            for (size_t ci = 0; ci < gatt_svc_access_diff[svc_index].numOfCharacteristics; ci++)
            {
                SAFE_FREE(gatt_svc_access_diff[svc_index].characteristics[ci].uuid);
                if (gatt_svc_access_diff[svc_index].characteristics[ci].descriptors)
                {
                    for (size_t di = 0; di < gatt_svc_access_diff[svc_index].characteristics[ci].numOfDescriptors; di++)
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
}

/* Deep-frees one IOT_BleAddServices() call's NimBLE-side service array —
 * the "Do NOT free" comment at the success path of nimble_addServices() holds
 * only while the stack that references these allocations is alive; once
 * nimble_port_deinit() has torn the host down, NimBLE no longer holds them
 * and they must be released here instead.
 *
 * Mirrors the fail_cleanup walk in nimble_addServices() (the "Free any
 * NimBLE-side allocated UUIDs attached to services_local and char_defs_arr"
 * block) field-for-field and loop-for-loop, over `count` services instead of
 * `add_count`. The only structural difference is that fail_cleanup reads the
 * characteristics array through the separately-tracked char_defs_arr[s] (still
 * live at that point, before the SAFE_FREE(char_defs_arr) on the success
 * path); here that tracking array is long gone, so the characteristics
 * pointer is recovered from services_local[s].characteristics itself — the
 * same address, since nimble_addServices() assigned
 * `services_local[s].characteristics = char_defs;` before persisting it. */
static void freePersistentServiceArray(struct ble_gatt_svc_def *services_local, size_t count)
{
    if (services_local == NULL)
    {
        return;
    }

    for (size_t s = 0; s < count; s++)
    {
        struct ble_gatt_chr_def *cdefs = (struct ble_gatt_chr_def *)(uintptr_t) services_local[s].characteristics;
        if (cdefs)
        {
            for (size_t ci = 0; cdefs[ci].uuid != NULL; ci++)
            {
                if (cdefs[ci].descriptors)
                {
                    for (size_t di = 0; cdefs[ci].descriptors[di].uuid != NULL; di++)
                    {
                        SAFE_FREE_CONST(cdefs[ci].descriptors[di].uuid);
                    }
                    SAFE_FREE(cdefs[ci].descriptors);
                }
                SAFE_FREE_CONST(cdefs[ci].uuid);
            }
            SAFE_FREE(cdefs);
        }
        SAFE_FREE_CONST(services_local[s].uuid);
    }

    SAFE_FREE(services_local);
}

static iot_err_t nimble_deinit(void)
{
    s_nimbleInitialized = false;
    nimble_port_stop();
    esp_err_t err = nimble_port_deinit();
    iot_err_t ret = IOT_OK;
    if (err != ESP_OK)
    {
        /* nimble_port_deinit() runs esp_nimble_deinit() (which tears down the host
         * and frees NimBLE's own ble_gatts_svc_defs) BEFORE disabling/deinitialising
         * the controller — so a failure here still leaves the host, and everything
         * this function is about to free, gone. Falling through to the resets below
         * instead of returning early is what makes the state consistent with what
         * actually happened to the stack, not just with what the return code says. */
        IOT_LOGE(TAG, "nimble_port_deinit failed: %d", err);
        ret = IOT_ERR_FAIL;
    }

    SAFE_FREE(callbackMgmt);
    freeGattSvcAccessDiff();
    SAFE_FREE(gatt_svc_access_diff);
    numberOfGattServices = 0;
    currConnectionHandle = 0;
    isBleConnected = false;

    /* Handles returned by searchAttrhandleByUuid() belong to the destroyed session.
     * Leaving these set means notifyToCharacteristic() notifies against a stale
     * handle — silently wrong rather than an error. */
    for (size_t i = 0; i < persistent_services_count; i++)
    {
        freePersistentServiceArray(persistent_services[i], persistent_services_sizes[i]);
    }
    SAFE_FREE(persistent_services);
    SAFE_FREE(persistent_services_sizes);
    persistent_services_count = 0;

    return ret;
}

static iot_err_t nimble_addServices(const IOT_BleMgmtGattServ_t *service)
{
    if (!s_nimbleInitialized)
    {
        IOT_LOGE(TAG, "addServices called while NimBLE is deinitialized — refusing");
        return IOT_ERR_INVALID_STATE;
    }
    // register for service registration callback
    ble_hs_cfg.gatts_register_cb = onSvcRegisterCb;
    ble_hs_cfg.sm_io_cap = 3;
    ble_hs_cfg.sm_sc = 0;
    if (service == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }

    /* count incoming services */
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

    /* expand persistent gatt_svc_access_diff storage to accommodate new services */
    size_t old_count = numberOfGattServices;
    size_t total_count = old_count + add_count;
    // IOT_LOGI("esp32", "Adding %d services, total will be %d\n", (int)add_count, (int)total_count);
    iot_gatt_service_access_diff_t *new_diff =
        Mem_SafeRealloc(gatt_svc_access_diff, total_count * sizeof(iot_gatt_service_access_diff_t), TAG, "gatt svc diff grow");
    if (new_diff == NULL)
    {
        return IOT_ERR_NO_MEM;
    }
    /* zero-initialize newly added entries */
    if (total_count > old_count)
    {
        memset(&new_diff[old_count], 0, (total_count - old_count) * sizeof(iot_gatt_service_access_diff_t));
    }
    gatt_svc_access_diff = new_diff;

    /* Prepare the services wrapper that will be passed to nimble.
       IMPORTANT: this wrapper and all nested allocations must remain valid after this function
       (if ble_gatts_add_dynamic_svcs succeeds). We will persist the wrapper only on success. */
    struct ble_gatt_svc_def *services_local = Mem_SafeCalloc(add_count + 1, sizeof(struct ble_gatt_svc_def), TAG, "gatt svc defs");
    if (services_local == NULL)
    {
        return IOT_ERR_NO_MEM;
    }

    /* helper arrays to track per-service allocations for cleanup on failure */
    struct ble_gatt_chr_def **char_defs_arr = Mem_SafeCalloc(add_count, sizeof(struct ble_gatt_chr_def *), TAG, "char defs arr");
    if (char_defs_arr == NULL)
    {
        SAFE_FREE(services_local);
        return IOT_ERR_NO_MEM;
    }

    /* Build services_local and copy UUIDs into persistent storage. On any error, jump to fail_cleanup. */
    for (size_t s = 0; s < add_count; s++)
    {
        const IOT_BleMgmtGattServ_t *in_svc = &service[s];
        size_t svc_index = old_count + s;

        /* fill metadata in persistent diff (copy uuid bytes instead of assigning pointer) */
        gatt_svc_access_diff[svc_index].service_index = (uint8_t) svc_index;
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

        /* build nimble service entry */
        services_local[s].type = (in_svc->type == IOT_GATT_SERVICE_TYPE_PRIMARY) ? BLE_GATT_SVC_TYPE_PRIMARY
                                                                                 : BLE_GATT_SVC_TYPE_SECONDARY;

        /* convert service UUID for NimBLE (allocated by convertToESP32UUID) */
        ble_uuid_t *svc_uuid = convertToESP32UUID(in_svc->uuid, in_svc->uuid_len);
        if (svc_uuid == NULL)
        {
            goto fail_cleanup;
        }
        services_local[s].uuid = svc_uuid;
        services_local[s].includes = NULL;

        /* count characteristics safely */
        size_t char_count = 0;
        const IOT_BleMgmtGattChar_t *char_ptr = in_svc->characteristics;
        if (char_ptr != NULL)
        {
            while (char_ptr[char_count].uuid != NULL)
            {
                char_count++;
            }
        }

        /* allocate char_defs for this service */
        struct ble_gatt_chr_def *char_defs = Mem_SafeCalloc(char_count + 1, sizeof(struct ble_gatt_chr_def), TAG, "char defs");
        if (char_defs == NULL)
        {
            goto fail_cleanup;
        }
        char_defs_arr[s] = char_defs;

        /* allocate and attach characteristics access diff storage (persistent copy) */
        gatt_svc_access_diff[svc_index].characteristics = Mem_SafeCalloc(char_count, sizeof(iot_gatt_characteristic_access_diff_t), TAG, "char access diff");
        if (gatt_svc_access_diff[svc_index].characteristics == NULL)
        {
            goto fail_cleanup;
        }
        gatt_svc_access_diff[svc_index].numOfCharacteristics = (uint8_t)char_count;

        /* populate each characteristic and descriptors */
        for (size_t ci = 0; ci < char_count; ci++)
        {
            const IOT_BleMgmtGattChar_t *in_char = &char_ptr[ci];

            /* persistent copy of characteristic uuid */
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

            char_defs[ci].arg = in_char->arg;
            char_defs[ci].cpfd = NULL;
            char_defs[ci].flags = in_char->flags;
            /* value handle pointer to persistent diff storage */
            char_defs[ci].val_handle = &gatt_svc_access_diff[svc_index].characteristics[ci].val_handle;

            /* NimBLE characteristic UUID */
            ble_uuid_t *char_uuid = convertToESP32UUID(in_char->uuid, in_char->uuid_len);
            if (char_uuid == NULL)
            {
                goto fail_cleanup;
            }
            char_defs[ci].uuid = char_uuid;
            char_defs[ci].access_cb = internalCharAccessCallback;

            /* descriptors */
            size_t desc_count = 0;
            const IOT_BleMgmtGattDesc_t *desc_ptr = in_char->descriptor;
            if (desc_ptr != NULL)
            {
                while (desc_ptr[desc_count].uuid != NULL)
                    desc_count++;
            }

            char_defs[ci].descriptors = Mem_SafeCalloc(desc_count + 1, sizeof(struct ble_gatt_dsc_def), TAG, "desc defs");
            if (char_defs[ci].descriptors == NULL && desc_count > 0)
            {
                goto fail_cleanup;
            }

            /* persistent descriptor access diff array */
            if (desc_count > 0)
            {
                gatt_svc_access_diff[svc_index].characteristics[ci].numOfDescriptors = (uint8_t)desc_count;
                gatt_svc_access_diff[svc_index].characteristics[ci].descriptors =
                    Mem_SafeCalloc(desc_count, sizeof(iot_gatt_descriptor_access_diff_t), TAG, "desc access diff");
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

            for (size_t di = 0; di < desc_count; di++)
            {
                const IOT_BleMgmtGattDesc_t *in_desc = &desc_ptr[di];

                /* persistent copy of descriptor uuid */
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

                /* NimBLE descriptor UUID */
                ble_uuid_t *desc_uuid = convertToESP32UUID(in_desc->uuid, in_desc->uuid_len);
                if (desc_uuid == NULL)
                {
                    goto fail_cleanup;
                }
                char_defs[ci].descriptors[di].uuid = desc_uuid;
                char_defs[ci].descriptors[di].att_flags = in_desc->att_flags;
                char_defs[ci].descriptors[di].access_cb = internalDescAccessCallback;
                char_defs[ci].descriptors[di].arg = in_desc->arg;
            }
            /* calloc zero-terminates descriptor list */
        } /* end char loop */

        /* attach char_defs to service wrapper */
        services_local[s].characteristics = char_defs;
    } /* end service loop */

    /* Successfully built services_local for add_count services.
       Now call NimBLE to register only these new services. */
    numberOfGattServices = (uint8_t) total_count;

    int rc;
    ble_svc_gatt_init();
    ble_svc_ans_init();

    rc = ble_gatts_count_cfg(services_local);
    if (rc != 0)
    {
        rc = IOT_ERR_FAIL;
        goto fail_cleanup;
    }

    rc = ble_gatts_add_dynamic_svcs(services_local);
    if (rc != 0)
    {
        rc = IOT_ERR_FAIL;
        goto fail_cleanup;
    }

    /* success: persist services_local and update global count.
       Do NOT free services_local or its nested allocations; NimBLE references them. */
    {
        /* Commit each pointer to its global the instant its own realloc succeeds, so a
           successful realloc's result is never discarded -- Mem_SafeRealloc() (realloc())
           frees the OLD block on success, so leaving a successful result unassigned would
           make the surviving global pointer dangling. persistent_services_count is only
           incremented when BOTH arrays are large enough to hold the new entry, since it
           bounds the deinit walk; a one-off capacity mismatch between the two arrays is
           harmless because the walk never indexes past persistent_services_count. */
        struct ble_gatt_svc_def **tmp = Mem_SafeRealloc(persistent_services, (persistent_services_count + 1) * sizeof(*persistent_services), TAG, "persistent svcs grow");
        if (tmp != NULL)
        {
            persistent_services = tmp;
        }

        size_t *tmp_sizes = Mem_SafeRealloc(persistent_services_sizes, (persistent_services_count + 1) * sizeof(*persistent_services_sizes), TAG, "persistent sizes grow");
        if (tmp_sizes != NULL)
        {
            persistent_services_sizes = tmp_sizes;
        }

        if (tmp != NULL && tmp_sizes != NULL)
        {
            persistent_services[persistent_services_count] = services_local;
            persistent_services_sizes[persistent_services_count] = add_count;
            persistent_services_count++;
        }
        else
        {
            /* best-effort: if we cannot record, keep services_local allocated (required by NimBLE)
               but log warning; we will not be able to free it later via our deinit unless manual cleanup implemented. */
            IOT_LOGW(TAG, "Failed to persist services pointer; services retained but not tracked for free.");
        }
    }

    SAFE_FREE(char_defs_arr); /* we keep char_defs arrays attached to services_local, only the tracker array freed */

    return IOT_OK;

fail_cleanup:
    /* Free any NimBLE-side allocated UUIDs attached to services_local and char_defs_arr */
    if (char_defs_arr)
    {
        for (size_t s = 0; s < add_count; s++)
        {
            struct ble_gatt_chr_def *cdefs = char_defs_arr[s];
            if (cdefs)
            {
                for (size_t ci = 0; cdefs[ci].uuid != NULL; ci++)
                {
                    if (cdefs[ci].descriptors)
                    {
                        for (size_t di = 0; cdefs[ci].descriptors[di].uuid != NULL; di++)
                        {
                            SAFE_FREE_CONST(cdefs[ci].descriptors[di].uuid);
                        }
                        SAFE_FREE(cdefs[ci].descriptors);
                    }
                    SAFE_FREE_CONST(cdefs[ci].uuid);
                }
                SAFE_FREE(cdefs);
            }
        }
    }

    /* Free service-level NimBLE UUIDs */
    if (services_local)
    {
        for (size_t s = 0; s < add_count; s++)
        {
            SAFE_FREE_CONST(services_local[s].uuid);
        }
    }

    /* Free persistent copies in gatt_svc_access_diff created by this call */
    for (size_t s = 0; s < add_count; s++)
    {
        size_t svc_index = old_count + s;
        SAFE_FREE(gatt_svc_access_diff[svc_index].uuid);
        if (gatt_svc_access_diff[svc_index].characteristics)
        {
            for (size_t ci = 0; ci < gatt_svc_access_diff[svc_index].numOfCharacteristics; ci++)
            {
                SAFE_FREE(gatt_svc_access_diff[svc_index].characteristics[ci].uuid);
                if (gatt_svc_access_diff[svc_index].characteristics[ci].descriptors)
                {
                    for (size_t di = 0; di < gatt_svc_access_diff[svc_index].characteristics[ci].numOfDescriptors; di++)
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

    SAFE_FREE(char_defs_arr);
    SAFE_FREE(services_local);

    return (rc == 0) ? IOT_OK : (iot_err_t) rc;
}
static iot_err_t nimble_stopScan(void)
{
    int rc = ble_gap_disc_cancel();
    if (rc != 0)
    {
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}

static iot_err_t nimble_startAdvertising(IOT_BleMgmtAdvData_t *adv_requirements)
{
    if (!s_nimbleInitialized)
    {
        IOT_LOGE(TAG, "startAdvertising called while NimBLE is deinitialized — refusing");
        return IOT_ERR_INVALID_STATE;
    }
    /* Ensure local MTU is set (may have been altered by BLE mesh stack) */
    ble_att_set_preferred_mtu(256);

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    ble_uuid16_t *uuid16 = NULL;
    uint8_t *mfg = NULL;

    if (adv_requirements->uuids != NULL && adv_requirements->numUuids > 0)
    {
        uuid16 = Mem_SafeMalloc(sizeof(ble_uuid16_t) * adv_requirements->numUuids, TAG, "adv UUID16");
        if (uuid16 == NULL)
        {
            return IOT_ERR_NO_MEM;
        }
        for (uint8_t i = 0; i < adv_requirements->numUuids; i++)
        {
            uint16_t val = BYTES_TO_U16_BE(adv_requirements->uuids[i][0], adv_requirements->uuids[i][1]);
            uuid16[i] = (ble_uuid16_t) BLE_UUID16_INIT(val);
            IOT_LOGI(TAG, "Advertising 16-bit UUID[%d]: 0x%04X", i, val);
        }
        fields.uuids16           = uuid16;
        fields.num_uuids16       = adv_requirements->numUuids;
        fields.uuids16_is_complete = 1;
    }

    if (adv_requirements->manufactureData != NULL && adv_requirements->manufactureDataLen > 0)
    {
        mfg = Mem_SafeMalloc(adv_requirements->manufactureDataLen + 2, TAG, "adv mfg data");
        if (mfg == NULL)
        {
            SAFE_FREE(uuid16);
            return IOT_ERR_NO_MEM;
        }
        mfg[0] = adv_requirements->manufactureId & 0xFF;        /* company ID low byte */
        mfg[1] = (adv_requirements->manufactureId >> 8) & 0xFF; /* company ID high byte */
        memcpy(mfg + 2, adv_requirements->manufactureData, adv_requirements->manufactureDataLen);
        IOT_LOGI(TAG, "Adv productId:");
        LOG_HEX_INLINE(LOG_LEVEL_DEBUG, adv_requirements->manufactureData, adv_requirements->manufactureDataLen, TAG);
        fields.mfg_data = mfg;
        fields.mfg_data_len = adv_requirements->manufactureDataLen + 2;
    }

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to set advertising fields: %d", rc);
        SAFE_FREE(uuid16);
        SAFE_FREE(mfg);
        return IOT_ERR_FAIL;
    }

    /* The advert packet is full (21-26 of 31 bytes), so the device name goes in the
     * scan response instead. BLE_GAP_CONN_MODE_UND advertising is scannable. */
    if (adv_requirements->name != NULL && adv_requirements->nameLen > 0)
    {
        struct ble_hs_adv_fields rsp_fields;
        memset(&rsp_fields, 0, sizeof(rsp_fields));
        rsp_fields.name             = (const uint8_t *) adv_requirements->name;
        rsp_fields.name_len         = adv_requirements->nameLen;
        rsp_fields.name_is_complete = 1;

        IOT_LOGI(TAG, "Adv scan-response name: %.*s", adv_requirements->nameLen, adv_requirements->name);

        rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
        if (rc != 0)
        {
            IOT_LOGE(TAG, "Failed to set scan response fields: %d", rc);
            SAFE_FREE(uuid16);
            SAFE_FREE(mfg);
            return IOT_ERR_FAIL;
        }
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    uint8_t own_addr_type;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to infer address type: %d", rc);
        SAFE_FREE(uuid16);
        SAFE_FREE(mfg);
        return IOT_ERR_FAIL;
    }

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, internalGapCb, NULL);
    if (rc != 0)
    {
        SAFE_FREE(uuid16);
        SAFE_FREE(mfg);
        return IOT_ERR_FAIL;
    }

    /* Note: uuid16 and mfg must remain valid while advertising is active.
     * They will be freed when advertising stops or new fields are set. */
    return IOT_OK;
}
static iot_err_t nimble_stopAdvertising(void)
{
    int rc = ble_gap_adv_stop();
    if (rc != 0)
    {
        if (rc == BLE_HS_EALREADY)
        {
            IOT_LOGW(TAG, "BLE advertising already stopped");
            return IOT_OK;
        }
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
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

static iot_err_t nimble_notifyToCharacteristic(const char *addr, const uint8_t *charUuid, const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0)
    {
        IOT_LOGE(TAG, "Cannot notify: data is NULL or len is 0");
        return IOT_ERR_INVALID_ARG;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to set advertising fields: %d", rc);
        return IOT_ERR_FAIL;
    }

    uint16_t char_handle = 0;
    iot_err_t res = searchAttrhandleByUuid(charUuid, &char_handle);
    if (res != IOT_OK)
    {
        IOT_LOGE(TAG, "Failed to find characteristic handle");
        return res;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL)
    {
        IOT_LOGE(TAG, "Failed to create mbuf from flat buffer");
        return IOT_ERR_NO_MEM;
    }

    rc = ble_gattc_notify_custom(currConnectionHandle, char_handle, om);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to send notification: %d", rc);
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}
static void internalSyncCb(void)
{
    IOT_LOGI(TAG, "BLE host synchronized");
    if (callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE].isRegistered)
    {
        callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE].cb(
            callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE].arg, BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE, NULL);
    }
}

static void internalResetCb(int reason)
{
    IOT_LOGW(TAG, "BLE host reset, reason=%d", reason);
    if (callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_RESET].isRegistered)
    {
        callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_RESET].cb(
            callbackMgmt[BLE_MGMT_EVENT_STATUS_TYPE_RESET].arg, BLE_MGMT_EVENT_STATUS_TYPE_RESET, &reason);
    }
}
static iot_err_t nimble_isConnected(bool *isConnected)
{
    if (isConnected == NULL)
    {
        return IOT_ERR_INVALID_ARG;
    }
    *isConnected = isBleConnected;
    return IOT_OK;
}

static iot_err_t nimble_disconnect(void)
{
    if (!isBleConnected || currConnectionHandle == 0)
    {
        return IOT_ERR_INVALID_STATE;
    }
    /* 0x13 = remote-user connection terminate */
    return (ble_gap_terminate(currConnectionHandle, BLE_ERR_REM_USER_CONN_TERM) == 0) ? IOT_OK : IOT_ERR_FAIL;
}

static iot_err_t nimble_registerEventCallback(IOT_BleMgmtEventStatusType_t event, IOT_BleEventCb_t cb, void *arg)
{
    if (event >= BLE_MGMT_EVENT_STATUS_TYPE_MAX)
    {
        return IOT_ERR_INVALID_ARG;
    }
    /* callbackMgmt is torn down by nimble_deinit(). The abstract IOT_BleMgmt.c
     * layer only checks that a driver is registered, not that it is initialised,
     * so a caller reaching here right after a deinit (e.g. a partner's BLE
     * recovery path re-registering callbacks before IOT_BleInit()) must be
     * refused instead of indexing through a NULL array. */
    if (callbackMgmt == NULL)
    {
        return IOT_ERR_INVALID_STATE;
    }
    switch (event)
    {
    case BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE:
        ble_hs_cfg.sync_cb = internalSyncCb;
        callbackMgmt[event].cb = cb;
        callbackMgmt[event].isRegistered = true;
        callbackMgmt[event].arg = arg;
        break;
    case BLE_MGMT_EVENT_STATUS_TYPE_RESET:
        ble_hs_cfg.reset_cb = internalResetCb;
        callbackMgmt[event].cb = cb;
        callbackMgmt[event].isRegistered = true;
        callbackMgmt[event].arg = arg;
        break;
    case BLE_MGMT_EVENT_STATUS_TYPE_ADV_STOP:
    case BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT:
    case BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE:
    case BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED:
    case BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT:
    case BLE_MGMT_EVENT_STATUS_TYPE_ENABLE_STATUS:
    case BLE_MGMT_EVENT_STATUS_TYPE_CONNECTING:
    case BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECTING:
    case BLE_MGMT_EVENT_STATUS_TYPE_SEND_NOTIFICATION:
    case BLE_MGMT_EVENT_STATUS_TYPE_WRITE_REQUEST_STATUS:
        callbackMgmt[event].cb = cb;
        callbackMgmt[event].isRegistered = true;
        callbackMgmt[event].arg = arg;
        break;
    default:
        return IOT_ERR_INVALID_ARG;
    }

    return IOT_OK;
}

static iot_err_t nimble_unregisterEventCallback(IOT_BleMgmtEventStatusType_t event)
{
    if (event >= BLE_MGMT_EVENT_STATUS_TYPE_MAX)
    {
        return IOT_ERR_INVALID_ARG;
    }
    /* Same rationale as nimble_registerEventCallback(): callbackMgmt does not
     * survive a deinit. */
    if (callbackMgmt == NULL)
    {
        return IOT_ERR_INVALID_STATE;
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

static const IOT_BleDriver_t s_nimbleDriver = {
    .init                    = nimble_init,
    .deinit                  = nimble_deinit,
    .addServices             = nimble_addServices,
    .startAdvertising        = nimble_startAdvertising,
    .stopAdvertising         = nimble_stopAdvertising,
    .startBeaconAdvertising  = nimble_startBeaconAdvertising,
    .stopBeaconAdvertising   = nimble_stopBeaconAdvertising,
    .notifyToCharacteristic  = nimble_notifyToCharacteristic,
    .isConnected             = nimble_isConnected,
    .disconnect              = nimble_disconnect,
    .startScan               = nimble_startScan,
    .stopScan                = nimble_stopScan,
    .setDeviceName           = nimble_setDeviceName,
    .registerEventCallback   = nimble_registerEventCallback,
    .unregisterEventCallback = nimble_unregisterEventCallback,
};

void __attribute__((constructor)) IOT_BleMgmtNimbleRegister(void)
{
    IOT_BleRegisterDriver(&s_nimbleDriver);
}

#endif /* CONFIG_BT_NIMBLE_ENABLED */

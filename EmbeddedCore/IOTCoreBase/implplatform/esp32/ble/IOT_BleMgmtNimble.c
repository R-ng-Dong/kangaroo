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
#include "IOT_Osal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* How long nimble_ensureHostStarted() waits for controller sync, and how often it looks.
 * Sync is normally tens of ms; the ceiling is one only a broken controller reaches.
 *
 * Taken FROM the published IOT_BleStart() contract rather than chosen here: callers size
 * their own waits against that number (the advertiser arbiter's lock timeout is derived
 * from it), and two independently maintained constants would drift. Raising the ceiling
 * means raising IOT_BLE_START_MAX_BLOCK_MS, which is the point -- everything that has to
 * know then knows. */
#define NIMBLE_SYNC_TIMEOUT_MS IOT_BLE_START_MAX_BLOCK_MS
#define NIMBLE_SYNC_POLL_MS 10u
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
#include "ble_ext_adv_policy.h"
#include "mesh_adv_restore.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"

/* Our advertising sets must not be ids BLE Mesh drives. Caught at compile time so a
 * Kconfig change cannot quietly reintroduce the collision at runtime. */
#if MYNEWT_VAL(BLE_EXT_ADV) && defined(CONFIG_BLE_MESH_ADV_INST_ID)
_Static_assert(IOT_BLE_ADV_INST_CONN != CONFIG_BLE_MESH_ADV_INST_ID &&
                   IOT_BLE_ADV_INST_BEACON != CONFIG_BLE_MESH_ADV_INST_ID,
               "advertising instance collides with CONFIG_BLE_MESH_ADV_INST_ID");
#endif
#if MYNEWT_VAL(BLE_EXT_ADV) && defined(CONFIG_BLE_MESH_PROXY_ADV_INST_ID)
_Static_assert(IOT_BLE_ADV_INST_CONN != CONFIG_BLE_MESH_PROXY_ADV_INST_ID &&
                   IOT_BLE_ADV_INST_BEACON != CONFIG_BLE_MESH_PROXY_ADV_INST_ID,
               "advertising instance collides with CONFIG_BLE_MESH_PROXY_ADV_INST_ID");
#endif
#if MYNEWT_VAL(BLE_EXT_ADV)
_Static_assert(IOT_BLE_ADV_INST_CONN < BLE_ADV_INSTANCES && IOT_BLE_ADV_INST_BEACON < BLE_ADV_INSTANCES,
               "raise CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES to cover our advertising sets");

/* The controller hands out connection handles from the same activity budget that
 * advertising sets consume, and NimBLE indexes slave_conn[], g_max_tx_time[] and
 * g_max_rx_time[] in ble_gap.c by the raw handle. Those arrays are sized
 * CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1, with no bounds check, so a handle past the
 * end writes into whatever follows and the next disconnect faults in
 * ble_gap_conn_broken() (measured: 7 panics in 10 connect/disconnect cycles).
 *
 * With one legacy advertising set handles stayed at 0-1 and this never bit, which
 * is why most targets still ship MAX_CONNECTIONS=3 safely. Extended advertising
 * puts four sets in play and makes the whole range reachable -- so the constraint
 * is asserted here, where enabling EXT_ADV on a target sized for legacy becomes a
 * build error instead of a crash on the device. Raising MAX_CONNECTIONS on targets
 * that do not use EXT_ADV would cost RAM for a handle they can never be issued. */
#if defined(CONFIG_BT_CTRL_BLE_MAX_ACT)
_Static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1 >= CONFIG_BT_CTRL_BLE_MAX_ACT,
               "raise CONFIG_BT_NIMBLE_MAX_CONNECTIONS: extended advertising makes the "
               "controller's full handle range reachable, and NimBLE indexes its "
               "per-connection arrays by the raw handle");
#endif
#endif

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
/* BLE_HS_CONN_HANDLE_NONE (0xFFFF) means "no connection": 0 is a valid NimBLE handle, usually the
 * first connection's, so it cannot double as "none". */
static uint16_t currConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
static bool isBleConnected = false;

/* Persisted wrappers for services added via IOT_BleAddServices */
static struct ble_gatt_svc_def **persistent_services = NULL;
static size_t *persistent_services_sizes = NULL;
static size_t persistent_services_count = 0;

/* True only between a successful nimble_init() and nimble_deinit(). Guards entry
 * points that call into the NimBLE host so a caller hitting a torn-down stack
 * (e.g. OTA deinitializes BLE to free heap) gets an error instead of a crash. */
static bool s_nimbleInitialized = false;
/* Host task start is deferred until first use — see nimble_ensureHostStarted(). */
static bool s_hostStarted = false;
/* STARTED is not USABLE. nimble_port_freertos_init() returns as soon as the host task
 * exists, but the controller only hands the host an identity address later, when it
 * signals sync. Until then ble_hs_id_infer_auto() returns BLE_HS_ENOADDR (21) and every
 * advertise/scan fails. Written by the host task in internalSyncCb(), read by callers
 * waiting in nimble_ensureHostStarted(); volatile because those are different tasks. */
static volatile bool s_hostSynced = false;
static void internalSyncCb(void);
static iot_err_t nimble_ensureHostStarted(void);
static iot_err_t nimble_ensureHostStartedImplicit(const char *caller);

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

    /* A write NimBLE reassembled from several ACL fragments arrives as an mbuf CHAIN; om_len is
     * only the first link, and copying just that silently truncates the write. Take the packet
     * length and flatten the whole chain. */
    uint16_t pktLen = OS_MBUF_PKTLEN(ctxt->om);
    convertCtxt->om->data = Mem_SafeMalloc(pktLen, TAG, "operation data");
    if (convertCtxt->om->data == NULL)
    {
        goto cleanup_error;
    }
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, convertCtxt->om->data, pktLen, &copied) != 0 || copied != pktLen)
    {
        IOT_LOGE(TAG, "Failed to flatten %u-byte GATT write (copied %u)", (unsigned) pktLen, (unsigned) copied);
        goto cleanup_error;
    }
    convertCtxt->om->len = copied;

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
#if CONFIG_IOT_MESH_ADV_RESTORE && !MYNEWT_VAL(BLE_EXT_ADV)
static void advRestoreOnConnect(void);
#endif

static int internalGapCb(struct ble_gap_event *event, void *arg)
{
    if (event == NULL)
    {
        return 0;
    }

    IOT_BleMgmtEventStatusType_t evType = getEventTypeFromEspBleEvent(event->type);

#if CONFIG_IOT_MESH_ADV_RESTORE && !MYNEWT_VAL(BLE_EXT_ADV)
    /* A connection consumes the advert; the app re-advertises itself if it wants to. */
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status == 0)
    {
        advRestoreOnConnect();
    }
#endif

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
            /* Worth logging: the controller's activity budget (CONFIG_BT_CTRL_BLE_MAX_ACT)
             * is shared between connections AND advertising sets, so every extra
             * advertising instance pushes connection handles higher. NimBLE indexes
             * several per-connection arrays by this handle, sized
             * CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1 — a handle past that end corrupts
             * whatever follows. Keep CONFIG_BT_NIMBLE_MAX_CONNECTIONS above the advertising sets in use. */
            IOT_LOGI(TAG, "Connected, handle=%u", (unsigned) currConnectionHandle);
#ifdef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
            if (currConnectionHandle > CONFIG_BT_NIMBLE_MAX_CONNECTIONS)
            {
                /* NimBLE is about to corrupt its own memory. g_max_tx_time[],
                 * g_max_rx_time[], g_max_tx_octets[], g_max_rx_octets[] and
                 * slave_conn[] in ble_gap.c are all sized
                 * CONFIG_BT_NIMBLE_MAX_CONNECTIONS + 1 and indexed by this handle with
                 * no bounds check. Nothing fails now — the next disconnect faults
                 * walking whatever was overwritten. Say so while the cause is still
                 * on screen. */
                IOT_LOGE(TAG, "Connection handle %u exceeds CONFIG_BT_NIMBLE_MAX_CONNECTIONS (%d) — "
                              "NimBLE will write out of bounds; raise it to at least "
                              "CONFIG_BT_CTRL_BLE_MAX_ACT",
                         (unsigned) currConnectionHandle, CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
            }
#endif
            isBleConnected = true;
            callbackMgmt[evType].cb(callbackMgmt[evType].arg, evType, &conn_info);
        }
        break;
    }

    case BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT:
    {
        currConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
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
    iot_err_t hostErr = nimble_ensureHostStartedImplicit("startScan");
    if (hostErr != IOT_OK)
        return hostErr;

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
/* ── Advertising API compatibility shim ────────────────────────────────────────
 *
 * CONFIG_BLE_MESH_USE_BLE_50 selects CONFIG_BT_NIMBLE_EXT_ADV, and NimBLE then
 * compiles ble_gap_adv_set_data / _rsp_set_data / _start / _stop down to stubs
 * that return BLE_HS_ENOTSUP. Nothing fails to build and nothing crashes — the
 * device simply never advertises. So once mesh asks for BLE 5.0 the extended API
 * is mandatory, not optional.
 *
 * These two helpers hide that fork. Under EXT_ADV they drive an advertising set
 * with legacy_pdu = 1, so what leaves the antenna is the same ADV_IND /
 * ADV_NONCONN_IND as before and phones see no difference. Without EXT_ADV they
 * are a thin pass-through to the legacy calls.
 *
 * They take the raw AD bytes built by the shared IOT_BleAdvBuild*() builder, so
 * NimBLE puts on air exactly what Bluedroid and BlueZ do (Flags included), and
 * every path copies the bytes before returning, so callers keep them on the stack.
 *
 * Both return a NimBLE rc (0 = success), not an iot_err_t, so callers keep their
 * existing error handling and logging.
 */

#if MYNEWT_VAL(BLE_EXT_ADV)
/**
 * @brief Hand raw advertising bytes to one extended advertising set.
 *
 * ble_gap_ext_adv_set_data()/_rsp_set_data() take ownership of the mbuf on every
 * path, success or failure — freeing it here would be a double free.
 */
static int iotAdvSetData(uint8_t instance, const uint8_t *buf, uint8_t len, bool isScanRsp)
{
    /* The exact bytes handed to the controller. BlueZ merges every report from one
     * address into a single device, so a device advertising on two sets (ours plus
     * BLE Mesh's) cannot be read back field-by-field from a scan — this is the only
     * place the payload is unambiguous. */
    IOT_LOGD(TAG, "%s payload, %u bytes on instance %u:", isScanRsp ? "Scan-response" : "Advertising", len,
             instance);
    LOG_HEX_INLINE(LOG_LEVEL_DEBUG, buf, len, TAG);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (om == NULL)
    {
        IOT_LOGE(TAG, "Out of mbufs for %s data", isScanRsp ? "scan response" : "advertising");
        return BLE_HS_ENOMEM;
    }

    int rc = isScanRsp ? ble_gap_ext_adv_rsp_set_data(instance, om) : ble_gap_ext_adv_set_data(instance, om);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to set %s data on instance %u: %d", isScanRsp ? "scan response" : "advertising",
                 instance, rc);
    }
    return rc;
}
#endif /* MYNEWT_VAL(BLE_EXT_ADV) */

#if CONFIG_IOT_MESH_ADV_RESTORE && !MYNEWT_VAL(BLE_EXT_ADV)
/* BLE 4.x mesh hub, advertiser time-sharing, on stock IDF.
 *
 * On BLE 4.x there is one legacy advertising set. ESP-BLE-MESH takes it for EVERY packet
 * it sends (core/adv.c: bt_le_adv_start ... bt_le_adv_stop), preempting whatever we had on
 * air -- its own start stops a running advert on BLE_HS_EALREADY and retries -- and it
 * overwrites the advert DATA as well. Stock IDF has no hook to put our advert back, but core/adv.c and nimble_host/adapter.c are
 * separate objects, so `-Wl,--wrap=bt_le_adv_stop` intercepts the same call with no IDF
 * edit. The price is that the advert is absent for the length of each mesh packet; the
 * gain is that mesh never has to be silenced to protect it. */
static MeshAdvRestore_t s_advRestore;
#define ADV_RC_SLOT_TAKEN (-1)    /* NimBLE codes are >= 0, so this cannot collide */
#define ADV_RC_NOT_ON_RECORD (-2) /* a stop for the instance NOT holding the one BLE 4.x slot */
static portMUX_TYPE s_advRestoreLock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_advRestoreCount;

/* Serialises the three "apply" sequences below (mesh's post-stop restore, our own start,
 * our own stop) against EACH OTHER -- the portMUX above only makes each individual
 * s_advRestore read/write atomic, not a whole record-then-apply sequence. Without this,
 * two interleavings were observed:
 *   1. A stop can race a restore: the wrapper takes its snapshot (wanted=true) right
 *      before iotAdvStop() forgets the record and finds the radio already idle (mesh had
 *      just stopped it) -- iotAdvStop() returns success having touched nothing, and the
 *      wrapper then puts the just-stopped advert back on air.
 *   2. A start can race a restore: the wrapper reads its snapshot before iotAdvStart()
 *      records the NEW advert, so the wrapper's legacyAdvApply() reapplies the OLD
 *      payload after the start already applied the new one -- leaving the stale advert on
 *      air with nothing pointing at it, and the start call itself sees EALREADY and
 *      reports failure.
 * Lock order: this mutex first, then (implicitly) NimBLE's own host lock inside the
 * ble_gap_* calls made while it is held. It must never be taken while s_advRestoreLock is
 * held, and no NimBLE call is ever made while s_advRestoreLock is held either. */
static StaticSemaphore_t s_advRestoreApplyMutexBuf;
static SemaphoreHandle_t s_advRestoreApplyMutex = NULL;

static int legacyAdvApply(bool connectable, uint8_t ownAddrType, const uint8_t *adv, uint8_t advLen,
                          const uint8_t *rsp, uint8_t rspLen)
{
    int rc = ble_gap_adv_set_data(adv, advLen);
    if (rc != 0)
        return rc;
    /* A connectable advert is scannable: always write its scan response, empty when it has
     * none, or the controller keeps answering scans with the previous advert's response. */
    if (connectable)
    {
        rc = ble_gap_adv_rsp_set_data(rsp, rspLen);
        if (rc != 0)
            return rc;
    }
    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof(p));
    p.conn_mode = connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(ownAddrType, NULL, BLE_HS_FOREVER, &p, internalGapCb, NULL);
}

static void advRestoreOnConnect(void)
{
    portENTER_CRITICAL(&s_advRestoreLock);
    meshAdvRestoreOnConnect(&s_advRestore);
    portEXIT_CRITICAL(&s_advRestoreLock);
}

int __real_bt_le_adv_stop(void);

/* Normally runs on the mesh advertising task (any other caller of bt_le_adv_stop is
 * wrapped too). */
int __wrap_bt_le_adv_stop(void)
{
    int err = __real_bt_le_adv_stop();

    /* Not yet created (driver not initialised): nothing to restore against, and no
     * mutex to serialise with -- skip silently rather than race an uninitialised driver.
     * Likewise once the host is disabled (mid-teardown, e.g. nimble_deinit()'s
     * nimble_port_stop()): mesh can still emit a stray stop while the host is going down,
     * and every ble_gap_* call below would just fail and log noise for a restore nobody
     * needs any more. */
    if (s_advRestoreApplyMutex != NULL && ble_hs_is_enabled() &&
        xSemaphoreTake(s_advRestoreApplyMutex, portMAX_DELAY) == pdTRUE)
    {
        MeshAdvRestore_t snap;
        portENTER_CRITICAL(&s_advRestoreLock);
        snap = s_advRestore;
        portEXIT_CRITICAL(&s_advRestoreLock);

        /* Nothing active is the only safe moment: if mesh's stop was a no-op our advert may
         * still be up, and a second start would just fail with EALREADY. */
        if (meshAdvRestoreShouldApply(&snap, ble_gap_adv_active(), ble_gap_conn_active() != 0))
        {
            int rc = legacyAdvApply(snap.connectable, snap.ownAddrType, snap.adv, snap.advLen, snap.rsp,
                                    snap.rspLen);
            if (rc == 0)
            {
                /* A connection can complete between the snapshot above and the apply just
                 * done, clearing the record via advRestoreOnConnect() -- the advert we just
                 * put back is then stale (the app will re-advertise if it wants to). Take it
                 * back down rather than leave an advert on air with nothing pointing at it. */
                bool stillWanted;
                portENTER_CRITICAL(&s_advRestoreLock);
                stillWanted = s_advRestore.wanted;
                portEXIT_CRITICAL(&s_advRestoreLock);
                if (!stillWanted)
                {
                    (void) ble_gap_adv_stop();
                }
                else if ((++s_advRestoreCount % 50) == 1)
                {
                    IOT_LOGI(TAG, "advert restored after mesh TX (%lu so far)", (unsigned long) s_advRestoreCount);
                }
            }
            else
            {
                IOT_LOGW(TAG, "advert restore after mesh TX failed: %d", rc);
            }
        }
        xSemaphoreGive(s_advRestoreApplyMutex);
    }
    return err;
}
#endif /* CONFIG_IOT_MESH_ADV_RESTORE && !BLE_EXT_ADV */

/**
 * @brief Put one advert on air, over whichever advertising API this build has.
 *
 * @param instance    advertising set id; ignored on a legacy build, which has only one.
 * @param connectable true for ADV_IND, false for ADV_NONCONN_IND.
 * @param adv         raw advertising payload (AD elements), copied before return.
 * @param advLen      length of adv, at most IOT_BLE_ADV_MAX_LEN.
 * @param rsp         raw scan-response payload, or NULL. Connectable adverts only —
 *                    a non-connectable legacy PDU is not scannable.
 * @param rspLen      length of rsp; 0 = no scan response. A connectable advert always
 *                    writes its scan response, empty when rspLen is 0, so a previous
 *                    advert's response never stays on air.
 * @return NimBLE rc, 0 on success.
 */
static int iotAdvStart(uint8_t instance, bool connectable, const uint8_t *adv, uint8_t advLen, const uint8_t *rsp,
                       uint8_t rspLen)
{
    static const uint8_t noRsp[1] = {0};
    if (rsp == NULL)
    {
        rsp = noRsp; /* NimBLE copies from it even at length 0 */
        rspLen = 0;
    }

    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to infer address type: %d", rc);
        return rc;
    }

    IOT_BleExtAdvShape_t shape;
    IOT_BleExtAdvShapeFromConnectable(connectable, &shape);

#if MYNEWT_VAL(BLE_EXT_ADV)
    /* A running instance cannot be reconfigured; EALREADY just means it was idle. */
    (void) ble_gap_ext_adv_stop(instance);

    struct ble_gap_ext_adv_params params;
    memset(&params, 0, sizeof(params));
    params.legacy_pdu = shape.legacyPdu;
    params.connectable = shape.connectable;
    params.scannable = shape.scannable;
    params.own_addr_type = own_addr_type;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_1M;
    params.sid = instance;
    params.tx_power = 127; /* 127 = no preference, let the controller choose */

    rc = ble_gap_ext_adv_configure(instance, &params, NULL, internalGapCb, NULL);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to configure advertising instance %u: %d", instance, rc);
        return rc;
    }
    IOT_BleExtAdvMarkConfigured(instance);

    rc = iotAdvSetData(instance, adv, advLen, false);
    if (rc != 0)
    {
        return rc;
    }

    /* Scannable sets only (NimBLE refuses a response on the others). Written even when
     * empty: configuring a set does not clear the response its last advert left behind. */
    if (shape.scannable)
    {
        rc = iotAdvSetData(instance, rsp, rspLen, true);
        if (rc != 0)
        {
            return rc;
        }
    }

    rc = ble_gap_ext_adv_start(instance, 0 /* no duration limit */, 0 /* no event limit */);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to start advertising instance %u: %d", instance, rc);
    }
    return rc;
#else
#if CONFIG_IOT_MESH_ADV_RESTORE
    {
        /* Record before starting: mesh can preempt the very first advert. Held across the
         * whole record-apply-(forget on failure) sequence -- see s_advRestoreApplyMutex's
         * comment for the interleaving this closes if it is not. */
        bool advRestoreHeld = (s_advRestoreApplyMutex != NULL) &&
                              (xSemaphoreTake(s_advRestoreApplyMutex, portMAX_DELAY) == pdTRUE);

        MeshAdvRestoreResult_t res;
        uint8_t holder;
        portENTER_CRITICAL(&s_advRestoreLock);
        holder = s_advRestore.instance;
        res = meshAdvRestoreRecord(&s_advRestore, instance, shape.connectable, own_addr_type, adv, advLen, rsp,
                                   rspLen);
        portEXIT_CRITICAL(&s_advRestoreLock);
        if (res == MESH_ADV_RESTORE_SLOT_TAKEN)
        {
            if (advRestoreHeld)
                xSemaphoreGive(s_advRestoreApplyMutex);
            IOT_LOGW(TAG, "Advert instance %u refused: instance %u holds the only BLE 4.x advertising slot",
                     instance, holder);
            return ADV_RC_SLOT_TAKEN;
        }
        if (res != MESH_ADV_RESTORE_OK)
        {
            if (advRestoreHeld)
                xSemaphoreGive(s_advRestoreApplyMutex);
            IOT_LOGE(TAG, "Advert instance %u not recorded: bad payload", instance);
            return BLE_HS_EINVAL;
        }

        (void) ble_gap_adv_stop(); /* ours or mesh's -- either way the set is being replaced */
        rc = legacyAdvApply(shape.connectable, own_addr_type, adv, advLen, rsp, rspLen);
        if (rc == BLE_HS_EALREADY)
        {
            /* Mesh's own (unwrapped) bt_le_adv_start won the race between our stop above and
             * our start here -- while we hold s_advRestoreApplyMutex, only mesh can have taken
             * the set. The record just written is still correct, so keep it: mesh's next stop
             * restores it through the wrapper, and reporting failure here would make the caller
             * (e.g. the one-shot recovery-advert restart after a phone disconnect) give up on an
             * advert that is about to come back on its own. */
            IOT_LOGD(TAG, "Advert instance %u start raced mesh's own start (EALREADY); record kept, "
                          "mesh's next stop will restore it",
                     instance);
            rc = 0;
        }
        else if (rc != 0)
        {
            IOT_LOGE(TAG, "Failed to start advertising: %d", rc);
            portENTER_CRITICAL(&s_advRestoreLock);
            (void) meshAdvRestoreForget(&s_advRestore, instance);
            portEXIT_CRITICAL(&s_advRestoreLock);
        }
        if (advRestoreHeld)
            xSemaphoreGive(s_advRestoreApplyMutex);
        return rc;
    }
#else
    (void) instance;
#endif

    rc = ble_gap_adv_set_data(adv, advLen);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to set advertising data: %d", rc);
        return rc;
    }

    /* Connectable = scannable: always write the response, empty when there is none, or the
     * controller keeps the previous advert's response. */
    if (shape.connectable)
    {
        rc = ble_gap_adv_rsp_set_data(rsp, rspLen);
        if (rc != 0)
        {
            IOT_LOGE(TAG, "Failed to set scan response data: %d", rc);
            return rc;
        }
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = shape.connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, internalGapCb, NULL);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to start advertising: %d", rc);
    }
    return rc;
#endif
}

/**
 * @brief Take one advert off air.
 * @return NimBLE rc; BLE_HS_EALREADY if it was not advertising. Under
 *         CONFIG_IOT_MESH_ADV_RESTORE, a stop for the instance NOT holding the one BLE 4.x
 *         slot returns ADV_RC_NOT_ON_RECORD instead -- same "already stopped" meaning, so
 *         callers still map it to success, but it is routine (one advert at a time is the
 *         rule) rather than a WARN-worthy surprise.
 */
static int iotAdvStop(uint8_t instance)
{
#if MYNEWT_VAL(BLE_EXT_ADV)
    /* Keep the legacy contract for "there was nothing to stop". ble_gap_adv_stop()
     * answered BLE_HS_EALREADY, which callers treat as success; the extended API
     * answers BLE_HS_EINVAL for an instance it never configured, which they treat as
     * a failure. Reachable on any provisioned boot whose WiFi stayed up: nothing ever
     * advertised, and an OTA still tears BLE down and stops advertising on the way. */
    if (!IOT_BleExtAdvIsConfigured(instance))
    {
        return BLE_HS_EALREADY;
    }
    return ble_gap_ext_adv_stop(instance);
#else
#if CONFIG_IOT_MESH_ADV_RESTORE
    /* Held from the forget through the actual stop -- see s_advRestoreApplyMutex's
     * comment for the interleaving this closes if it is not. */
    bool advRestoreHeld = (s_advRestoreApplyMutex != NULL) &&
                          (xSemaphoreTake(s_advRestoreApplyMutex, portMAX_DELAY) == pdTRUE);
    bool wasOurs;
    portENTER_CRITICAL(&s_advRestoreLock);
    wasOurs = meshAdvRestoreForget(&s_advRestore, instance);
    portEXIT_CRITICAL(&s_advRestoreLock);
    if (!wasOurs)
    {
        if (advRestoreHeld)
            xSemaphoreGive(s_advRestoreApplyMutex);
        /* Not ours on air: leave whatever is (our other advert, or mesh's packet) alone. This
         * is the ordinary shape of "stop the beacon while the connectable advert holds the
         * slot" (or vice versa) -- not a surprise worth a WARN every time, unlike a genuine
         * EALREADY from the radio. Callers still see the legacy BLE_HS_EALREADY-is-success
         * contract; only the log level differs. */
        return ADV_RC_NOT_ON_RECORD;
    }
    {
        int rc = ble_gap_adv_stop();
        if (advRestoreHeld)
            xSemaphoreGive(s_advRestoreApplyMutex);
        return rc;
    }
#else
    (void) instance;
    return ble_gap_adv_stop();
#endif
#endif
}

static iot_err_t nimble_startBeaconAdvertising(IOT_BleMgmtAdvData_t *adv_requirements)
{
    iot_err_t hostErr = nimble_ensureHostStartedImplicit("startBeaconAdvertising");
    if (hostErr != IOT_OK)
        return hostErr;

    /* Flags, 16-bit service UUIDs, then manufacturer data: the same order NimBLE's field
     * encoder used, so the beacon's bytes are unchanged. Non-connectable, so no scan response. */
    uint8_t adv[IOT_BLE_ADV_MAX_LEN];
    uint8_t advLen = 0;
    iot_err_t err = IOT_BleAdvBuildPayload(adv_requirements, adv, sizeof(adv), &advLen);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Beacon advertising payload does not fit: %d", err);
        return IOT_ERR_INVALID_ARG;
    }

    int rc = iotAdvStart(IOT_BLE_ADV_INST_BEACON, false /* non-connectable */, adv, advLen, NULL, 0);

#if CONFIG_IOT_MESH_ADV_RESTORE && !MYNEWT_VAL(BLE_EXT_ADV)
    if (rc == ADV_RC_SLOT_TAKEN)
        return IOT_ERR_INVALID_STATE;
#endif
    return (rc == 0) ? IOT_OK : IOT_ERR_FAIL;
}
static iot_err_t nimble_stopBeaconAdvertising(void)
{
    int rc = iotAdvStop(IOT_BLE_ADV_INST_BEACON);
    if (rc != 0)
    {
#if CONFIG_IOT_MESH_ADV_RESTORE && !MYNEWT_VAL(BLE_EXT_ADV)
        if (rc == ADV_RC_NOT_ON_RECORD)
        {
            /* Routine under time-sharing: the connectable advert holds the one slot, not
             * the beacon. Still the requested end state, so it is success -- just not
             * worth a WARN every time it happens. */
            IOT_LOGD(TAG, "BLE beacon advertising already stopped (not the one time-shared with mesh)");
            return IOT_OK;
        }
#endif
        if (rc == BLE_HS_EALREADY)
        {
            /* Nothing was advertising. That is the requested end state, so it is
             * success — and it is what nimble_stopAdvertising() has always answered.
             * This path used to disagree, which made a stop that had already happened
             * (or never needed to) look like a driver failure. */
            IOT_LOGW(TAG, "BLE beacon advertising already stopped");
            return IOT_OK;
        }
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
#if CONFIG_IOT_MESH_ADV_RESTORE && !MYNEWT_VAL(BLE_EXT_ADV)
    /* Created once, before anything can advertise, and never destroyed -- deinit/reinit
     * (e.g. around an OTA) reuses the same mutex rather than recreating it. */
    if (s_advRestoreApplyMutex == NULL)
    {
        s_advRestoreApplyMutex = xSemaphoreCreateMutexStatic(&s_advRestoreApplyMutexBuf);
    }

    /* Link-time guard: without -Wl,--wrap=bt_le_adv_stop (mesh_adv_restore.cmake) this is
     * an undefined reference, so a board that forgot the include fails to link instead of
     * silently shipping a hub whose advert is never restored.
     *
     * This has to be a `volatile` STORE done as an ordinary statement inside nimble_init()
     * itself, not a separate (even `static`+`used`) variable: a separate variable gets its
     * own section under -fdata-sections, and if the compiler proves nothing ever reads it
     * (a plain `(void)x;` cast on an optimised build proves exactly that and elides the
     * load entirely) nimble_init's own machine code never actually references it, so
     * --gc-sections drops that unreferenced section -- taking the guard's reference to
     * __real_bt_le_adv_stop with it. A `volatile` local forces a real store instruction,
     * in nimble_init's own function body, that the optimiser cannot remove; nimble_init is
     * reached through the registered driver struct, so its own code is always linked in. */
    void *volatile wrapGuard = (void *) __real_bt_le_adv_stop;
    (void) wrapGuard;
#endif

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
    /* Register the sync callback HERE, not when an application asks for INIT_DONE.
     * The driver needs to know the host has synced for its own sake -- until then the
     * controller has given it no identity address and every advertise/scan fails with
     * BLE_HS_ENOADDR. Forwarding that event on to an application callback is a separate,
     * optional concern, handled inside internalSyncCb().
     *
     * Until 2026-09-15 this line lived ONLY in nimble_registerEventCallback()'s
     * INIT_DONE case, so on any path where nothing registered that callback,
     * ble_hs_cfg.sync_cb stayed NULL and the driver never learned the host was up. */
    ble_hs_cfg.sync_cb = internalSyncCb;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_ans_init();

    /* The host is deliberately NOT started here — see nimble_ensureHostStarted(). */
    s_nimbleInitialized = true;
    return IOT_OK;
}

/**
 * @brief Start the NimBLE host task, once, on first use.
 *
 * NimBLE's documented lifecycle is: init -> register every GATT service ->
 * START THE HOST. ble_hs_start() calls ble_gatts_start(), which registers all
 * pending service definitions and then frees the pending list. Starting the
 * host inside nimble_init() broke that order: services registered afterwards
 * (ours arrive during IOT_CoreInit, well after IOT_InitPlatform) had missed the
 * only ble_gatts_start(), so the driver had to fall back on
 * ble_gatts_add_dynamic_svcs() and CONFIG_BT_NIMBLE_DYNAMIC_SERVICE.
 *
 * That fallback is what made BLE Mesh unusable on NimBLE. esp_ble_mesh_init()
 * calls ble_gatts_start() itself, and with BLE_DYNAMIC_SERVICE enabled a second
 * call re-mallocs ble_gatts_svc_entry_mem and re-inits ble_gatts_svc_entry_pool
 * onto the new buffer — orphaning every entry already allocated, so ATT entries
 * point at dead memory and ble_uuid_cmp faults on garbage (observed constant:
 * 0x00020001). Verified on hardware: with DYNAMIC_SERVICE off,
 * esp_ble_mesh_init() returns 0 and the device keeps advertising. That crash was
 * recorded for a year as "mesh requires Bluedroid"; it was this instead.
 *
 * Deferring the host start restores the documented order, lets services register
 * statically, and leaves a second ble_gatts_start() from mesh harmless.
 *
 * Every entry point that needs a running host calls this first. Registering
 * services does not, which is exactly the point.
 */

/**
 * @brief Block until the controller reports sync, or give up.
 *
 * Factored out of nimble_ensureHostStarted() so a first start and a retry of a stuck
 * start share one wait loop rather than two copies drifting apart.
 *
 * Found 2026-09-15: on ESP32 classic the first advert ran ~20 ms after the host start
 * and failed, which aborted SDK init entirely — so a factory-reset device could never be
 * re-provisioned. The C3 happened to win the race, which is why it went unseen. Sync is
 * typically tens of ms; the budget is deliberately far larger, because a slow sync must
 * not be fatal.
 *
 * Safe to block here: the host runs on its OWN task (nimble_port_freertos_init()), so
 * this never waits on itself.
 */
static iot_err_t nimble_waitForHostSync(void)
{
    for (uint32_t waitedMs = 0; !s_hostSynced && waitedMs < NIMBLE_SYNC_TIMEOUT_MS;
         waitedMs += NIMBLE_SYNC_POLL_MS)
    {
        IOT_OsalDelay(NIMBLE_SYNC_POLL_MS);
    }
    if (!s_hostSynced)
    {
        IOT_LOGE(TAG, "BLE host did not synchronize within %u ms", (unsigned) NIMBLE_SYNC_TIMEOUT_MS);
        return IOT_ERR_TIMEOUT;
    }
    return IOT_OK;
}

static iot_err_t nimble_ensureHostStarted(void)
{
    if (!s_nimbleInitialized)
    {
        IOT_LOGE(TAG, "BLE host requested before init");
        return IOT_ERR_INVALID_STATE;
    }
    if (s_hostStarted)
    {
        if (s_hostSynced)
        {
            return IOT_OK;
        }
        /* s_hostStarted only records that a start was ATTEMPTED, not that it worked.
         * esp_nimble_enable() (inside nimble_port_freertos_init()) does not surface
         * xTaskCreate()'s return value, so a heap-starved first attempt can leave no
         * host task running at all while still having set s_hostStarted — and a
         * genuinely slow (but alive) sync also lands here. Treating "started" as
         * "ready" without checking s_hostSynced was exactly the bug: every later
         * IOT_BleStart() returned IOT_OK regardless, and the mesh hub reported READY
         * over a radio that was never listening (found 2026-09-17, review of the
         * deferred mesh radio start). Retry the wait rather than declare success. */
        if (xTaskGetHandle("nimble_host") == NULL)
        {
            /* The host task itself was never created — the earlier start could not
             * have succeeded no matter how long we wait. Clear the flag so this call
             * falls through to a real nimble_port_freertos_init() retry below instead
             * of polling for a sync that will never come. */
            IOT_LOGW(TAG, "BLE host task missing after a previous start — retrying");
            s_hostStarted = false;
        }
        else
        {
            return nimble_waitForHostSync();
        }
    }

    nimble_port_freertos_init(blecent_host_task);
    s_hostStarted = true;

    /* esp_nimble_enable() (inside nimble_port_freertos_init()) does not surface
     * xTaskCreate()'s return value, so a heap-starved boot can silently leave no host
     * task running at all. Checking right here -- instead of only after
     * NIMBLE_SYNC_TIMEOUT_MS of polling for a sync that can never arrive -- is what
     * turns that into a fast, diagnosable failure.
     *
     * Measured on hardware: the mesh hub
     * started the host from the core work task at the cloud's worst heap moment
     * (subscriptions still confirming), the task was never created, and the caller
     * still blocked the core work task for the full 3022 ms sync wait before giving
     * up -- every queued cloud message queued behind it. Failing immediately here
     * means a caller on that task is blocked only for as long as the failed
     * xTaskCreate() itself took, not the sync timeout on top of it. */
    if (xTaskGetHandle("nimble_host") == NULL)
    {
        IOT_LOGE(TAG, "BLE host task could not be created (free heap %u, largest free block %u) "
                      "-- not waiting for a sync that can never arrive",
                 (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        s_hostStarted = false;
        return IOT_ERR_NO_MEM;
    }

    IOT_LOGI(TAG, "NimBLE host started (services registered first)");

    /* Wait for the controller to sync before returning. Callers treat this function as
     * "the host is ready"; without the wait they advertise into BLE_HS_ENOADDR. */
    return nimble_waitForHostSync();
}

/**
 * @brief IOT_BleStart() — explicit host activation. See IOT_BleMgmt.h.
 *
 * The same work nimble_ensureHostStarted() does, under a name that says so.
 * Idempotent.
 */
static iot_err_t nimble_start(void)
{
    return nimble_ensureHostStarted();
}

/**
 * @brief Fallback for callers that advertise or scan without calling IOT_BleStart().
 *
 * Starts the host anyway so older integrations keep working, but says so: a
 * lifecycle transition happening inside startAdvertising()/startScan() is exactly
 * the kind of hidden side effect that makes a host failure look like an
 * advertising failure.
 */
static iot_err_t nimble_ensureHostStartedImplicit(const char *caller)
{
    if (!s_hostStarted && s_nimbleInitialized)
    {
        IOT_LOGW(TAG, "%s started the BLE host implicitly — call IOT_BleStart() after "
                      "IOT_BleAddServices() instead", caller);
    }
    return nimble_ensureHostStarted();
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
    s_hostStarted = false;
    s_hostSynced = false;
#if CONFIG_IOT_MESH_ADV_RESTORE && !MYNEWT_VAL(BLE_EXT_ADV)
    /* A stack brought back up is a new session with nothing on air -- a record that
     * survived here would make the wrapper try to restore an advert whose payload,
     * connection state and even instance id may no longer mean anything. Mirrors the
     * IOT_BleExtAdvForgetAll() call below for the extended-advertising latches. */
    if (s_advRestoreApplyMutex != NULL && xSemaphoreTake(s_advRestoreApplyMutex, portMAX_DELAY) == pdTRUE)
    {
        portENTER_CRITICAL(&s_advRestoreLock);
        memset(&s_advRestore, 0, sizeof(s_advRestore));
        portEXIT_CRITICAL(&s_advRestoreLock);
        xSemaphoreGive(s_advRestoreApplyMutex);
    }
#endif
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
    /* The advertising sets went with the host. A stack brought back up is a new
     * session with nothing configured on it, so a latch that survived here would make
     * the driver skip ble_gap_ext_adv_configure() and advertise nothing at all. */
    IOT_BleExtAdvForgetAll();
    numberOfGattServices = 0;
    currConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
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

    /* No ble_svc_gatt_init()/ble_svc_ans_init() here: nimble_init() already did both,
     * and a second call is now fatal. They are sysinit functions — ble_svc_gatt_init()
     * opens with SYSINIT_ASSERT_ACTIVE(), which panics once the host is running. That
     * became reachable when BLE Mesh started the host during its own init: the next
     * IOT_BleAddServices() (the change-WiFi recovery advert bringing up its GATT
     * service) landed here with the host already up and paniced the device. */
    int rc;

    rc = ble_gatts_count_cfg(services_local);
    if (rc != 0)
    {
        rc = IOT_ERR_FAIL;
        goto fail_cleanup;
    }

    /* Static registration, NOT ble_gatts_add_dynamic_svcs(). These go on the
     * pending list and are registered by the next ble_gatts_start(), which
     * nimble_ensureHostStarted() triggers once every service is in. That keeps
     * CONFIG_BT_NIMBLE_DYNAMIC_SERVICE off, which is what makes BLE Mesh
     * survivable — see the comment on nimble_ensureHostStarted(). */
    rc = ble_gatts_add_svcs(services_local);
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
    iot_err_t hostErr = nimble_ensureHostStartedImplicit("startAdvertising");
    if (hostErr != IOT_OK)
        return hostErr;

    if (!s_nimbleInitialized)
    {
        IOT_LOGE(TAG, "startAdvertising called while NimBLE is deinitialized — refusing");
        return IOT_ERR_INVALID_STATE;
    }
    /* Ensure local MTU is set (may have been altered by BLE mesh stack) */
    ble_att_set_preferred_mtu(256);

    /* The advert packet is nearly full, so the device name goes in the scan response.
     * A connectable undirected advert is scannable. */
    uint8_t adv[IOT_BLE_ADV_MAX_LEN];
    uint8_t advLen = 0;
    iot_err_t err = IOT_BleAdvBuildPayload(adv_requirements, adv, sizeof(adv), &advLen);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Advertising payload does not fit: %d", err);
        return IOT_ERR_INVALID_ARG;
    }

    uint8_t rsp[IOT_BLE_ADV_MAX_LEN];
    uint8_t rspLen = 0;
    err = IOT_BleAdvBuildScanRsp(adv_requirements, rsp, sizeof(rsp), &rspLen);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "Scan response payload does not fit: %d", err);
        return IOT_ERR_INVALID_ARG;
    }

    /* The exact bytes on air, one line per packet: BlueZ merges the advert and the scan
     * response into one device, so a scan cannot tell which field came from which. */
    IOT_LOGI(TAG, "Adv payload (%u B):", (unsigned) advLen);
    LOG_HEX_INLINE(LOG_LEVEL_INFO, adv, advLen, TAG);
    if (rspLen > 0)
    {
        IOT_LOGI(TAG, "Adv scan response (%u B):", (unsigned) rspLen);
        LOG_HEX_INLINE(LOG_LEVEL_INFO, rsp, rspLen, TAG);
    }
    else
    {
        IOT_LOGI(TAG, "Adv scan response: empty");
    }

    int rc = iotAdvStart(IOT_BLE_ADV_INST_CONN, true /* connectable */, adv, advLen, (rspLen > 0) ? rsp : NULL,
                         rspLen);

#if CONFIG_IOT_MESH_ADV_RESTORE && !MYNEWT_VAL(BLE_EXT_ADV)
    if (rc == ADV_RC_SLOT_TAKEN)
        return IOT_ERR_INVALID_STATE;
#endif
    return (rc == 0) ? IOT_OK : IOT_ERR_FAIL;
}
static iot_err_t nimble_stopAdvertising(void)
{
    int rc = iotAdvStop(IOT_BLE_ADV_INST_CONN);
    if (rc != 0)
    {
#if CONFIG_IOT_MESH_ADV_RESTORE && !MYNEWT_VAL(BLE_EXT_ADV)
        if (rc == ADV_RC_NOT_ON_RECORD)
        {
            /* Routine under time-sharing: the beacon holds the one slot, not the
             * connectable advert. Still the requested end state, so it is success -- just
             * not worth a WARN every time it happens. */
            IOT_LOGD(TAG, "BLE advertising already stopped (not the one time-shared with mesh)");
            return IOT_OK;
        }
#endif
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

    /* This used to clear the advertising payload before every notification — a
     * leftover that had nothing to do with notifying, and that would wipe the
     * provisioning advert of a device the app is still talking to. */
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

    int rc = ble_gatts_notify_custom(currConnectionHandle, char_handle, om);
    if (rc != 0)
    {
        IOT_LOGE(TAG, "Failed to send notification: %d", rc);
        return IOT_ERR_FAIL;
    }
    return IOT_OK;
}
static void internalSyncCb(void)
{
    s_hostSynced = true;
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
    if (!isBleConnected || currConnectionHandle == BLE_HS_CONN_HANDLE_NONE)
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
        /* ble_hs_cfg.sync_cb is wired in nimble_init() -- the driver needs it whether or
         * not anyone registers here. This only records who to forward the event to. */
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
    .start                   = nimble_start,
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

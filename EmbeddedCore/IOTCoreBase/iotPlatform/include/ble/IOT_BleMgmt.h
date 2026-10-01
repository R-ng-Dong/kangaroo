/**
 * @file IOT_BleMgmt.h
 * @brief BLE Management API - Platform Abstraction Layer
 *
 * This file provides a platform-independent API for Bluetooth Low Energy (BLE) operations.
 * Implementations for specific platforms (ESP32, nRF52, Linux, etc.) must implement
 * all functions defined in this header.
 *
 * @note This is an abstraction layer. The actual BLE stack (NimBLE, SoftDevice, BlueZ)
 *       is hidden behind these APIs.
 */

#pragma once

#include "IOT_ErrorManager.h"
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

/* ============================
 *  GATT CONSTANTS
 * ============================ */

/** @defgroup GATT_Service_Types GATT Service Types
 *  @{
 */
#define IOT_GATT_SERVICE_TYPE_PRIMARY 0x01   /**< Primary GATT service */
#define IOT_GATT_SERVICE_TYPE_SECONDARY 0x02 /**< Secondary GATT service */
/** @} */

/** @defgroup GATT_Char_Properties GATT Characteristic Properties
 *  @brief Flags that define characteristic permissions and capabilities
 *  @{
 */
#define IOT_BLE_GATT_CHR_F_BROADCAST 0x0001       /**< Broadcast property */
#define IOT_BLE_GATT_CHR_F_READ 0x0002            /**< Read property */
#define IOT_BLE_GATT_CHR_F_WRITE_NO_RSP 0x0004    /**< Write without response */
#define IOT_BLE_GATT_CHR_F_WRITE 0x0008           /**< Write with response */
#define IOT_BLE_GATT_CHR_F_NOTIFY 0x0010          /**< Notify property */
#define IOT_BLE_GATT_CHR_F_INDICATE 0x0020        /**< Indicate property */
#define IOT_BLE_GATT_CHR_F_AUTH_SIGN_WRITE 0x0040 /**< Authenticated signed write */
#define IOT_BLE_GATT_CHR_F_RELIABLE_WRITE 0x0080  /**< Reliable write */
#define IOT_BLE_GATT_CHR_F_AUX_WRITE 0x0100       /**< Auxiliary write */
#define IOT_BLE_GATT_CHR_F_READ_ENC 0x0200        /**< Read with encryption required */
#define IOT_BLE_GATT_CHR_F_READ_AUTHEN 0x0400     /**< Read with authentication required */
#define IOT_BLE_GATT_CHR_F_READ_AUTHOR 0x0800     /**< Read with authorization required */
#define IOT_BLE_GATT_CHR_F_WRITE_ENC 0x1000       /**< Write with encryption required */
#define IOT_BLE_GATT_CHR_F_WRITE_AUTHEN 0x2000    /**< Write with authentication required */
#define IOT_BLE_GATT_CHR_F_WRITE_AUTHOR 0x4000    /**< Write with authorization required */
/** @} */

/** @defgroup GATT_Access_Ops GATT Access Operations
 *  @brief Operation types for GATT attribute access callbacks
 *  @{
 */
#define IOT_BLE_GATT_ACCESS_OP_READ_CHR 0  /**< Read characteristic value */
#define IOT_BLE_GATT_ACCESS_OP_WRITE_CHR 1 /**< Write characteristic value */
#define IOT_BLE_GATT_ACCESS_OP_READ_DSC 2  /**< Read descriptor value */
#define IOT_BLE_GATT_ACCESS_OP_WRITE_DSC 3 /**< Write descriptor value */
/** @} */

/* ============================
 *  TYPE DEFINITIONS
 * ============================ */

/** @brief Forward declaration of GATT access context */
struct IOT_BleMgmtGattAccessContext_t;

/**
 * @brief GATT attribute access callback function type
 * @param ctxt Pointer to the access context containing operation details
 * @param arg User-defined argument passed during characteristic/descriptor registration
 * @return 0 on success, negative value on error
 *
 * @note This callback is invoked when a peer device reads or writes to a characteristic/descriptor
 * @note Implementation should handle both read and write operations based on ctxt->op
 */
typedef int IOT_BleMgmtGattAttrAccessCb_t(struct IOT_BleMgmtGattAccessContext_t *ctxt, void *arg);

/**
 * @brief Type for characteristic flags
 */
typedef uint16_t IOT_BleMgmtGattCharFlags_t;

/**
 * @brief GATT Descriptor definition structure
 */
typedef struct
{
    uint8_t *uuid;                            /**< Pointer to descriptor UUID (2, 4, or 16 bytes) */
    uint8_t uuid_len;                         /**< Length of UUID in bytes */
    uint8_t att_flags;                        /**< Attribute flags (e.g., READ, WRITE permissions) */
    IOT_BleMgmtGattAttrAccessCb_t *access_cb; /**< Access callback for read/write operations */
    void *arg;                                /**< User argument passed to access callback */
} IOT_BleMgmtGattDesc_t;

/**
 * @brief GATT Characteristic definition structure
 */
typedef struct
{
    uint8_t uuid_len;                         /**< Length of UUID in bytes (2, 4, or 16) */
    uint8_t *uuid;                            /**< Pointer to characteristic UUID */
    IOT_BleMgmtGattAttrAccessCb_t *access_cb; /**< Access callback for read/write operations */
    void *arg;                                /**< User argument passed to access callback */
    IOT_BleMgmtGattDesc_t *descriptor;       /**< Pointer to descriptor array (NULL-terminated) */
    IOT_BleMgmtGattCharFlags_t flags;         /**< Characteristic properties (READ, WRITE, NOTIFY, etc.) */
} IOT_BleMgmtGattChar_t;

/**
 * @brief GATT Service definition structure
 */
typedef struct
{
    uint8_t service_index;                         /**< Service index (for internal tracking) */
    uint8_t type;                                  /**< Service type (PRIMARY or SECONDARY) */
    uint8_t uuid_len;                              /**< Length of UUID in bytes (2, 4, or 16) */
    uint8_t *uuid;                                 /**< Pointer to service UUID */
    const IOT_BleMgmtGattChar_t *characteristics; /**< Pointer to characteristic array (NULL-terminated) */
} IOT_BleMgmtGattServ_t;

/**
 * @brief Data buffer structure for GATT operations
 */
typedef struct
{
    uint8_t *data; /**< Pointer to data buffer */
    uint16_t len;  /**< Length of data in bytes */
} uint_data_t;

/**
 * @brief GATT Access Context structure
 * @note Passed to attribute access callbacks to provide operation details
 */
struct IOT_BleMgmtGattAccessContext_t
{
    uint8_t op;        /**< Access operation type (READ_CHR, WRITE_CHR, READ_DSC, WRITE_DSC) */
    uint_data_t *om;   /**< Buffer containing data for read/write operation */

    union
    {
        IOT_BleMgmtGattChar_t *characteristic; /**< Characteristic being accessed (for chr operations) */
        IOT_BleMgmtGattDesc_t *descriptor;     /**< Descriptor being accessed (for desc operations) */
    };
};

/* ============================
 *  EVENT SYSTEM
 * ============================ */

/**
 * @brief BLE Management Event Types
 * @note These events are platform-independent. Implementation must map platform-specific
 *       events to these types.
 */
typedef enum
{
    BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE = 0, /**< BLE stack initialization completed */
    BLE_MGMT_EVENT_STATUS_TYPE_RESET,         /**< BLE stack reset occurred */
    BLE_MGMT_EVENT_STATUS_TYPE_ADV_STOP,      /**< Advertising stopped */
    BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT,   /**< Scan result received */
    BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE, /**< Scan completed */
    BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED,     /**< Device connected */
    BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT,    /**< Device disconnected */
    BLE_MGMT_EVENT_STATUS_TYPE_ENABLE_STATUS, /**< Notification/Indication enabled/disabled by peer */
    BLE_MGMT_EVENT_STATUS_TYPE_CONNECTING,           /**< Connection in progress */
    BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECTING,        /**< Disconnection in progress */
    BLE_MGMT_EVENT_STATUS_TYPE_SEND_NOTIFICATION,    /**< Notification sent to peer */
    BLE_MGMT_EVENT_STATUS_TYPE_WRITE_REQUEST_STATUS, /**< Write request status */
    BLE_MGMT_EVENT_STATUS_TYPE_MAX                    /**< Maximum value (for array sizing) */
} IOT_BleMgmtEventStatusType_t;

/**
 * @brief Connection event data structure
 */
typedef struct
{
    int status; /**< Connection status: 0 for success, non-zero for error code */
} IOT_BleMgmtEventConnectData_t;

/**
 * @brief Disconnection event data structure
 */
typedef struct
{
    int reason; /**< Disconnection reason code */
} IOT_BleMgmtEventDisconnectData_t;

/**
 * @brief Scan result event data structure
 */
typedef struct
{
    uint8_t addr[6];   /**< Device MAC address (6 bytes) */
    int8_t rssi;       /**< Received Signal Strength Indicator */
    uint8_t adv_type;  /**< Advertisement type */
    uint8_t addr_type; /**< Address type (public, random, etc.) */
    uint8_t *data;     /**< Pointer to advertisement data */
    uint8_t data_len;  /**< Length of advertisement data */
} IOT_BleMgmtEventScanResultData_t;

/**
 * @brief Notification/Indication enable/disable event data structure
 */
typedef struct
{
    bool success; /**< true if notification enabled, false if disabled */
} IOT_BleMgmtEventNotifyIndicateEnable_t;

/**
 * @brief Notification send status data structure
 */
typedef struct
{
    bool success; /**< true if notification sent successfully, false otherwise */
} IOT_BleMgmtNotifySendData_t;

/**
 * @brief BLE Address Types
 */
typedef enum
{
    IOT_BLE_ADDR_TYPE_PUBLIC,     /**< Public device address */
    IOT_BLE_ADDR_TYPE_RANDOM,     /**< Random device address */
    IOT_BLE_ADDR_TYPE_RPA_PUBLIC, /**< Resolvable Private Address (public identity) */
    IOT_BLE_ADDR_TYPE_RPA_RANDOM, /**< Resolvable Private Address (random identity) */
} IOT_BleMgmtAddrType_t;

/**
 * @brief BLE Scan Types
 */
typedef enum
{
    BLE_SCAN_ACTIVE,  /**< Active scan (sends scan requests) */
    BLE_SCAN_PASSIVE, /**< Passive scan (no scan requests) */
} IOT_BleMgmtScanType_t;

/**
 * @brief BLE event callback function type
 * @param arg User-defined argument passed during registration
 * @param eventType Type of event that occurred
 * @param eventData Pointer to event-specific data structure (can be NULL for some events)
 *
 * @note Callback is invoked from BLE stack context. Keep processing lightweight.
 * @note For eventData types, see corresponding event data structures above
 */
typedef void (*IOT_BleEventCb_t)(void *arg, IOT_BleMgmtEventStatusType_t eventType, void *eventData);

/**
 * @brief Advertising data structure
 */
typedef struct
{
    const uint8_t **uuids;      /**< Array of 16-bit service UUID pointers (each 2 bytes, big-endian).
                                     NULL or numUuids==0 to omit the UUID AD element. */
    uint8_t numUuids;           /**< Number of entries in uuids[] */
    uint16_t manufactureId;     /**< Manufacturer ID (company identifier) */
    uint8_t *manufactureData;   /**< Pointer to manufacturer-specific data */
    uint8_t manufactureDataLen; /**< Length of manufacturer data */
    const char *name;           /**< Optional device name, sent in the SCAN RESPONSE as the
                                     Complete Local Name (AD type 0x09). NULL to omit the scan
                                     response entirely. Not required to be NUL-terminated —
                                     nameLen bytes are copied. Must stay valid while advertising. */
    uint8_t nameLen;            /**< Length of name in bytes. 0 omits the scan response. */
} IOT_BleMgmtAdvData_t;

/* ============================
 *  API FUNCTIONS
 * ============================ */

/** @defgroup BLE_Lifecycle BLE Lifecycle Management
 *  @{
 */

/**
 * @brief Initialize BLE stack and management system
 *
 * This function must be called before any other BLE function. It initializes:
 * - Platform BLE stack (NimBLE, SoftDevice, BlueZ, etc.)
 * - Event callback management system
 * - Internal data structures
 *
 * @return IOT_OK on success
 * @return IOT_ERR_NO_MEM if memory allocation failed
 * @return IOT_ERR_FAIL if BLE stack initialization failed
 *
 * @note Must be called once at system startup
 * @note This function may start BLE stack tasks/threads depending on platform
 *
 * @par Example:
 * @code
 * iot_err_t err = IOT_BleInit();
 * if (err != IOT_OK) {
 *     printf("BLE init failed: %s\n", iot_err_to_name(err));
 *     return err;
 * }
 * @endcode
 */
/**
 * @brief Is a BLE driver actually registered on this build?
 *
 * Reflects real registration, not a compile flag: a driver object that failed to
 * link (weak-stub / missing -u) reports false here, where a CONFIG_* check would
 * wrongly report BLE as present.
 */
bool IOT_BleIsAvailable(void);

iot_err_t IOT_BleInit(void);

/**
 * @brief Deinitialize BLE stack and cleanup resources
 *
 * Stops BLE operations and frees all allocated resources. After calling this,
 * IOT_BleInit() must be called again before using any BLE functions.
 *
 * @return IOT_OK on success
 * @return IOT_ERR_FAIL if BLE stack stop failed
 *
 * @note Stops all ongoing operations (advertising, scanning, connections)
 * @note Frees callback management and internal data structures
 * @note Should be called during system shutdown
 */
iot_err_t IOT_BleDeinit(void);

/** @} */

/** @defgroup BLE_GATT GATT Service Management
 *  @{
 */

/**
 * @brief Register and add GATT services to the BLE stack
 *
 * Registers an array of GATT services with the BLE stack. Each service can contain
 * multiple characteristics, and each characteristic can have descriptors.
 *
 * @param service Pointer to NULL-terminated array of service definitions
 *
 * @return IOT_OK on success
 * @return IOT_ERR_INVALID_ARG if service is NULL
 * @return IOT_ERR_NO_MEM if memory allocation failed
 * @return IOT_ERR_FAIL if service registration failed
 *
 * @note Service array must be NULL-terminated (type=0, uuid=NULL for last entry)
 * @note Characteristics array must be NULL-terminated (uuid=NULL for last entry)
 * @note Descriptors array must be NULL-terminated (uuid=NULL for last entry)
 * @note Must be called after IOT_BleInit() and before starting advertising
 *
 * @par Example:
 * @code
 * // Define descriptor
 * static IOT_BleMgmtGattDesc_t descriptors[] = {
 *     {
 *         .uuid = (uint8_t[]){0x29, 0x02},  // Client Characteristic Config
 *         .uuid_len = 2,
 *         .att_flags = IOT_BLE_GATT_CHR_F_READ | IOT_BLE_GATT_CHR_F_WRITE,
 *         .access_cb = desc_access_cb,
 *         .arg = NULL
 *     },
 *     {.uuid = NULL}  // NULL-terminator
 * };
 *
 * // Define characteristic
 * static IOT_BleMgmtGattChar_t characteristics[] = {
 *     {
 *         .uuid = (uint8_t[]){0x2A, 0x19},  // Battery Level
 *         .uuid_len = 2,
 *         .access_cb = char_access_cb,
 *         .arg = NULL,
 *         .descriptor = descriptors,
 *         .flags = IOT_BLE_GATT_CHR_F_READ | IOT_BLE_GATT_CHR_F_NOTIFY
 *     },
 *     {.uuid = NULL}  // NULL-terminator
 * };
 *
 * // Define service
 * static IOT_BleMgmtGattServ_t services[] = {
 *     {
 *         .service_index = 0,
 *         .type = IOT_GATT_SERVICE_TYPE_PRIMARY,
 *         .uuid = (uint8_t[]){0x18, 0x0F},  // Battery Service
 *         .uuid_len = 2,
 *         .characteristics = characteristics
 *     },
 *     {.type = 0, .uuid = NULL}  // NULL-terminator
 * };
 *
 * iot_err_t err = IOT_BleAddServices(services);
 * @endcode
 */
iot_err_t IOT_BleAddServices(const IOT_BleMgmtGattServ_t *service);

/** @} */

/** @defgroup BLE_Scan Scanning Operations
 *  @{
 */

/**
 * @brief Start BLE scanning
 *
 * Starts scanning for nearby BLE devices. Scan results are delivered via
 * BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT event callback.
 *
 * @param intervalTime Scan duration in milliseconds (0 = scan forever until stopped)
 *
 * @return IOT_OK on success
 * @return IOT_ERR_FAIL if scan start failed (e.g., already scanning)
 *
 * @note Register BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT callback to receive results
 * @note Register BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE callback for completion notification
 * @note Call IOT_BleStopScan() to stop scanning before duration expires
 *
 * @par Example:
 * @code
 * void scan_result_cb(void *arg, IOT_BleMgmtEventStatusType_t type, void *data) {
 *     if (type == BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT) {
 *         IOT_BleMgmtEventScanResultData_t *result = data;
 *         printf("Found: %02X:%02X:..., RSSI: %d\n",
 *                result->addr[0], result->addr[1], result->rssi);
 *     }
 * }
 *
 * IOT_BleRegisterEventCallback(BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT, scan_result_cb, NULL);
 * IOT_BleStartScan(10000);  // Scan for 10 seconds
 * @endcode
 */
iot_err_t IOT_BleStartScan(uint32_t intervalTime);

/**
 * @brief Stop BLE scanning
 *
 * Stops an ongoing scan operation.
 *
 * @return IOT_OK on success
 * @return IOT_ERR_FAIL if stop failed (e.g., not scanning)
 *
 * @note Can be called before scan duration expires to stop early
 */
iot_err_t IOT_BleStopScan(void);

/** @} */

/** @defgroup BLE_Config Device Configuration
 *  @{
 */

/**
 * @brief Set BLE device name
 *
 * Sets the device name that will appear in GAP advertisements and GATT device name characteristic.
 *
 * @param name Pointer to NULL-terminated device name string
 *
 * @return IOT_OK on success
 * @return IOT_ERR_FAIL if set name failed
 *
 * @note Maximum length depends on platform (typically 20-29 characters)
 * @note Should be called before starting advertising
 * @note Name persists until changed or device reset
 *
 * @par Example:
 * @code
 * IOT_BleSetDeviceName("MyIoTDevice");
 * @endcode
 */
iot_err_t IOT_BleSetDeviceName(const char *name);

/** @} */

/** @defgroup BLE_Advertising Advertising Operations
 *  @{
 */

/**
 * @brief Start BLE beacon advertising (non-connectable)
 *
 * Starts advertising in beacon mode. Devices cannot connect, only receive broadcast data.
 * Useful for iBeacon, Eddystone, or custom beacon applications.
 *
 * @param adv_requirements Pointer to advertising data configuration
 *
 * @return IOT_OK on success
 * @return IOT_ERR_FAIL if advertising start failed
 * @return IOT_ERR_NO_MEM if memory allocation failed
 *
 * @note Non-connectable mode - devices can only scan, not connect
 * @note Advertising continues until IOT_BleStopBeaconAdvertising() is called
 * @note Cannot be used simultaneously with IOT_BleStartAdvertising()
 *
 * @par Example:
 * @code
 * IOT_BleMgmtAdvData_t beacon_data = {
 *     .uuid = (uint8_t[]){0x18, 0x0F},  // Battery Service UUID
 *     .manufactureId = 0x004C,           // Apple Inc.
 *     .manufactureData = (uint8_t[]){0x02, 0x15, ...},  // iBeacon data
 *     .manufactureDataLen = 21
 * };
 * IOT_BleStartBeaconAdvertising(&beacon_data);
 * @endcode
 */
iot_err_t IOT_BleStartBeaconAdvertising(IOT_BleMgmtAdvData_t *adv_requirements);

/**
 * @brief Stop BLE beacon advertising
 *
 * Stops beacon advertising started with IOT_BleStartBeaconAdvertising().
 *
 * @return IOT_OK on success
 * @return IOT_ERR_FAIL if stop failed
 */
iot_err_t IOT_BleStopBeaconAdvertising(void);

/**
 * @brief Start BLE advertising (connectable)
 *
 * Starts connectable advertising. Nearby devices can connect and access GATT services.
 *
 * @param adv_requirements Pointer to advertising data configuration
 *
 * @return IOT_OK on success
 * @return IOT_ERR_FAIL if advertising start failed
 * @return IOT_ERR_NO_MEM if memory allocation failed
 *
 * @note Connectable mode - devices can connect and access GATT services
 * @note Register BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED callback to handle connections
 * @note Advertising continues until connection or IOT_BleStopAdvertising() called
 * @note Advertising may auto-stop on connection (platform-dependent)
 *
 * @par Example:
 * @code
 * IOT_BleMgmtAdvData_t adv_data = {
 *     .uuid = (uint8_t[]){0x18, 0x0F},  // Battery Service
 *     .manufactureId = 0x1234,
 *     .manufactureData = (uint8_t[]){0x01, 0x02, 0x03},
 *     .manufactureDataLen = 3
 * };
 * IOT_BleStartAdvertising(&adv_data);
 * @endcode
 */
iot_err_t IOT_BleStartAdvertising(IOT_BleMgmtAdvData_t *adv_requirements);

/**
 * @brief Stop BLE advertising
 *
 * Stops advertising started with IOT_BleStartAdvertising().
 *
 * @return IOT_OK on success
 * @return IOT_ERR_FAIL if stop failed
 *
 * @note Automatically called on connection (platform-dependent)
 */
iot_err_t IOT_BleStopAdvertising(void);

/** @} */

/** @defgroup BLE_Notify Notification Operations
 *  @{
 */

/**
 * @brief Send notification to connected client
 *
 * Sends a notification with data to a connected peer device. The characteristic
 * must have NOTIFY property enabled.
 *
 * @param addr Client address (can be NULL if only one connection exists)
 * @param charUuid Pointer to characteristic UUID (2, 4, or 16 bytes)
 * @param data Pointer to data to send
 * @param len Length of data in bytes
 *
 * @return IOT_OK on success
 * @return IOT_ERR_INVALID_ARG if parameters are invalid
 * @return IOT_ERR_NOT_FOUND if characteristic not found
 * @return IOT_ERR_NO_MEM if memory allocation failed
 * @return IOT_ERR_FAIL if notification send failed (e.g., no connection, notifications disabled)
 *
 * @note Client must have enabled notifications (written to CCCD descriptor)
 * @note Maximum data length depends on MTU (typically 20 bytes default, up to 512 with MTU exchange)
 * @note Notification does not wait for acknowledgment from peer
 *
 * @par Example:
 * @code
 * uint8_t battery_level = 85;
 * uint8_t battery_uuid[] = {0x2A, 0x19};  // Battery Level characteristic
 *
 * iot_err_t err = IOT_BleNotifyToCharacteristic(
 *     NULL,           // Use current connection
 *     battery_uuid,
 *     &battery_level,
 *     sizeof(battery_level)
 * );
 * @endcode
 */
iot_err_t IOT_BleNotifyToCharacteristic(const char *addr, const uint8_t *charUuid, const uint8_t *data, uint16_t len);

/** @} */

/** @defgroup BLE_Status Connection Status
 *  @{
 */

/**
 * @brief Check if a BLE client is currently connected
 *
 * @param[out] isConnected Pointer to receive connection status (true if connected)
 *
 * @return IOT_OK on success
 * @return IOT_ERR_INVALID_ARG if isConnected is NULL
 */
iot_err_t IOT_BleIsConnected(bool *isConnected);

/**
 * @brief Terminate the current BLE connection (if any).
 *
 * Additive platform primitive. Drivers that do not implement it (NULL vtable
 * slot) return IOT_ERR_NOT_SUPPORTED.
 *
 * @return IOT_OK if a disconnect was initiated, IOT_ERR_NOT_SUPPORTED if
 *         unimplemented, or IOT_ERR_INVALID_STATE if not connected.
 */
iot_err_t IOT_BleDisconnect(void);

/** @} */

/** @defgroup BLE_Events Event Callback Management
 *  @{
 */

/**
 * @brief Register event callback for a specific BLE event type
 *
 * Registers a callback function to be invoked when the specified BLE event occurs.
 * Only one callback can be registered per event type.
 *
 * @param event Event type to register for
 * @param cb Callback function to invoke when event occurs
 * @param arg User-defined argument to pass to callback
 *
 * @return IOT_OK on success
 * @return IOT_ERR_INVALID_ARG if event type is invalid (>= BLE_MGMT_EVENT_STATUS_TYPE_MAX)
 *
 * @note Callback is invoked from BLE stack context - keep processing lightweight
 * @note Registering for same event type twice replaces previous callback
 * @note Some events (INIT_DONE, RESET) require platform-specific setup
 *
 * @par Example:
 * @code
 * void on_connected(void *arg, IOT_BleMgmtEventStatusType_t type, void *data) {
 *     IOT_BleMgmtEventConnectData_t *conn = data;
 *     if (conn->status == 0) {
 *         printf("Connected successfully!\n");
 *     }
 * }
 *
 * IOT_BleRegisterEventCallback(
 *     BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED,
 *     on_connected,
 *     NULL
 * );
 * @endcode
 *
 * @par Supported Event Types:
 * - BLE_MGMT_EVENT_STATUS_TYPE_INIT_DONE: BLE stack initialized
 * - BLE_MGMT_EVENT_STATUS_TYPE_RESET: BLE stack reset
 * - BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED: Device connected
 * - BLE_MGMT_EVENT_STATUS_TYPE_DISCONNECT: Device disconnected
 * - BLE_MGMT_EVENT_STATUS_TYPE_SCAN_RESULT: Scan result available
 * - BLE_MGMT_EVENT_STATUS_TYPE_SCAN_COMPLETE: Scan completed
 * - BLE_MGMT_EVENT_STATUS_TYPE_ADV_STOP: Advertising stopped
 * - BLE_MGMT_EVENT_STATUS_TYPE_ENABLE_STATUS: Notifications enabled/disabled
 * - BLE_MGMT_EVENT_STATUS_TYPE_SEND_NOTIFICATION: Notification sent
 */
iot_err_t IOT_BleRegisterEventCallback(IOT_BleMgmtEventStatusType_t event, IOT_BleEventCb_t cb, void *arg);

/**
 * @brief Unregister event callback for a specific BLE event type
 *
 * Removes a previously registered callback for the specified event type.
 *
 * @param event Event type to unregister
 *
 * @return IOT_OK on success
 * @return IOT_ERR_INVALID_ARG if event type is invalid
 * @return IOT_ERR_FAIL if callback was not registered
 *
 * @par Example:
 * @code
 * IOT_BleUnregisterEventCallback(BLE_MGMT_EVENT_STATUS_TYPE_CONNECTED);
 * @endcode
 */
iot_err_t IOT_BleUnregisterEventCallback(IOT_BleMgmtEventStatusType_t event);

/** @} */
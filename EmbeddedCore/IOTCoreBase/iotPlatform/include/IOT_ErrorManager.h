#pragma once

typedef int iot_err_t;

#define IOT_OK 0        // No error
#define IOT_ERR_FAIL -1 // General failure

#define IOT_ERR_NO_MEM 0x101                       /*!< Out of memory */
#define IOT_ERR_INVALID_ARG 0x102                  /*!< Invalid argument */
#define IOT_ERR_INVALID_STATE 0x103                /*!< Invalid state */
#define IOT_ERR_INVALID_SIZE 0x104                 /*!< Invalid size */
#define IOT_ERR_NOT_FOUND 0x105                    /*!< Requested resource not found */
#define IOT_ERR_NOT_SUPPORTED 0x106                /*!< Operation or feature not supported */
#define IOT_ERR_TIMEOUT 0x107                      /*!< Operation timed out */
#define IOT_ERR_INVALID_RESPONSE 0x108             /*!< Received response was invalid */
#define IOT_ERR_INVALID_CRC 0x109                  /*!< CRC or checksum was invalid */
#define IOT_ERR_INVALID_VERSION 0x10A              /*!< Version was invalid */
#define IOT_ERR_INVALID_MAC 0x10B                  /*!< MAC address was invalid */
#define IOT_ERR_NOT_FINISHED 0x10C                 /*!< There are items remained to retrieve */
#define IOT_ERR_NOT_ALLOWED 0x10D                  /*!< Operation is not allowed */
#define IOT_ERR_ROC_IN_PROGRESS 0x10E              /*!< ROC Operation is in progress */
#define IOT_ERR_WIFI_GET_IP_FAILED 0x10F           /*!< Failed to get IP address from WiFi */
#define IOT_ERR_WIFI_CONNECT_FAILED 0x110          /*!< Failed to connect to WiFi */
#define IOT_ERR_WIFI_GET_AP_INFO_FAILED 0x111      /*!< Failed to get connected AP info */
#define IOT_ERR_WIFI_SCAN_FAILED 0x112             /*!< Failed to scan WiFi networks */
#define IOT_ERR_WIFI_SET_PROTOCOL_FAILED 0x113     /*!< Failed to set WiFi protocol */
#define IOT_ERR_INVALID_INTERFACE 0x114            /*!< Invalid interface type */
#define IOT_ERR_WIFI_SET_MAX_TX_POWER_FAILED 0x115 /*!< Failed to set maximum WiFi transmit power */
#define IOT_ERR_WIFI_GET_AP_RSSI_FAILED 0x116      /*!< Failed to get RSSI of connected AP */
#define IOT_ERR_ALREADY_EXISTS 0x117               /*!< Resource already exists */
#define IOT_ERR_WIFI_BASE 0x3000                   /*!< Starting number of WiFi error codes */
#define IOT_ERR_MESH_BASE 0x4000                   /*!< Starting number of MESH error codes */
#define IOT_ERR_FLASH_BASE 0x6000                  /*!< Starting number of flash error codes */
#define IOT_ERR_HW_CRYPTO_BASE 0xc000              /*!< Starting number of HW cryptography module error codes */
#define IOT_ERR_MEMPROT_BASE 0xd000                /*!< Starting number of Memory Protection API error codes */
#define IOT_ERR_NO_NETWORK_CONNECT 0xe00
#define IOT_ERR_TYPE_MISMATCH 0xe01
#define IOT_ERR_DATA_NOT_FOUND 0xe02
#define IOT_ERR_ALREADY_INIT 0xe03
#define IOT_ERR_NOT_INITIALIZED 0x1004               /*!< Not initialized */
#define IOT_ERR_OTA_BOOT_DEGRADED 0x1005             /*!< IOT_CoreInit()'s OTA boot could neither hand the
                                                           download off to an SDK nor discard the unclaimed
                                                           request; it attempted to recover BLE in place instead
                                                           of rebooting into the same unresolved state. See
                                                           IOT_CoreInit()'s doc comment. */
const char *iot_err_to_name(iot_err_t code);

#define IOT_ERROR_CHECK(x)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        int iot_rc_ = (x);                                                                                             \
        if (iot_rc_ != IOT_OK)                                                                                         \
        {                                                                                                              \
            fprintf(stderr, "Error: %s at %s:%d\n", iot_err_to_name(err_rc_), __FILE__, __LINE__);                     \
            exit(err_rc_);                                                                                             \
        }                                                                                                              \
    } while (0)

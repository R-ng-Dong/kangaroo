#pragma once
#include "IOT_ErrorManager.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* OTA status codes (MsgData byte 1) — match protocol spec SETTING_OTA_PROGRESS_INFO */
#define IOT_OTA_STATUS_SUCCESS          0x00 /* OTA applied and verified — post-reboot only */
#define IOT_OTA_STATUS_INPROGRESS       0x01 /* Legacy — do not use; replaced by 0x04/0x05/0x06 */
#define IOT_OTA_STATUS_FAILURE          0x02 /* OTA failed; MsgData byte 2 = error code */
#define IOT_OTA_STATUS_NOT_UPDATE       0x03 /* No update available / version already current */
#define IOT_OTA_STATUS_DOWNLOADING      0x04 /* Download started; MsgData byte 2 = 0 */
#define IOT_OTA_STATUS_UPDATING         0x05 /* Flash/validate started; MsgData byte 2 = 0 */
#define IOT_OTA_STATUS_SOFTWAREPROGRESS 0x06 /* Progress tick; MsgData byte 2 = progress 0-100 */
#define IOT_OTA_STATUS_UPDATE_LATER     0x07 /* Cloud sent UPDATELATER option */

/* OTA failure error codes (MsgData byte 2 when statusCode == FAILURE) */
#define IOT_OTA_ERR_GENERAL                     0x00
#define IOT_OTA_ERR_URL_NOT_EXIST               0x01
#define IOT_OTA_ERR_NOT_SUPPORT                 0x02
#define IOT_OTA_ERR_BUSY                        0x03
#define IOT_OTA_ERR_VERSION_LOWER               0x04
#define IOT_OTA_ERR_VERSION_NOT_INCREASE        0x05
#define IOT_OTA_ERR_REVISION_NOT_VALID          0x06
#define IOT_OTA_ERR_DOWNLOAD_GENERAL            0x10
#define IOT_OTA_ERR_DOWNLOAD_HTTPS_CERT         0x11
#define IOT_OTA_ERR_UPDATE_GENERAL              0x20

/* Linux target: process exit status used to ask the supervisor to restart the service after reset/reboot. */
#define IOT_LINUX_RESTART_EXIT_CODE 75

/**
 * @brief Callback for OTA progress reporting
 * @param status  IOT_OTA_STATUS_* code (0x04=DOWNLOADING, 0x05=UPDATING, 0x06=PROGRESS, 0x02=FAILURE, 0x00=SUCCESS)
 * @param data    Meaning depends on status:
 *                  DOWNLOADING (0x04) → 0
 *                  UPDATING    (0x05) → 0
 *                  PROGRESS    (0x06) → progress% (0-100)
 *                  FAILURE     (0x02) → IOT_OTA_ERR_* error code
 *                  SUCCESS     (0x00) → 100
 * @param user_data User-provided context pointer
 */
typedef void (*IOT_OtaProgressCb_t)(uint8_t status, uint8_t progress, void *user_data);

/**
 * @brief Live OTA state — queried by core to build INFO_SOFTWARE (0xB001) attr.
 */
typedef struct
{
    uint8_t statusCode;        // IOT_OTA_STATUS_*
    uint8_t statusData;        // progress% (DOWNLOADING/UPDATING) or error code (FAILURE)
    char targetVersion[32];    // OTA target version string (empty if none)
} IOT_OtaState_t;

/**
 * @brief Why the chip started this time.
 *
 * The distinction that matters to a boot-loop guard is *unplanned death* versus
 * everything else. A user cutting power and a deliberate restart both look like a
 * reboot but say nothing about the firmware's health, and BROWNOUT is deliberately
 * its own case: it means the supply is failing, and no amount of erasing the
 * device's data will fix a supply.
 */
typedef enum
{
    IOT_RESET_REASON_POWER_ON,  /**< Cold start, or power physically removed. */
    IOT_RESET_REASON_SOFTWARE,  /**< Deliberate restart — OTA, reboot command. */
    IOT_RESET_REASON_CRASH,     /**< Panic or watchdog: the app died unplanned. */
    IOT_RESET_REASON_BROWNOUT,  /**< Supply sagged. A power problem, not a data problem. */
    IOT_RESET_REASON_UNKNOWN,
} IOT_ResetReason_t;

/**
 * @brief Report why the chip started. Never fails destructively — an unmappable
 *        reason is reported as IOT_RESET_REASON_UNKNOWN.
 */
iot_err_t IOT_AppManagerGetResetReason(IOT_ResetReason_t *out);

/**
 * @brief Confirm the running image works, cancelling any pending rollback.
 *
 * With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE set, a freshly-OTA'd image boots in
 * a pending-verify state and the bootloader reverts to the previous slot unless
 * the app confirms itself. Call this once the device has demonstrably settled.
 * **Enabling that Kconfig without calling this makes every OTA revert on its
 * second boot.** No-op on platforms without rollback support.
 */
iot_err_t IOT_AppManagerMarkAppValid(void);

iot_err_t IOT_ApplicationRestartChip(void);
iot_err_t IOT_AppManagerGetVersion(char *versionBuffer, size_t bufferLen);
iot_err_t IOT_AppManagerGetOtaState(IOT_OtaState_t *state);
iot_err_t IOT_AppManagerCheckForUpdates(const char *newVersion, bool *isUpdateAvailable); // to be modified
iot_err_t IOT_AppManagerStartUpdateProcess(const char *updateUrl, const char *cert, size_t certLen,
                                           const char *authToken, size_t authTokenLen,
                                           void *user_data, IOT_OtaProgressCb_t progressCb);

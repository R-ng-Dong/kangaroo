#ifndef APP_H
#define APP_H


#include "common.h"
#include "esp_err.h"

#define TEST_PRODUCT 1

#define BOOT_COUNT_RESET_THRESHOLD 3
#define BOOT_COUNT_RESET_TIMEOUT_MS 60000

#define APP_INTERVAL_MS 1000
#define TIMEOUT_UPDATE_SYSTEM_TIME_MS 8*3600*1000 // 8 hours

#define TIMEOUT_UPDATE_SYSTEM_TIME_COUNT  (TIMEOUT_UPDATE_SYSTEM_TIME_MS/APP_INTERVAL_MS)
#define TIMEOUT_UPDATE_WARNING_COUNT (30*60*1000 / APP_INTERVAL_MS)

#define PID_MAX_LENGTH 16
#define SERIAL_MAX_LENGTH 10

typedef struct 
{
    UI8 type;
    UI8 pid[PID_MAX_LENGTH];
    UI8 serial[SERIAL_MAX_LENGTH];
}ProductModel;

esp_err_t AppCommonInit(void);
esp_err_t AppInit(WaterPurifierType model);
void AppCheckModel(void);
void AppGetModelId(UI8 modelID[PID_MAX_LENGTH]);
void AppGetSerial(UI8 serial[SERIAL_MAX_LENGTH]);
WaterPurifierType AppGetModel(void);
void AppSetModel(WaterPurifierType model);
void AppRspToCloud(UI8 elem_id, UI16 attr_id);
void AppPushTdsReportToCloud(UI16 inlet_ppm, UI16 ro_ppm);
void AppPushWarningToCloud(void);
void AppRefreshAttributeValue(void);

_bool ModelConfigStore(WaterPurifierType model, const char *pid, const char *serial);
_bool ModelConfigGet(ProductModel *p_model);

_bool IsOtaReady(void);

#endif

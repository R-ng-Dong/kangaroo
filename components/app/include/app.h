#ifndef APP_H
#define APP_H


#include "common.h"
#include "esp_err.h"

#define BOOT_COUNT_RESET_THRESHOLD 3
#define BOOT_COUNT_RESET_TIMEOUT_MS 60000

#define APP_INTERVAL_MS 1000
#define TIMEOUT_UPDATE_SYSTEM_TIME_MS 2*3600*1000 // 2 hours
#define TIMEOUT_RESPONSE_ENERGY_TO_CLOUD_MS 1*60*1000 // 5 minutes
#define TIMEOUT_RESPONSE_WATER_CONSUMPTION_TO_CLOUD_MS 1*60*1000 // 5 minutes

#define TIMEOUT_UPDATE_SYSTEM_TIME_COUNT  (TIMEOUT_UPDATE_SYSTEM_TIME_MS/APP_INTERVAL_MS)
#define TIMEOUT_RESPONSE_ENERGY_TO_CLOUD_COUNT  (TIMEOUT_RESPONSE_ENERGY_TO_CLOUD_MS/APP_INTERVAL_MS)
#define TIMEOUT_RESPONSE_WATER_CONSUMPTION_TO_CLOUD_MS_COUNT  (TIMEOUT_RESPONSE_WATER_CONSUMPTION_TO_CLOUD_MS/APP_INTERVAL_MS)

esp_err_t AppCommonInit(void);
esp_err_t AppInit(WaterPurifierType model);
WaterPurifierType AppCheckModel(void);
WaterPurifierType AppGetModel(void);
void AppSetModel(WaterPurifierType model);
void AppRspToCloud(UI8 elem_id, UI16 attr_id);
void AppPushTdsReportToCloud(UI16 inlet_ppm, UI16 ro_ppm);

#endif

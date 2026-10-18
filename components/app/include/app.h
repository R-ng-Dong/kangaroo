#ifndef APP_H
#define APP_H


#include "common.h"
#include "esp_err.h"

esp_err_t AppInit(void);
void AppRspToCloud(UI8 elem_id, UI16 attr_id);
void AppPushTdsReportToCloud(UI16 inlet_ppm, UI16 ro_ppm);

#endif

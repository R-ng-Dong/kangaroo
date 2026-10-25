#ifndef LED_MANAGER_H_
#define LED_MANAGER_H_

#include "common.h"
#include "esp_err.h"

#define DIM_ON 50  // 50%
#define DIM_OFF 10 // 10%

esp_err_t LedManagerInit(void);
esp_err_t LedManagerSetError(void);
esp_err_t LedManagerSetNormal(void);
esp_err_t LedManagerSetState(UI8 state);
esp_err_t LedManagerSetNetworkWaiting(void);
esp_err_t LedManagerSetNetworkConnected(void);
esp_err_t LedManagerSetOta(void);
/** Set the factory-test LED color and stop any active LED pattern. */
esp_err_t LedManagerSetRgb(UI8 red, UI8 green, UI8 blue); // cungdd

#endif /* LED_MANAGER_H_ */

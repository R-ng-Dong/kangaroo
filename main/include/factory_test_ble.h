#ifndef FACTORY_TEST_BLE_H
#define FACTORY_TEST_BLE_H

#include <stdbool.h>
#include "esp_err.h"

/** Register the factory Wi-Fi GATT service before core advertising. */
esp_err_t FactoryTestBleInit(void);

/** Advertise a unique factory name so the PC can discover this device over BLE. */
esp_err_t FactoryTestBleStartAdvertising(void);
bool FactoryTestBleIsAdvertising(void);
const char *FactoryTestBleGetName(void);

#endif /* FACTORY_TEST_BLE_H */

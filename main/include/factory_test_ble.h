#ifndef FACTORY_TEST_BLE_H
#define FACTORY_TEST_BLE_H

#include "esp_err.h"

/** Register the factory Wi-Fi GATT service before core advertising. */
esp_err_t FactoryTestBleInit(void);

#endif /* FACTORY_TEST_BLE_H */

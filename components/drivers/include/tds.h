#ifndef TDS_DRIVER_H
#define TDS_DRIVER_H

#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "adc_driver.h"

#define TDS_MAX_SENSOR_COUNT 2U

typedef enum {
    TDS_SENSOR_0 = 0,
    TDS_SENSOR_1 = 1,
    TDS_SENSOR_UNKNOWN = 0xFF
} TdsSensorId;

typedef struct
{
    gpio_num_t pulse1_pin;
    gpio_num_t pulse2_pin;
    AdcChannel adc_channel;
    TdsSensorId sensor_id;
}TdsPinConfig;

esp_err_t TdsInit(TdsPinConfig config[2]);
esp_err_t TdsDeinit(TdsSensorId sensor_id);
UI16 TdsGetValue(TdsSensorId sensor_id);
esp_err_t TdsConfigure(gpio_num_t pulse1_pin, gpio_num_t pulse2_pin, AdcChannel adc_channel, TdsSensorId sensor_id);
esp_err_t TdsStartPulses(TdsSensorId sensor_id);
esp_err_t TdsStopPulses(TdsSensorId sensor_id);
void TdsTaskDeinit(void);
esp_err_t TdsTaskResume(void);

#endif

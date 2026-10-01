#pragma once

#include "esp_err.h"

#define RAL_TIMEOUT_RESET_MS   (3*60*1000)

esp_err_t device_reset_timer_start(uint32_t interval_ms);
esp_err_t device_reset_timer_stop(void);
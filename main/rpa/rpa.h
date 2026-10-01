#pragma once

#include "esp_err.h"
#include "udp.h"

#ifdef __cplusplus
extern "C" {
#endif


#define RPA_WIFI_SSID "TP-Link_04BE" 
#define RPA_WIFI_PASS "63061871" 

// #define RPA_WIFI_SSID "PHONG CAM" 
// #define RPA_WIFI_PASS "12345678" 

// #define RPA_WIFI_SSID "TTS" 
// #define RPA_WIFI_PASS "TTS123456" 

esp_err_t rpa_init_wifi_default(void);
int8_t rd_get_wifi_rssi_test(void);
void rpa_read_adc(int *value1, int *value2);
esp_err_t rpa_init_input_task(void);
esp_err_t rpa_deinit_input_task(void);
esp_err_t rpa_init_output_task(void);
esp_err_t rpa_deinit_output_task(void);
void rpa_test_led_rgb(uint8_t red, uint8_t green, uint8_t blue);
void TDS_test(void);

#ifdef __cplusplus
}
#endif
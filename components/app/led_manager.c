#include "led_manager.h"
#include "lc8823.h"
#include "pin_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static LedHandle s_led = NULL;
typedef enum 
{ 
    LED_PATTERN_CONNECTED, 
    LED_PATTERN_WAITING, 
    LED_PATTERN_OTA 
} LedPattern;

static volatile LedPattern s_pattern = LED_PATTERN_WAITING;
static volatile _bool s_pattern_active = D_FALSE;

static void LedPatternTask(void *arg)
{
    _bool led_on = D_TRUE;
    while (1)
    {
        if (!s_pattern_active)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        LedPattern pattern = s_pattern;
        if (pattern == LED_PATTERN_CONNECTED)
        {
            LedManagerSetState(1);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        LedManagerSetState(led_on ? LED_ON : LED_OFF);
        led_on = !led_on;
        vTaskDelay(pdMS_TO_TICKS(pattern == LED_PATTERN_OTA ? 100 : 500));
    }
}

static esp_err_t LedManagerSetPattern(LedPattern pattern)
{
    if (!s_led) return ESP_FAIL;
    s_pattern = pattern;
    s_pattern_active = D_TRUE;
    return ESP_OK;
}

esp_err_t LedManagerInit(void)
{
    s_led = LC8823Create(0, 0);    
    if(s_led == NULL) return ESP_FAIL;
    LC8823Config config = {
        .clk_pin = LED_CLK,
        .data_pin = LED_DATA,
        .freq = FREQ_LED
    };
    LC8823Init(&config);
    esp_err_t err = LC8823InitFrameBuffer(1);
    if(err != ESP_OK) return ESP_FAIL;

    LC8823LoadValue(s_led, LED_ON, DIM_ON, 0x00, 0xFF, 0x00); // LED ON: dim 50%, rgb = 0 255 0
    LC8823LoadValue(s_led, LED_OFF, DIM_OFF, 0x00, 0xFF, 0x00); // LED OFF: dim 10% rgb = 0 255 0

    err = LC8823SetState(s_led, LED_ON);
    if(err != ESP_OK) return ESP_FAIL;
    err = LC8823SendFrameBuffer();
    if (err != ESP_OK) return err;
    if (xTaskCreate(LedPatternTask, "LedPattern", 2048, NULL, 4, NULL) != pdPASS) return ESP_FAIL;
    return LedManagerSetNetworkWaiting();
}

esp_err_t LedManagerSetError(void)
{
    if(!s_led) return ESP_FAIL;
    // s_pattern_active = false;
    LC8823LoadValue(s_led, LED_ON, DIM_ON, 0xFF, 0x00, 0x00); 
    LC8823LoadValue(s_led, LED_OFF, DIM_OFF, 0xFF, 0x00, 0x00);
    esp_err_t err = LC8823SetState(s_led, LED_ON);
    if(err != ESP_OK) return ESP_FAIL;
    return LC8823SendFrameBuffer();    
}

esp_err_t LedManagerSetNormal(void)
{
    if(!s_led) return ESP_FAIL;
    // s_pattern_active = false;
    LC8823LoadValue(s_led, LED_ON, DIM_ON, 0x00, 0xFF, 0x00); 
    LC8823LoadValue(s_led, LED_OFF, DIM_OFF, 0x00, 0xFF, 0x00);
    esp_err_t err = LC8823SetState(s_led, LED_ON);
    if(err != ESP_OK) return ESP_FAIL;
    return LC8823SendFrameBuffer();    
}

esp_err_t LedManagerSetState(UI8 state)
{
    if(!s_led) return ESP_FAIL;
    esp_err_t err = LC8823SetState(s_led, (state == 0) ? LED_OFF : LED_ON);
    if(err != ESP_OK) return ESP_FAIL;
    return LC8823SendFrameBuffer();  
}

esp_err_t LedManagerSetNetworkWaiting(void) { return LedManagerSetPattern(LED_PATTERN_WAITING); }
esp_err_t LedManagerSetNetworkConnected(void) { return LedManagerSetPattern(LED_PATTERN_CONNECTED); }
esp_err_t LedManagerSetOta(void) { return LedManagerSetPattern(LED_PATTERN_OTA); }

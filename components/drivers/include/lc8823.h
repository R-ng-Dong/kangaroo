#ifndef LC8823_H_
#define LC8823_H_

#include "common.h"
#include "esp_err.h"
#include "driver/gpio.h"

// #define LIGHT_MAX    0xEF  //50%
// #define LIGHT_MIN    0xE2  //2/31 = 6%

#define MAX_LUM_VALUE 31
#define MAX_DIM_VALUE 100
#define MIN_DIM_VALUE 0

#define MAX_NUM_LED_OBJECT 7

#define LED_ON  1
#define LED_OFF 0

typedef struct{
    gpio_num_t clk_pin;
    gpio_num_t data_pin;
    UI16 freq; // max 27kHz
}LC8823Config;

typedef void *LedHandle;

void LC8823Init(LC8823Config *config);
LedHandle LC8823Create(UI8 index, UI8 elm_id);

esp_err_t LC8823InitFrameBuffer(const UI8 num_led);
// esp_err_t LC8823UpdateFrameBuffer(UI8 idx, LedData *led_data);
esp_err_t LC8823SendFrameBuffer(void);

void      LC8823LoadValue(LedHandle led, UI8 onoff, UI8 dim, UI8 red, UI8 green, UI8 blue); //dim: 0 -> 100%
esp_err_t LC8823SetState(LedHandle led, UI8 state);
esp_err_t LC8823SetColor(LedHandle led, UI8 onoff, UI8 red, UI8 green, UI8 blue);
esp_err_t LC8823SetLum(LedHandle led, UI8 onoff, UI8 lum); // lum: 0 -> 31

UI8 LC8823GetLum(LedHandle led, UI8 onoff);
UI8 LC8823GetRedColor(LedHandle led, UI8 onoff);
UI8 LC8823GetGreenColor(LedHandle led, UI8 onoff);
UI8 LC8823GetBlueColor(LedHandle led, UI8 onoff);

static inline UI8 LC8823ConvertDimToLum(UI8 dim) //dim: 0-100
{
    if(MAX_DIM_VALUE < dim) return MAX_LUM_VALUE;
    if((MIN_DIM_VALUE + 5) >= dim && MIN_DIM_VALUE < dim) return 1;
    UI8 dim_calib = dim * MAX_LUM_VALUE / MAX_DIM_VALUE;
    return dim_calib;
}

#endif /* LC8823_H_ */

#pragma once

#include "freertos/FreeRTOS.h"
#include "led_rmt.h"

#define BLINK_ALL_LED 0xff

enum class BlinkMode{
    BLINK_MODE_BLOCKING, // sử dụng delay, chặn CPU
    BLINK_MODE_NON_BLOCKING // dùng timer
};
struct BlinkLedConfig{
    uint16_t count;
    uint32_t intervalMs;
    TickType_t lastTime; //typedef uint32_t TickType_t;
    LedState oldState;
    BlinkLedConfig() : count(0), intervalMs(0), lastTime(0), oldState(LedState::OFF) {}
};
class LedButton : public LedRmt
{
    public:
        static LedButton *getInstance();
        static void deleteInstance();
        esp_err_t ledButtonSetState(uint8_t ledBtnIdx, LedState state);
        esp_err_t ledButtonSetLum(uint8_t ledBtnIdx, LedState state, uint8_t lum);
        esp_err_t ledButtonSetColor(uint8_t ledBtnIdx, LedState state, uint8_t red, uint8_t green, uint8_t blue);
        esp_err_t ledButtonSetColor(uint8_t ledBtnIdx, LedState state, uint8_t red, uint8_t green, uint8_t blue, uint8_t lum);
        esp_err_t ledButtonInit(uint8_t ledBtnIdx, LedState state, uint8_t lumOn, uint8_t redOn, uint8_t greenOn,\
                                uint8_t blueOn, uint8_t lumOff, uint8_t redOff, uint8_t greenOff, uint8_t blueOff);
        esp_err_t LedButtonSendData() { return sendData(); }
        esp_err_t LedButtonBlink(BlinkMode mode, uint8_t ledBtnIdx, uint8_t count, uint32_t intervalMs);
        
    private:
        LedButton();
        ~LedButton() = default;
        static LedButton *instance;
        std::vector<LedRmt::LedRgb> _leds;
        BlinkLedConfig blinkLedConfig[4];
        static void handleBlinkAdapter(void * arg);
        void handleBlink(void *arg);
};

class LedScreen : public LedRmt
{
    public:
        static LedScreen *getInstance();
        static void deleteInstance();
        esp_err_t ledScreenSetColor(uint8_t red, uint8_t green, uint8_t blue);
    private:
        LedScreen();
        ~LedScreen() = default;
        static LedScreen *instance;
        std::vector<LedRmt::LedRgb> _leds;
};



 
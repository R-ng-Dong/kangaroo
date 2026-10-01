#pragma once

#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#include <vector>

enum class LedState {
    OFF = 0,
    ON = 1
};
/*===============================================
LED RGB (HC0807) data format: G7...G0 R7...R0 B7...B0
================================================*/
class LedRmt {
    public:
        class LedRgb {
            private:
                uint8_t _rOn; uint8_t _rOff; // 0-255
                uint8_t _gOn; uint8_t _gOff;
                uint8_t _bOn; uint8_t _bOff;
                uint8_t _lumOn; uint8_t _lumOff; // 0-100
                uint8_t _idx; // index of the LED in the strip, starting from 0
                uint8_t _groupIdx; // giá trị này cho biết led đó gắn với button nào
                LedState _state; // 0: off, 1: on
            public:
                LedRgb(uint8_t idx, uint8_t groupIdx);
                ~LedRgb() {}
                void setValue(LedState state, uint8_t lum , uint8_t red, uint8_t green, uint8_t blue);
                void setLum(LedState state, uint8_t lum);
                void setColor(LedState state, uint8_t red, uint8_t green, uint8_t blue);
                void setState(LedState state) { _state = state; }
                LedState getState() const { return _state; }
                uint8_t getIndex() const { return _idx; }
                uint8_t getGroupIndex() const { return _groupIdx; }
                uint8_t getRedOn() const { return _rOn; }
                uint8_t getGreenOn() const { return _gOn; }
                uint8_t getBlueOn() const { return _bOn; }
                uint8_t getRedOff() const { return _rOff; }
                uint8_t getGreenOff() const { return _gOff; }
                uint8_t getBlueOff() const { return _bOff; }
        };

        LedRmt(gpio_num_t dataPin, uint8_t numLed);
        ~LedRmt();
        esp_err_t sendData();
        esp_err_t updateFrame(uint8_t idx, uint8_t red, uint8_t green, uint8_t blue);
        esp_err_t updateFrame(LedRgb led);
        void setColor(LedRgb &led, LedState state, uint8_t red, uint8_t green, uint8_t blue);
        void setLum(LedRgb &led, LedState state, uint8_t lum);
        void setState(LedRgb &led, LedState state);
    private:
        rmt_channel_handle_t _channel = nullptr;
        rmt_encoder_handle_t _encoder = nullptr;
        gpio_num_t _dataPin;
        uint8_t _numLed;
        std::vector<uint8_t> _txBuffer;
};

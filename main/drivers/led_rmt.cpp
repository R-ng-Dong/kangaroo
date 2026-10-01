#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "led_rmt.h"


#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)


LedRmt::LedRgb::LedRgb(uint8_t idx, uint8_t groupIdx):
    _idx(idx),
    _groupIdx(groupIdx)
{
    _rOn = 0;
    _gOn = 0x0f;
    _bOn = 0;
    _rOff = 0;
    _gOff = 0;
    _bOff = 0x0f;
    _lumOn = 0;
    _lumOff = 0;
    _state = LedState::OFF;
}
void LedRmt::LedRgb::setValue(LedState state, uint8_t lum, uint8_t red, uint8_t green, uint8_t blue)
{
    if (state == LedState::ON) {
        _rOn = red;
        _gOn = green;
        _bOn = blue;
        _lumOn = lum;
    } else {
        _rOff = red;
        _gOff = green;
        _bOff = blue;
        _lumOff = lum;
    }
}

void LedRmt::LedRgb::setLum(LedState state, uint8_t lum)
{   
    if(lum > 100) lum = 100;
    if (state == LedState::ON) {
        _lumOn = lum;
        _rOn = _rOn * lum /100;
        _gOn = _gOn * lum /100;
        _bOn = _bOn * lum /100;
    } else {
        _lumOff = lum;
        _rOff = _rOff * lum /100;
        _gOff = _gOff * lum /100;
        _bOff = _bOff * lum /100;
    }
}
void LedRmt::LedRgb::setColor(LedState state, uint8_t red, uint8_t green, uint8_t blue)
{
    if (state == LedState::ON) {
        _rOn = red;
        _gOn = green;
        _bOn = blue;
    } else {
        _rOff = red;
        _gOff = green;
        _bOff = blue;
    }
}

LedRmt::LedRmt(gpio_num_t dataPin, uint8_t numLed):
    _channel(nullptr), _encoder(nullptr), _dataPin(dataPin), _numLed(numLed), _txBuffer(numLed * 3, 0) // 3 bytes per LED (GRB)
{
    rmt_tx_channel_config_t tx_chan_config = {};
    tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT; // select source clock
    tx_chan_config.gpio_num = dataPin;
    tx_chan_config.mem_block_symbols = 64; // increase the block size can make the LED less flickering
    tx_chan_config.resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ;
    tx_chan_config.trans_queue_depth = 4; // set the number of transactions that can be pending in the background

    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &_channel));
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };

    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &_encoder));
    ESP_ERROR_CHECK(rmt_enable(_channel)); 
}

LedRmt::~LedRmt()
{
    if (_channel) {
        rmt_disable(_channel);
        rmt_del_channel(_channel);
    }
    if (_encoder) {
        rmt_del_encoder(_encoder);
    }
}

esp_err_t LedRmt::sendData()
{
    rmt_transmit_config_t transmit_config = {
        .loop_count = 0, // number of times to repeat the transmission, 0 means no repeat
    };
    esp_err_t ret = ESP_OK;
    ret = rmt_transmit(_channel, _encoder, _txBuffer.data(), _txBuffer.size(), &transmit_config);
    ret = rmt_tx_wait_all_done(_channel, pdMS_TO_TICKS(200));
    return ret;
}

esp_err_t LedRmt::updateFrame(uint8_t idx, uint8_t red, uint8_t green, uint8_t blue){
    if (idx >= _numLed) {
        return ESP_ERR_INVALID_ARG;
    }
    // GRB format
    _txBuffer[idx * 3] = green; 
    _txBuffer[idx * 3 + 1] = red;
    _txBuffer[idx * 3 + 2] = blue;
    return ESP_OK;
}

esp_err_t LedRmt::updateFrame(LedRgb led){
    if (led.getState() == LedState::ON) {
        return updateFrame(led.getIndex(), led.getRedOn(), led.getGreenOn(), led.getBlueOn());
    } else {
        return updateFrame(led.getIndex(), led.getRedOff(), led.getGreenOff(), led.getBlueOff());
    }
}

void LedRmt::setColor(LedRgb &led, LedState state, uint8_t red, uint8_t green, uint8_t blue)
{ 
    led.setColor(state, red, green, blue); 
}

void LedRmt::setLum(LedRgb &led, LedState state, uint8_t lum)
{ 
    led.setLum(state, lum); 
}

void LedRmt::setState(LedRgb &led, LedState state) 
{ 
    led.setState(state); 
}
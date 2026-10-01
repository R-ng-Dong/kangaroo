#include "led_mgmt.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

// macro delay_us
#define SLEEP_US(us) esp_rom_delay_us(us)

#define LED_DATA_BUTTON_PIN GPIO_NUM_4
#define LED_BUTTON_NUM 8
#define LED_DATA_SCREEN_PIN GPIO_NUM_20
#define LED_SCREEN_NUM 1

#define LED_0 BUTTON_INDEX_1
#define LED_1 BUTTON_INDEX_2
#define LED_2 BUTTON_INDEX_4
#define LED_3 BUTTON_INDEX_3

// static void handleBlinkLed(void *arg);

/*=====================================================================
                              LED BUTTON
=====================================================================*/
LedButton *LedButton::instance = nullptr;

LedButton *LedButton::getInstance()
{
    if (instance == nullptr)
    {
        instance = new LedButton();
    }
    return instance;
}

void LedButton::deleteInstance()
{
    delete instance;
    instance = nullptr;
}
/*
- Ở đây phải khởi tạo luôn constructor của LedRmt vì LedRmt là base class của LedButton, nếu không sẽ bị lỗi linker do thiếu định nghĩa constructor của LedRmt.
- _leds là vector object, nó chưa tồn tại lúc vào constructor của LedButton nên ko cần gọi lúc khởi tạo
- Nếu _leds là object trực tiếp trong class thì cũng phải gọi luôn constructor giống LedRmt
*/
LedButton::LedButton() : LedRmt(LED_DATA_BUTTON_PIN, LED_BUTTON_NUM)
{

}

esp_err_t LedButton::ledButtonSetState(uint8_t ledBtnIdx, LedState state)
{
    for (auto &led : _leds)
    {
        if (led.getGroupIndex() == ledBtnIdx)
        {
            led.setState(state);
            if (LedRmt::updateFrame(led) != ESP_OK)
            {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    return LedRmt::sendData();
}
esp_err_t LedButton::ledButtonSetLum(uint8_t ledBtnIdx, LedState state, uint8_t lum)
{
    for (auto &led : _leds)
    {
        if (led.getGroupIndex() == ledBtnIdx)
        {
            led.setLum(state, lum);
            if (LedRmt::updateFrame(led) != ESP_OK)
            {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    return LedRmt::sendData();
}
esp_err_t LedButton::ledButtonSetColor(uint8_t ledBtnIdx, LedState state, uint8_t red, uint8_t green, uint8_t blue)
{
    for (auto &led : _leds)
    {
        if (led.getGroupIndex() == ledBtnIdx)
        {
            led.setColor(state, red, green, blue);
            if (LedRmt::updateFrame(led) != ESP_OK)
            {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    return LedRmt::sendData();
}

esp_err_t LedButton::ledButtonSetColor(uint8_t ledBtnIdx, LedState state, uint8_t red, uint8_t green, uint8_t blue, uint8_t lum)
{
    for (auto &led : _leds)
    {
        if (led.getGroupIndex() == ledBtnIdx)
        {
            led.setColor(state, red, green, blue);
            led.setLum(state, lum);
            if (LedRmt::updateFrame(led) != ESP_OK)
            {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    return LedRmt::sendData();
}

esp_err_t LedButton::ledButtonInit(uint8_t ledBtnIdx, LedState state, uint8_t lumOn, uint8_t redOn, uint8_t greenOn,
                                   uint8_t blueOn, uint8_t lumOff, uint8_t redOff, uint8_t greenOff, uint8_t blueOff)
{
    for (auto &led : _leds)
    {
        if (led.getGroupIndex() == ledBtnIdx)
        {
            led.setState(state);
            led.setColor(LedState::ON, redOn, greenOn, blueOn);
            led.setColor(LedState::OFF, redOff, greenOff, blueOff);
            led.setLum(LedState::ON, lumOn);
            led.setLum(LedState::OFF, lumOff);
            if (LedRmt::updateFrame(led) != ESP_OK)
            {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    return ESP_OK;
    // return LedRmt::sendData();
}

esp_err_t LedButton::LedButtonBlink(BlinkMode mode, uint8_t ledBtnIdx, uint8_t count, uint32_t intervalMs)
{
    if (mode == BlinkMode::BLINK_MODE_NON_BLOCKING)
    {
        if (ledBtnIdx == BLINK_ALL_LED)
        {
            for (size_t i = 0; i < 4; i++)
            {
                blinkLedConfig[i].count = count * 2 + 1;
                blinkLedConfig[i].intervalMs = intervalMs;
                blinkLedConfig[i].lastTime = xTaskGetTickCount();
                for (auto &led : _leds)
                {
                    if (led.getGroupIndex() == i)
                    {
                        blinkLedConfig[i].oldState = led.getState();
                        break;
                    }
                }
            }
        }
        else
        {
            if (ledBtnIdx > 4)
                return ESP_ERR_INVALID_ARG;
            blinkLedConfig[ledBtnIdx].count = count * 2 + 1;
            blinkLedConfig[ledBtnIdx].intervalMs = intervalMs;
            blinkLedConfig[ledBtnIdx].lastTime = xTaskGetTickCount();
            for (auto &led : _leds)
            {
                if (led.getGroupIndex() == ledBtnIdx)
                {
                    blinkLedConfig[ledBtnIdx].oldState = led.getState();
                    break;
                }
            }
        }
    }
    else if (mode == BlinkMode::BLINK_MODE_BLOCKING)
    {
        uint16_t numCycle = count * 2;
        uint8_t isSendFrame = 0;

        std::vector<LedState> oldState;
        oldState.reserve(LED_BUTTON_NUM);
        for (auto &led : _leds)
        {
            oldState.emplace_back(led.getState());
        }

        while (numCycle > 0)
        {
            for (auto &led : _leds)
            {
                if (ledBtnIdx == BLINK_ALL_LED)
                {
                    if (numCycle % 2 == 0)
                        led.setState(LedState::ON);
                    else
                        led.setState(LedState::OFF);

                    if (LedRmt::updateFrame(led) != ESP_OK)
                    {
                        return ESP_ERR_INVALID_ARG;
                    }
                    isSendFrame = 1;
                }
                else
                {
                    if (led.getGroupIndex() == ledBtnIdx)
                    {
                        if (numCycle % 2 == 0)
                            led.setState(LedState::OFF);
                        else
                            led.setState(LedState::ON);

                        if (LedRmt::updateFrame(led) != ESP_OK)
                        {
                            return ESP_ERR_INVALID_ARG;
                        }
                        isSendFrame = 1;
                    }
                }
            }

            if (isSendFrame)
            {
                isSendFrame = 0;
                LedRmt::sendData();
            }
            numCycle--;
            SLEEP_US(intervalMs * 1000);
        }

        // reload led state
        for (auto &led : _leds)
        {
            if (ledBtnIdx == BLINK_ALL_LED)
            {
                led.setState(oldState[led.getIndex()]);
                if (LedRmt::updateFrame(led) != ESP_OK)
                {
                    return ESP_ERR_INVALID_ARG;
                }
            }
            else
            {
                if (led.getGroupIndex() == ledBtnIdx)
                {
                    led.setState(oldState[led.getIndex()]);
                    if (LedRmt::updateFrame(led) != ESP_OK)
                    {
                        return ESP_ERR_INVALID_ARG;
                    }
                }
            }
        }
        LedRmt::sendData();
    }
    return ESP_OK;
}

void LedButton::handleBlinkAdapter(void * arg)
{
    LedButton *ledButton = static_cast<LedButton *>(arg);
    ledButton->handleBlink(NULL);
}

void LedButton::handleBlink(void *arg)
{
    uint8_t isSendFrame = 0;
    while (1)
    {
        for (size_t i = 0; i < 4; i++)
        {
            if (blinkLedConfig[i].count > 0)
            {
                // if(xTaskGetTickCount() - blinkLedConfig[i].lastTime >= pdMS_TO_TICKS(blinkLedConfig[i].intervalMs))
                TickType_t elapse = 0;
                if (xTaskGetTickCount() >= blinkLedConfig[i].lastTime)
                    elapse = xTaskGetTickCount() - blinkLedConfig[i].lastTime;
                else
                    elapse = (portMAX_DELAY - blinkLedConfig[i].lastTime) + xTaskGetTickCount();
                if (elapse * portTICK_PERIOD_MS >= blinkLedConfig[i].intervalMs)
                {
                    for (auto &led : _leds)
                    {
                        if (led.getGroupIndex() == i)
                        {
                            if (blinkLedConfig[i].count == 1)
                            {
                                led.setState(blinkLedConfig[i].oldState);
                            }
                            else{
                                if (blinkLedConfig[i].count % 2 == 1)
                                    led.setState(LedState::ON);
                                else
                                    led.setState(LedState::OFF);
                            }
                            if (LedRmt::updateFrame(led) != ESP_OK)
                            {
                                // TODO
                            }
                            isSendFrame = 1;
                        }
                    }
                    blinkLedConfig[i].count--;
                    blinkLedConfig[i].lastTime = xTaskGetTickCount();
                }
            }
        }

        if(isSendFrame)
        {
            isSendFrame = 0;
            LedRmt::sendData();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*=====================================================================
                              LED LCD SCREEN
=====================================================================*/
LedScreen *LedScreen::instance = nullptr;

LedScreen *LedScreen::getInstance()
{
    if (instance == nullptr)
    {
        instance = new LedScreen();
    }
    return instance;
}

void LedScreen::deleteInstance()
{
    delete instance;
    instance = nullptr;
}

LedScreen::LedScreen() : LedRmt(LED_DATA_SCREEN_PIN, LED_SCREEN_NUM)
{
    for (uint8_t i = 0; i < 1; i++)
    {
        _leds.emplace_back(i, 0); // các LED còn lại ko gắn với button nào
    }
    for (auto &led : _leds)
    {
        led.setState(LedState::ON);
        updateFrame(led);
    }
    sendData();
}

esp_err_t LedScreen::ledScreenSetColor(uint8_t red, uint8_t green, uint8_t blue)
{
    for (auto &led : _leds)
    {
        led.setColor(LedState::ON, red, green, blue);
        if (updateFrame(led) != ESP_OK)
        {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return sendData();
}
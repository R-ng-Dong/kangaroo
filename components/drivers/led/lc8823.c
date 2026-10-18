#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lc8823.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/spi_master.h"

static const char* TAG = "lc8823";

typedef struct {
    UI8 lum; //lumen
    UI8 blue;
    UI8 green;
    UI8 red;
}LedData;

typedef struct{
    LedData ON;
    LedData OFF;
}LedOnOff;

/*======== LED Object=======*/
typedef struct{
    LedOnOff value;
    UI8 state;
    UI8 index; // led index
    UI8 element; // (ele1, ele2..., led_wifi)
    UI8 used;
}LedObject;

#define MUTEX_LOCK(x)                                           \
    do {                                                        \
        if (xSemaphoreTake(x, pdMS_TO_TICKS(1000)) != pdTRUE) { \
            ESP_LOGE(TAG, "mutex timeout");                     \
            return;                                             \
        }                                                       \
    } while(0)

#define MUTEX_UNLOCK(x)                         \
    do {                                        \
        if (xSemaphoreGive(x) != pdTRUE) {      \
            ESP_LOGE(TAG, "mutex give fail");   \
            return;                             \
        }                                       \
    } while(0)

#define LED_DATA_DEFAULT     GPIO_NUM_21   
#define LED_CLK_DEFAULT      GPIO_NUM_20 
#define FREQ_LED_DEFAULT     25000

#define EN_COLOR     0xff
#define DIS_COLOR    0x00
#define START_FRAME  0x00
#define END_FRAME    0xff

#define START_FRAME_SIZE 4
#define END_FRAME_SIZE 4

// macro chuyển đổi độ sáng LED theo format của led lc8823
#define LIGHT_SET(lum)  (0xE0 | (lum))
#define min2(a,b)	 ((a) < (b) ? (a) : (b))

#define HSPI_HOST    SPI2_HOST
static spi_device_handle_t spi;
static SemaphoreHandle_t spiMutex;

static UI8 *s_frame_buffer = NULL;
static UI8 s_frame_buffer_size = 0;
static LC8823Config lc8823_config = {
    .clk_pin = LED_CLK_DEFAULT,
    .data_pin = LED_DATA_DEFAULT,
    .freq = FREQ_LED_DEFAULT
};

static LedObject led_object[MAX_NUM_LED_OBJECT] = {0};

static void LC8823SpiInit(gpio_num_t clk_pin, gpio_num_t data_pin, uint32_t freq)
{
    // Cấu hình bus SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,      // MISO
        .mosi_io_num = data_pin,      // MOSI
        .sclk_io_num = clk_pin,       // SCLK
        .quadwp_io_num = -1,             // not use
        .quadhd_io_num = -1,             // not use
        .max_transfer_sz = 0 //SPI_BUF_MAX       
    };

    // Cấu hình thiết bị SPI
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = freq,        
        .mode = 0,         
        .spics_io_num = -1,   
        .queue_size = 2,
        .pre_cb = NULL,      // Không cần callback trước khi truyền
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
    };

    spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(HSPI_HOST, &devcfg, &spi);


    ESP_LOGI(TAG, "lc8823 spi init done!!");
}

static void SpiSendData(UI8 *data_send, UI8 length){
    if (data_send == NULL || length == 0) {
        return;
    }
    MUTEX_LOCK(spiMutex);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = length*8; 
    if(t.length<=32){
        t.flags = SPI_TRANS_USE_TXDATA; 
        for(size_t i = 0; i<length; i++){
            t.tx_data[i] = data_send[i];
        }
    }else{
        t.tx_buffer = data_send; 
    }

    if (!spiMutex)
    {
        ESP_LOGE(TAG, "mutex not init");
        return ;
    }
    esp_err_t ret = spi_device_transmit(spi, &t);
    MUTEX_UNLOCK(spiMutex);
    if (ret != ESP_OK) {
        ESP_LOGE("SPI", "SPI transaction failed: %s\n", esp_err_to_name(ret));
        return;
    }
}

esp_err_t LC8823InitFrameBuffer(const UI8 num_led){
    if(MAX_NUM_LED_OBJECT < num_led)
    {
        ESP_LOGE(TAG, "num_led must be <= %d", MAX_NUM_LED_OBJECT);
        return ESP_FAIL;
    }
    if(s_frame_buffer != NULL){
        free(s_frame_buffer);
        s_frame_buffer = NULL;
        s_frame_buffer_size = 0;
    }
    s_frame_buffer_size = START_FRAME_SIZE + num_led * sizeof(LedData) + END_FRAME_SIZE; // start frame + led data + end frame
    s_frame_buffer = (UI8 *) calloc(s_frame_buffer_size, sizeof(UI8));
    if(s_frame_buffer == NULL){
        ESP_LOGE(TAG, "frame buffer calloc fail");
        s_frame_buffer_size = 0;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t LC8823SendFrameBuffer(void){
    if(s_frame_buffer == NULL || s_frame_buffer_size == 0){
        ESP_LOGE(TAG, "frame buffer not init");
        return ESP_FAIL;
    }

    SpiSendData(s_frame_buffer, s_frame_buffer_size);
    return ESP_OK;
}

esp_err_t LC8823UpdateFrameBuffer(UI8 idx, LedData *data){
    if(s_frame_buffer == NULL || s_frame_buffer_size == 0){
        ESP_LOGE(TAG, "frame buffer not init");
        return ESP_FAIL;
    }
    UI8 *p = s_frame_buffer + 4 + idx * sizeof(LedData);
    p[0] = data->lum;
    p[1] = data->blue;
    p[2] = data->green;
    p[3] = data->red;
    return ESP_OK; // return LC8823SendFrameBuffer();
}


esp_err_t LC8823SetState(LedHandle led, UI8 state){
    if(led == NULL) return ESP_FAIL;
    LedObject *led_obj = (LedObject *)led;
    if(led_obj->state == state) return ESP_ERR_INVALID_ARG;
    led_obj->state = state;
    return LC8823UpdateFrameBuffer(led_obj->index, (state) ? &led_obj->value.ON : &led_obj->value.OFF);
}

esp_err_t LC8823SetColor(LedHandle led, UI8 onoff, UI8 red, UI8 green, UI8 blue){
    if(led == NULL) return ESP_FAIL;
    LedObject *led_obj = (LedObject *)led;
    if (onoff)
    {
        led_obj->value.ON.blue = blue;
        led_obj->value.ON.green = green;
        led_obj->value.ON.red = red;
    }
    else
    {
        led_obj->value.OFF.blue = blue;
        led_obj->value.OFF.green = green;
        led_obj->value.OFF.red = red;
    }
    UI8 stt_onoff = led_obj->state;
    return LC8823UpdateFrameBuffer(led_obj->index, (stt_onoff) ? &led_obj->value.ON : &led_obj->value.OFF);
}

esp_err_t LC8823SetLum(LedHandle led, UI8 onoff, UI8 lum){
    if(led == NULL) return ESP_FAIL;
    LedObject *led_obj = (LedObject *)led;
    ESP_LOGW("LC8823", "set lum: %02X", lum);
    if (onoff)
    {
        led_obj->value.ON.lum = LIGHT_SET(lum);
    }
    else
    {
        led_obj->value.OFF.lum = LIGHT_SET(lum);
    }
    UI8 stt_onoff = led_obj->state;
    return LC8823UpdateFrameBuffer(led_obj->index, (stt_onoff) ? &led_obj->value.ON : &led_obj->value.OFF);
}

// load data to led object
void LC8823LoadValue(LedHandle led, UI8 onoff, UI8 dim, UI8 red, UI8 green, UI8 blue){
    if(led == NULL) return;
    LedObject *led_obj = (LedObject *)led;
    UI8 lum = LIGHT_SET(LC8823ConvertDimToLum(dim));
    if (onoff)
    {
        led_obj->value.ON.lum = lum;
        led_obj->value.ON.blue = blue;
        led_obj->value.ON.green = green;
        led_obj->value.ON.red = red;
    }
    else
    {
        led_obj->value.OFF.lum = lum;
        led_obj->value.OFF.blue = blue;
        led_obj->value.OFF.green = green;
        led_obj->value.OFF.red = red;
    }
}

UI8 LC8823GetLum(LedHandle led, UI8 onoff){
    if(led == NULL) return 0;
    LedObject *led_obj = (LedObject *)led;
    return onoff ? led_obj->value.ON.lum : led_obj->value.OFF.lum;
}

UI8 LC8823GetRedColor(LedHandle led, UI8 onoff){
    if(led == NULL) return 0;
    LedObject *led_obj = (LedObject *)led;
    return onoff ? led_obj->value.ON.red : led_obj->value.OFF.red;
}

UI8 LC8823GetGreenColor(LedHandle led, UI8 onoff){
    if(led == NULL) return 0;
    LedObject *led_obj = (LedObject *)led;
    return onoff ? led_obj->value.ON.green : led_obj->value.OFF.green;
}

UI8 LC8823GetBlueColor(LedHandle led, UI8 onoff){
    if(led == NULL) return 0;
    LedObject *led_obj = (LedObject *)led;
    return onoff ? led_obj->value.ON.blue : led_obj->value.OFF.blue;
}

void LC8823Init(LC8823Config *config){
    if (!config)
    {
        ESP_LOGW(TAG, "lc8823 config is NULL, use config default");
    }else{
        lc8823_config.freq = min2(config->freq, 27000);
        lc8823_config.clk_pin = config->clk_pin;
        lc8823_config.data_pin = config->data_pin;
    }
    LC8823SpiInit(lc8823_config.clk_pin, lc8823_config.data_pin, lc8823_config.freq);
    spiMutex = xSemaphoreCreateMutex();
    if (spiMutex == NULL)
    {
        ESP_LOGE(TAG, "Init mutex SPI failure");
    }
    assert(spiMutex);
}

LedHandle LC8823Create(UI8 index, UI8 elm_id)
{
    if(MAX_NUM_LED_OBJECT <= index) return NULL;
    for (size_t i = 0; i < MAX_NUM_LED_OBJECT; i++)
    {
        if(!led_object[i].used)
        {
            led_object[i].used = 1;
            led_object[i].index = index;
            led_object[i].element = elm_id;
            return &led_object[i];
        }
    }
    return NULL;
}
#include "adc_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_adc/adc_oneshot.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "soc/gpio_sig_map.h"
#include "esp_rom_gpio.h"
#include "esp_private/rmt.h" 

#define TDS2_PULSE_GPIO1    2
#define TDS2_PULSE_GPIO2    10

#define TDS2_ADC_CHANNEL    ADC_CHANNEL_4
#define RMT_RESOLUTION_HZ   800000

#define SEG_TICKS           32000   // 40ms per segment
#define REMAIN_TICKS        16000

static rmt_channel_handle_t tx_chan;
static rmt_encoder_handle_t copy_encoder;
bool fanOutB = false; 
adc_oneshot_unit_handle_t adc1_handle;



rmt_symbol_word_t pulseA_symbols[] = {
    // --- HIGH phase (500ms) ---
    { .duration0 = SEG_TICKS, .level0 = 1, .duration1 = SEG_TICKS, .level1 = 1 },
    { .duration0 = SEG_TICKS, .level0 = 1, .duration1 = SEG_TICKS, .level1 = 1 },
    { .duration0 = SEG_TICKS, .level0 = 1, .duration1 = SEG_TICKS, .level1 = 1 },
    { .duration0 = SEG_TICKS, .level0 = 1, .duration1 = SEG_TICKS, .level1 = 1 },
    { .duration0 = SEG_TICKS, .level0 = 1, .duration1 = SEG_TICKS, .level1 = 1 },
    { .duration0 = SEG_TICKS, .level0 = 1, .duration1 = SEG_TICKS, .level1 = 1 },
    // remainder + transition to LOW
    { .duration0 = REMAIN_TICKS, .level0 = 1, .duration1 = SEG_TICKS, .level1 = 0 },
    // --- LOW phase (500ms) ---
    { .duration0 = SEG_TICKS, .level0 = 0, .duration1 = SEG_TICKS, .level1 = 0 },
    { .duration0 = SEG_TICKS, .level0 = 0, .duration1 = SEG_TICKS, .level1 = 0 },
    { .duration0 = SEG_TICKS, .level0 = 0, .duration1 = SEG_TICKS, .level1 = 0 },
    { .duration0 = SEG_TICKS, .level0 = 0, .duration1 = SEG_TICKS, .level1 = 0 },
    { .duration0 = SEG_TICKS, .level0 = 0, .duration1 = SEG_TICKS, .level1 = 0 },
    // remainder finish
    { .duration0 = SEG_TICKS, .level0 = 0, .duration1 = REMAIN_TICKS, .level1 = 0 },
};

void ADC_oneshot_init(void){
    //-------------ADC1 Init---------------//
    
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, TDS2_ADC_CHANNEL, &config));
}

void ADC_oneshot_read(int *raw)
{
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, TDS2_ADC_CHANNEL, raw));
}


void TDS_init(void)
{
    ADC_oneshot_init();

    gpio_config_t io_conf_out = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
        .pin_bit_mask = (1ULL << TDS2_PULSE_GPIO1 | 1ULL << TDS2_PULSE_GPIO2), 
        .pull_down_en = 0,
    };
    gpio_config(&io_conf_out);
    gpio_set_level(TDS2_PULSE_GPIO1, 0);
    gpio_set_level(TDS2_PULSE_GPIO2, 0);

    /*
     * ==========================================
     * GPIO MATRIX
     * ==========================================
     *
     * RMT CH0
     *    |
     *    +----------> GPIO2   normal
     *    |
     *    +----------> GPIO10  inverted
     */

    // esp_rom_gpio_connect_out_signal(
    //     TDS2_PULSE_GPIO2,
    //     RMT_SIG_OUT0_IDX,
    //     true,
    //     false
    // );
}


void detachFanOut(int gpio, bool *active, const char *label)
{
    if (*active == true) return;
    esp_rom_gpio_connect_out_signal(gpio, SIG_GPIO_OUT_IDX, false, false);
    *active = false;
    printf("[RMT] Fan-out released \n");
}

void stopPulses()
{
    if (tx_chan) { rmt_disable(tx_chan); rmt_del_channel(tx_chan); tx_chan = NULL; }
    if (copy_encoder) { rmt_del_encoder(copy_encoder); copy_encoder = NULL; }

    // The fan-out pin is still matrix-routed to RMT output; release it so
    // normal GPIO writes can work again.
    detachFanOut(TDS2_PULSE_GPIO2, &fanOutB, "B");

    gpio_config_t io_conf_out = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
        .pin_bit_mask = (1ULL << TDS2_PULSE_GPIO1 | 1ULL << TDS2_PULSE_GPIO2), 
        .pull_down_en = 0,
    };
    gpio_config(&io_conf_out);

    gpio_set_level(TDS2_PULSE_GPIO1, 0);
    gpio_set_level(TDS2_PULSE_GPIO2, 0);

    printf("[RMT] Both pulse channels stopped \n");
}

bool createPulseChannel()
{
    // =====================================================
    // RMT TX channel
    // =====================================================
    esp_err_t err;

    rmt_tx_channel_config_t tx_config = {
        .gpio_num = TDS2_PULSE_GPIO1,
        .clk_src = RMT_CLK_SRC_APB, //RMT_CLK_SRC_DEFAULT, //RMT_CLK_SRC_REF_TICK //RMT_CLK_SRC_APB
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    tx_config.flags.invert_out = false;
    tx_config.flags.io_loop_back = false;
    tx_config.flags.io_od_mode = false;
    err = rmt_new_tx_channel(&tx_config, &tx_chan);
    if (err != ESP_OK)
    {
        printf("[RMT] ERROR: rmt_new_tx_channel \n");
        return false;
    }

    // =====================================================
    // Copy encoder
    // =====================================================

    rmt_copy_encoder_config_t encoder_config = {};

    err = rmt_new_copy_encoder(&encoder_config, &copy_encoder);
    if (err != ESP_OK)
    {
        printf("[RMT] ERROR: rmt_new_copy_encoder( \n");
        return false;
    }
    err = rmt_enable(tx_chan);
    if (err != ESP_OK)
    {
        printf("[RMT] ERROR: rmt_enable( \n");
        return false;
    }

    return true;
}

void startPulses()
{
    esp_err_t err;

    if (!createPulseChannel()) { stopPulses(); return; }
    int channelId = 0;
    err = rmt_get_channel_id(tx_chan, &channelId);
    uint32_t sig = (uint32_t)(RMT_SIG_OUT0_IDX + channelId);
    esp_rom_gpio_connect_out_signal(TDS2_PULSE_GPIO2, sig, true /* invert */, false);
    // --- Probe 1: RMT channel on GPIO2, fan out GPIO10 ---
    fanOutB = true;

    // --- Transmit with infinite loop ---
    rmt_transmit_config_t txCfg = {};
    txCfg.loop_count = -1;  // -1 = infinite loop

    err = rmt_transmit(tx_chan, copy_encoder, pulseA_symbols, sizeof(pulseA_symbols), &txCfg);
    if (err != ESP_OK)
    {
        printf("[RMT] ERROR: rmt_transmit(A) failed: %s\n", esp_err_to_name(err));
        stopPulses();
        return;
    }

    printf("[RMT] pulse channels started simultaneously\n");
}

// ============================================================
// READ ADC — OVERSAMPLED, FRACTIONAL COUNTS
// ============================================================

void readADC()
{
    int raw_data[16];
    for (size_t i = 0; i < 16; i++)
    {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, TDS2_ADC_CHANNEL, &raw_data[i]));
    }
    printf("raw: ");
    for (size_t i = 0; i < 16; i++)
    {
        printf("%d ", raw_data[i]);
    }
    printf("\n");
}

void measureTDS(void)
{
    stopPulses();

    gpio_set_level(TDS2_PULSE_GPIO1, 1);
    gpio_set_level(TDS2_PULSE_GPIO2, 0);
    // esp_rom_delay_us(500);
    vTaskDelay(110 / portTICK_PERIOD_MS);
    readADC();

    gpio_set_level(TDS2_PULSE_GPIO1, 0);
    gpio_set_level(TDS2_PULSE_GPIO2, 1);
    // esp_rom_delay_us(500);
    vTaskDelay(110 / portTICK_PERIOD_MS);
    readADC();

    gpio_set_level(TDS2_PULSE_GPIO1, 0);
    gpio_set_level(TDS2_PULSE_GPIO2, 0);

    startPulses();
}

static void tdsTask(void * param)
{
    TDS_init();
    startPulses();
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        measureTDS();
    }
}

void TDS_test(void)
{
    xTaskCreate(tdsTask, "tdsTask", 2048*2, NULL, configMAX_PRIORITIES - 3, NULL);
}
#include "tds.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "soc/gpio_sig_map.h"
#include "esp_rom_gpio.h"
#include "esp_private/rmt.h"

#define TDS_RMT_RESOLUTION_HZ 800000
#define TDS_SEG_TICKS         32000U
#define TDS_REMAIN_TICKS      16000U
#define TDS_DEFAULT_SAMPLES   16U
#define TDS_ADC_BUFFER_SIZE   256U

#define ADC_SAMPLE_COUNT    16
#define ADC_TRIM_EDGES      2  

#define POLARITY_PERIOD_MS   500
#define SETTLING_TIME_MS     100
#define TDS_PULSE_RESET_MS   60000U


#define R_REF_OHM       1000.0f

#define CELL_CONSTANT_1 0.508f     // probe 1 (IO2/IO10 branch) - base K0
#define CELL_CONSTANT_2 0.972f     // probe 2 (IO5/IO6 branch)  - base K0
#define R_OFFSET_1      -13563.0f  // probe 1 - polarization ADDED to R (hence negative)
#define R_OFFSET_2      -8923.0f   // probe 2 - polarization ADDED to R (hence negative)

#define TDS_FACTOR      1.0f //0.5f //1.0f    // EC(uS/cm) -> TDS(ppm), per this pen's scale
#define TEMP_COEFF      0.02f
#define WATER_TEMP_C    25.0f

static const char *TAG = "TDS";

#define CAL_N 40

static const float CAL_R[CAL_N] = {
    1418.8f, 2049.5f, 2096.6f, 2126.4f, 2186.6f,
    2323.8f, 2414.4f, 2728.8f, 2926.8f, 3084.4f,
    3301.8f, 3571.3f, 4196.2f, 4352.4f, 4512.1f,
    4712.6f, 4856.6f, 5720.0f, 5880.4f, 6470.4f,
    6977.3f, 7239.5f, 7593.9f, 7803.3f, 8219.5f,
    8694.2f, 8911.3f, 9698.9f, 9883.7f, 10197.5f,
    11045.6f, 12295.5f, 13487.0f, 16053.9f, 18601.1f,
    22924.2f, 31202.2f, 43012.6f, 68751.6f, 129344.8f
};

static const float CAL_TDS[CAL_N] = {
    955.0f, 516.0f, 501.0f, 489.0f, 468.0f,
    427.0f, 402.0f, 350.0f, 320.0f, 298.0f,
    281.0f, 252.0f, 220.0f, 201.0f, 190.0f,
    186.0f, 178.0f, 153.0f, 149.0f, 121.0f,
    115.0f, 111.0f, 109.0f, 104.0f, 100.0f,
    95.0f, 93.0f, 89.0f, 85.0f, 82.0f,
    77.0f, 72.0f, 67.0f, 59.0f, 53.0f,
    45.0f, 37.0f, 29.0f, 23.5f, 14.0f
};

typedef struct {
    gpio_num_t pulse1_pin;
    gpio_num_t pulse2_pin;
    AdcChannel adc_channel;
    TdsSensorId sensor_id;
    UI8 fan_out_active;
    rmt_channel_handle_t rmt_channel;
    rmt_encoder_handle_t rmt_encoder;
    bool channel_enabled;
    bool pulse_gpio_mode;
    bool pulses_running;
    // UI16 pos_value; // positive excitation
    // UI16 neg_value; // negative excitation
    UI16 tds_value; // TDS value in ppm
} TdsConfig;

static TdsConfig s_tds_configs[TDS_MAX_SENSOR_COUNT];
static size_t s_tds_count = 0; //must be less than or equal to TDS_MAX_SENSOR_COUNT
static TdsPinConfig s_tds_saved_config[TDS_MAX_SENSOR_COUNT];
static bool s_tds_has_saved_config;
TaskHandle_t xTask_tds = NULL;
static volatile bool s_tds_stop_requested = false;
// ============================================================
// RMT SYMBOL DATA
//
// rmt_symbol_word_t field order: duration0, level0, duration1, level1
//
// Each probe uses ONE RMT channel carrying this pattern:
//   HIGH(500ms) -> LOW(500ms), repeat forever
// and the complement is produced by the GPIO matrix (out_inv = true),
// so pulseB_symbols[] is no longer needed.
//
// HIGH phase: 6 symbols x (32000+32000) + 1 x (16000+32000) = 400,000 ticks
// LOW phase:  5 symbols x (32000+32000) + 1 x (32000+16000) = 400,000 ticks
// Total: 13 symbols per channel
// ============================================================

static rmt_symbol_word_t s_pulse_symbols[] = {
    { .duration0 = TDS_SEG_TICKS, .level0 = 1, .duration1 = TDS_SEG_TICKS, .level1 = 1 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 1, .duration1 = TDS_SEG_TICKS, .level1 = 1 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 1, .duration1 = TDS_SEG_TICKS, .level1 = 1 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 1, .duration1 = TDS_SEG_TICKS, .level1 = 1 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 1, .duration1 = TDS_SEG_TICKS, .level1 = 1 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 1, .duration1 = TDS_SEG_TICKS, .level1 = 1 },
    { .duration0 = TDS_REMAIN_TICKS, .level0 = 1, .duration1 = TDS_SEG_TICKS, .level1 = 0 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 0, .duration1 = TDS_SEG_TICKS, .level1 = 0 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 0, .duration1 = TDS_SEG_TICKS, .level1 = 0 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 0, .duration1 = TDS_SEG_TICKS, .level1 = 0 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 0, .duration1 = TDS_SEG_TICKS, .level1 = 0 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 0, .duration1 = TDS_SEG_TICKS, .level1 = 0 },
    { .duration0 = TDS_SEG_TICKS, .level0 = 0, .duration1 = TDS_REMAIN_TICKS, .level1 = 0 },
};

static TdsConfig *TdsGetConfig(TdsSensorId sensor_id)
{
    if((TDS_SENSOR_0 != sensor_id && TDS_SENSOR_1 != sensor_id) || 0 == s_tds_count)
        return NULL;
    for (size_t i = 0; i < TDS_MAX_SENSOR_COUNT; i++) {
        if (s_tds_configs[i].sensor_id == sensor_id) {
            return &s_tds_configs[i];
        }
    }
    return NULL;
}

static void TdsDetachFanOut(TdsSensorId sensor_id)
{
    TdsConfig *temp = TdsGetConfig(sensor_id);
    if (NULL == temp) return;
    
    if (temp->fan_out_active) {
        esp_rom_gpio_connect_out_signal(temp->pulse2_pin, SIG_GPIO_OUT_IDX, false, false);
        temp->fan_out_active = 0;
    }
}

static esp_err_t TdsAttachFanOut(TdsSensorId sensor_id)
{
    TdsConfig *temp = TdsGetConfig(sensor_id);
    if (NULL == temp) return ESP_ERR_INVALID_ARG;

    int channel_id = 0;
    esp_err_t err = rmt_get_channel_id(temp->rmt_channel, &channel_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_get_channel_id failed for sensorID: %d %s", sensor_id,  esp_err_to_name(err));
        return err;
    }

    uint32_t sig = (uint32_t)(RMT_SIG_OUT0_IDX + channel_id);
    esp_rom_gpio_connect_out_signal(temp->pulse2_pin, sig, true, false);
    temp->fan_out_active = 1;
    return ESP_OK;
}

static esp_err_t TdsCreatePulseChannel(TdsSensorId sensor_id)
{
    TdsConfig *temp = TdsGetConfig(sensor_id);
    if (NULL == temp) return ESP_ERR_INVALID_ARG;

    if (temp->rmt_channel != NULL) {
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_config = {
        .gpio_num = temp->pulse1_pin,
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = TDS_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 4,
    };

    tx_config.flags.invert_out = false;
    tx_config.flags.io_loop_back = false;
    tx_config.flags.io_od_mode = false;

    esp_err_t err = rmt_new_tx_channel(&tx_config, &temp->rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed for sensor %zu: %s", sensor_id, esp_err_to_name(err));
        return err;
    }

    rmt_copy_encoder_config_t encoder_config = {};
    err = rmt_new_copy_encoder(&encoder_config, &temp->rmt_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder failed for sensor %zu: %s", sensor_id, esp_err_to_name(err));
        rmt_del_channel(temp->rmt_channel);
        temp->rmt_channel = NULL;
        return err;
    }

    err = rmt_enable(temp->rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed for sensor %zu: %s", sensor_id, esp_err_to_name(err));
        rmt_del_encoder(temp->rmt_encoder);
        rmt_del_channel(temp->rmt_channel);
        temp->rmt_encoder = NULL;
        temp->rmt_channel = NULL;
        return err;
    }

    temp->channel_enabled = true;
    return ESP_OK;
}

esp_err_t TdsStopPulses(TdsSensorId sensor_id)
{
    TdsConfig *temp = TdsGetConfig(sensor_id);
    if (NULL == temp) return ESP_ERR_INVALID_ARG;

    if (temp->rmt_channel != NULL && temp->channel_enabled) {
        esp_err_t err = rmt_disable(temp->rmt_channel);
        if (err != ESP_OK) return err;
        temp->channel_enabled = false;
        temp->pulses_running = false;
    }
    if (temp->rmt_channel != NULL) {
        esp_err_t err = rmt_tx_wait_all_done(temp->rmt_channel, 0);
        if (err != ESP_OK) return err;
    }

    TdsDetachFanOut(sensor_id);
    esp_rom_gpio_connect_out_signal(temp->pulse1_pin, SIG_GPIO_OUT_IDX, false, false);
    temp->pulse_gpio_mode = true;
    esp_err_t err = gpio_set_direction(temp->pulse1_pin, GPIO_MODE_OUTPUT);
    if (err == ESP_OK) err = gpio_set_direction(temp->pulse2_pin, GPIO_MODE_OUTPUT);
    if (err == ESP_OK) err = gpio_set_pull_mode(temp->pulse1_pin, GPIO_FLOATING);
    if (err == ESP_OK) err = gpio_set_pull_mode(temp->pulse2_pin, GPIO_FLOATING);
    if (err == ESP_OK) err = gpio_set_level(temp->pulse1_pin, 0);
    if (err == ESP_OK) err = gpio_set_level(temp->pulse2_pin, 0);
    temp->pulses_running = false;
    return err;
}

static esp_err_t TdsStopInternal(TdsSensorId sensor_id)
{
    TdsConfig *temp = TdsGetConfig(sensor_id);
    if (NULL == temp) return ESP_ERR_INVALID_ARG;

    esp_err_t err = TdsStopPulses(sensor_id);
    if (err != ESP_OK) return err;
    if (temp->rmt_channel != NULL) {
        err = rmt_del_channel(temp->rmt_channel);
        if (err != ESP_OK) return err;
        temp->rmt_channel = NULL;
    }
    if (temp->rmt_encoder != NULL) {
        err = rmt_del_encoder(temp->rmt_encoder);
        if (err != ESP_OK) return err;
        temp->rmt_encoder = NULL;
    }
    return ESP_OK;
}

esp_err_t TdsConfigure(gpio_num_t pulse1_pin, gpio_num_t pulse2_pin, AdcChannel adc_channel, TdsSensorId sensor_id)
{
    if(TDS_SENSOR_0 != sensor_id && TDS_SENSOR_1 != sensor_id)
        return ESP_ERR_INVALID_ARG;
    if (TDS_MAX_SENSOR_COUNT <= s_tds_count)
        return ESP_FAIL;

    static UI8 initialized = 1;
    if(initialized)
    {
        memset(s_tds_configs, 0, sizeof(s_tds_configs));
        s_tds_configs[0].sensor_id = TDS_SENSOR_UNKNOWN;
        s_tds_configs[1].sensor_id = TDS_SENSOR_UNKNOWN;
        s_tds_count = 0;
        initialized = 0;
    }

    TdsConfig *temp = NULL;
    for (size_t i = 0; i < TDS_MAX_SENSOR_COUNT; i++) {
        if (TDS_SENSOR_UNKNOWN == s_tds_configs[i].sensor_id) {
            temp = &s_tds_configs[i];
            break;
        }
    }
    if (NULL == temp) {
        return ESP_FAIL;
    }

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
        .pin_bit_mask = (1ULL << pulse1_pin) | (1ULL << pulse2_pin),
        .pull_down_en = 0,
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed for sensor %zu: %s", sensor_id, esp_err_to_name(err));
        return err;
    }

    gpio_set_level(pulse1_pin, 0);
    gpio_set_level(pulse2_pin, 0);
    temp->pulse1_pin = pulse1_pin;
    temp->pulse2_pin = pulse2_pin;
    temp->adc_channel = adc_channel;
    temp->sensor_id = sensor_id;
    temp->fan_out_active = 0;
    temp->rmt_channel = NULL;
    temp->rmt_encoder = NULL;
    temp->channel_enabled = false;
    temp->pulse_gpio_mode = false;
    temp->pulses_running = false;
    temp->tds_value = 0U;

    s_tds_count++;

    return AdcOneshotInit(adc_channel, DRV_ADC_ATTEN_DB_12, DRV_ADC_BITWIDTH_12);
}

esp_err_t TdsStartPulses(TdsSensorId sensor_id)
{
    TdsConfig *temp = TdsGetConfig(sensor_id);
    if (NULL == temp) {
        return ESP_ERR_INVALID_ARG;
    }

    if (temp->pulses_running) return ESP_OK;
    esp_err_t err = TdsCreatePulseChannel(sensor_id);
    if (err != ESP_OK) return err;
    if (temp->pulse_gpio_mode) {
        err = rmt_tx_switch_gpio(temp->rmt_channel, temp->pulse1_pin, false);
        if (err != ESP_OK) return err;
        temp->pulse_gpio_mode = false;
    }
    err = TdsAttachFanOut(sensor_id);
    if (err != ESP_OK) return err;
    if (!temp->channel_enabled) {
        err = rmt_enable(temp->rmt_channel);
        if (err != ESP_OK) return err;
        temp->channel_enabled = true;
    }

    rmt_transmit_config_t tx_cfg = { .loop_count = -1 };
    err = rmt_transmit(temp->rmt_channel, temp->rmt_encoder, s_pulse_symbols, sizeof(s_pulse_symbols), &tx_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed for sensor %d: %s", sensor_id, esp_err_to_name(err));
        TdsStopPulses(sensor_id);
        return err;
    }
    temp->pulses_running = true;
    return ESP_OK;
}

UI16 TdsGetValue(TdsSensorId sensor_id)
{
    TdsConfig *temp = TdsGetConfig(sensor_id);
    if (NULL == temp) {
        return 0;
    }
    return temp->tds_value;
}

typedef struct
{
    uint32_t start_us[ADC_SAMPLE_COUNT];
    uint32_t end_us[ADC_SAMPLE_COUNT];
    UI16 raw[ADC_SAMPLE_COUNT];
    UI16 count;
    int64_t phase_start_us;
} TdsSampleTrace;

// Diagnostic storage stays off the measurement task's stack.
static TdsSampleTrace s_sample_trace[TDS_MAX_SENSOR_COUNT][2];
static bool s_sample_trace_done;

static void TdsPrintSampleTrace(void)
{
    for (UI8 sensor = 0; sensor < TDS_MAX_SENSOR_COUNT; sensor++) {
        for (UI8 phase = 0; phase < 2; phase++) {
            const TdsSampleTrace *trace = &s_sample_trace[sensor][phase];
            for (UI16 i = 0; i < trace->count; i++) {
                ESP_LOGI(TAG, "ADC trace %s %c[%u]: raw=%u start=%lu end=%lu read=%lu us",
                         sensor == TDS_SENSOR_0 ? "inlet" : "ro", phase == 0 ? '+' : '-',
                         (unsigned)i, (unsigned)trace->raw[i],
                         (unsigned long)trace->start_us[i], (unsigned long)trace->end_us[i],
                         (unsigned long)(trace->end_us[i] - trace->start_us[i]));
            }
        }
    }
}

static esp_err_t TdsReadRawSamples(TdsSensorId sensor_id, UI16 *samples, UI16 sample_count, int64_t *last_sample_us, TdsSampleTrace *trace)
{
    TdsConfig *temp = TdsGetConfig(sensor_id);
    if (NULL == temp || NULL == samples || 0 == sample_count) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(samples, 0, sample_count * sizeof(UI16));
    for (UI16 i = 0; i < sample_count; i++) {
        int64_t sample_start_us = trace != NULL ? esp_timer_get_time() : 0;
        esp_err_t err = AdcOneshotRead(temp->adc_channel, &samples[i]);
        if (err != ESP_OK) {
            return err;
        }
        *last_sample_us = esp_timer_get_time();
        if (trace != NULL) {
            trace->start_us[i] = (uint32_t)(sample_start_us - trace->phase_start_us);
            trace->end_us[i] = (uint32_t)(*last_sample_us - trace->phase_start_us);
            trace->raw[i] = samples[i];
            trace->count = i + 1;
        }
    }
    return ESP_OK;
}

static esp_err_t TdsReadAverageRaw(TdsSensorId sensor_id, float *average_raw, int64_t *last_sample_us, TdsSampleTrace *trace)
{
    if (NULL == average_raw) {
        return ESP_ERR_INVALID_ARG;
    }
    UI16 samples[ADC_SAMPLE_COUNT] = {0};
    esp_err_t err = TdsReadRawSamples(sensor_id, samples, ADC_SAMPLE_COUNT, last_sample_us, trace);
    if (err != ESP_OK) {
        return err;
    }
    // Sort the samples to remove outliers
    for (UI16 i = 1; i < ADC_SAMPLE_COUNT; i++) {
        UI16 value = samples[i];
        UI16 j = i;
        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            j--;
        }
        samples[j] = value;
    }
    // Calculate the average of the middle samples, trimming edges
    UI32 sum = 0;
    for (UI16 i = ADC_TRIM_EDGES; i < ADC_SAMPLE_COUNT - ADC_TRIM_EDGES; i++) {
        sum += samples[i];
    }
    *average_raw = (float)sum / (ADC_SAMPLE_COUNT - 2 * ADC_TRIM_EDGES);

    return ESP_OK;
}


/*============================================================
                    Calib
============================================================*/
#if 1
float AdcToResistance(float adc_positive, float adc_negative)
{
    float resistance = 0.0f;
    const float ADC_MAX = 4095.0f;
    // Measured floors (see log_zero.txt): a genuine 0 V reads 0.00 counts,
    // but ~5-10 counts couple in when the OTHER probe's pin is at 3.3 V
    // (~400 MOhm surface leakage between wires). Floor = 4 counts, chosen
    // so probe 2's 1 ppm reading (4.2 counts) still reports as water inside
    // the 10-15% spec; below that a reading merges with the coupling floor
    // and reports OPEN. (An open probe IS infinite resistance - correct
    // answer for wiring faults; ultra-pure water < ~0.5 ppm (probe 1) /
    // ~1 ppm (probe 2) cannot be told apart from a disconnected probe at
    // R_REF = 1k.)
    const float MIN_COUNTS   = 0.5f;  // electrically at a rail
    const float OPEN_FLOOR   = 4.0f;  // coupling/zero floor

    // printf("calc \n");
    if (adc_positive >= 4090.0f && adc_negative <= OPEN_FLOOR)
    {
        // Negative phase reads ~0 and positive phase is at the top rail.
        //
        // NOTE the polarity: during the negative phase the junction sits at
        // 3.3V × R1/(R1+R_sensor), so R_sensor -> INFINITY gives ADC- -> 0.
        // Zero on ADC- therefore means OPEN (very high resistance), not short!
        resistance = 1.0e9f;
        // printf(" sensor OPEN (or TDS below ~1 ppm)\n");
    }
    else if (adc_negative >= 4090.0f && adc_positive <= OPEN_FLOOR)
    {
        // Symmetric case: ADC- at the top rail and ADC+ ~0 means
        // R_sensor -> 0, i.e. SHORTED.
        resistance = 0.1f;
        // printf(" sensor SHORTED\n");
    }
    else if (adc_negative > MIN_COUNTS)
    {
        // Use negative excitation: V = 3.3V × R1/(R1+R_sensor)
        // ADC = 4095 × R1/(R1+R_sensor)
        // R_sensor = R1 × (4095/ADC − 1)
        resistance = R_REF_OHM * (ADC_MAX / adc_negative - 1.0f);
        // printf(" using ADC- = %f\n", adc_negative);
        // if (adc_negative < 15.0f)
        //     printf("  [range] ADC- < 15 counts: below ~2-4 ppm - sub-count reading, verify low end against reference\n");
        // else if (adc_negative >= 4000.0f)
        //     printf("  [range] ADC- >= 4000 counts: near short / extremely high TDS\n");
    }
    else if (adc_positive > MIN_COUNTS && adc_positive < 4090.0f)
    {
        // ADC- dead but ADC+ usable - should not happen with a real sensor
        // (ADC- ~0 means open). Kept as a diagnostic fallback.
        resistance = R_REF_OHM * adc_positive / (ADC_MAX - adc_positive);
        printf(" using ADC+ = %f\n", adc_positive);
        printf(" (ADC- ~0 - inconsistent, check wiring)\n");
    }
    else
    {
        // Neither phase usable (floating pin / stopped excitation).
        resistance = 1.0e9f;
        // printf(" sensor OPEN (no valid pair)\n");
    }

    return resistance;    
}
#else
float AdcToResistance(uint16_t adc)
{
    if (adc == 0 || adc >= 4095) 
        return 1.0e9f;
    return R_REF_OHM * ((float)adc) / (4095.0f - (float)adc);
}
#endif

float ResistanceToEC(float resistance, float cellConstant)
{
    // With a NEGATIVE R_OFFSET a fault input (short: R ~ 0) can push the
    // sum below zero - clamp to 1 ohm so a short reads over-range (huge)
    // instead of 0 ppm.
    if (resistance < 1.0f) resistance = 1.0f;
    return cellConstant * 1000000.0f / resistance;
}

float TemperatureCompensateEC(float ec, float temperature)
{
    float compensation = 1.0f + TEMP_COEFF * (temperature - 25.0f);
    if (compensation <= 0.0f) compensation = 1.0f;
    return ec / compensation;
}

float EcToTDS(float ec)
{
    return ec * TDS_FACTOR;
}

bool cal_clipped = false;
static float table_tds(float resistance)
{
    float tds;
    int i;

    if (resistance < CAL_R[0]) {
        tds = CAL_TDS[0]
            + (CAL_R[0] - resistance)
            * (CAL_TDS[0] - CAL_TDS[1])
            / (CAL_R[1] - CAL_R[0]);

        cal_clipped = true;
    }
    else if (resistance > CAL_R[CAL_N - 1]) {
        tds = CAL_TDS[CAL_N - 1]
            + (resistance - CAL_R[CAL_N - 1])
            * (CAL_TDS[CAL_N - 1] - CAL_TDS[CAL_N - 2])
            / (CAL_R[CAL_N - 1] - CAL_R[CAL_N - 2]);

        cal_clipped = true;
    }
    else {
        cal_clipped = false;
        i = 0;

        while (i < CAL_N - 2 && resistance > CAL_R[i + 1]) {
            i++;
        }

        float f = (resistance - CAL_R[i])
                / (CAL_R[i + 1] - CAL_R[i]);

        tds = CAL_TDS[i]
            + f * (CAL_TDS[i + 1] - CAL_TDS[i]);
    }

    return tds < 0.0f ? 0.0f : tds;
}

//============================================================
esp_err_t TdsDeinit(TdsSensorId sensor_id)
{
    TdsConfig *temp = TdsGetConfig(sensor_id);
    if (NULL == temp) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = TdsStopInternal(sensor_id);
    if (err != ESP_OK) return err;
    gpio_reset_pin(temp->pulse1_pin);
    gpio_reset_pin(temp->pulse2_pin);
    temp->sensor_id = TDS_SENSOR_UNKNOWN;
    s_tds_count--;
    return ESP_OK;
}

float TdsCorrectValue(float measured_ppm)
{
    // Shared empirical calibration for the current sampling timing.
    // Zero is preserved; values above the measured range use the last slope.
    static const float measured[] = {0.0f, 28.0f, 45.55f, 75.0f, 105.0f, 337.0f, 395.0f};
    static const float reference[] = {0.0f, 16.0f, 40.0f, 60.0f, 80.0f, 180.0f, 200.0f};
    const size_t count = sizeof(measured) / sizeof(measured[0]);
    if (measured_ppm <= 0.0f) return 0.0f;

    size_t i = 0;
    while (i < count - 2 && measured_ppm > measured[i + 1]) {
        i++;
    }
    float fraction = (measured_ppm - measured[i]) / (measured[i + 1] - measured[i]);
    float corrected_ppm = reference[i] + fraction * (reference[i + 1] - reference[i]);
    if (corrected_ppm > (float)UINT16_MAX) return (float)UINT16_MAX;
    return corrected_ppm;
}

#define TDS_SLEEP_US 100
#define TDS_MEASURE_PERIOD_US 1000000LL
#define TDS_LOG_PERIOD_US 10000000LL
static esp_err_t TdsReadPair(TdsConfig *sensor, float *positive, float *negative,
                             int64_t *positive_us, int64_t *negative_us)
{
    esp_err_t err = gpio_set_level(sensor->pulse1_pin, 1);
    if (err == ESP_OK) err = gpio_set_level(sensor->pulse2_pin, 0);
    int64_t phase_start_us = esp_timer_get_time();
    int64_t last_sample_us = phase_start_us;
    TdsSampleTrace *positive_trace = s_sample_trace_done ? NULL : &s_sample_trace[sensor->sensor_id][0];
    TdsSampleTrace *negative_trace = s_sample_trace_done ? NULL : &s_sample_trace[sensor->sensor_id][1];
    if (positive_trace != NULL) {
        positive_trace->phase_start_us = phase_start_us;
        positive_trace->count = 0;
        negative_trace->count = 0;
    }
    if (err == ESP_OK) {
        esp_rom_delay_us(TDS_SLEEP_US);
        err = TdsReadAverageRaw(sensor->sensor_id, positive, &last_sample_us, positive_trace);
        *positive_us = last_sample_us - phase_start_us;
    }
    if (err == ESP_OK) err = gpio_set_level(sensor->pulse1_pin, 0);
    if (err == ESP_OK) err = gpio_set_level(sensor->pulse2_pin, 1);
    phase_start_us = esp_timer_get_time();
    last_sample_us = phase_start_us;
    if (negative_trace != NULL) negative_trace->phase_start_us = phase_start_us;
    if (err == ESP_OK) {
        esp_rom_delay_us(TDS_SLEEP_US);
        err = TdsReadAverageRaw(sensor->sensor_id, negative, &last_sample_us, negative_trace);
        *negative_us = last_sample_us - phase_start_us;
    }
    esp_err_t low1_err = gpio_set_level(sensor->pulse1_pin, 0);
    esp_err_t low2_err = gpio_set_level(sensor->pulse2_pin, 0);
    if (err == ESP_OK) err = low1_err;
    if (err == ESP_OK) err = low2_err;
    return err;
}

static void TdsTask(void * param)
{
    TdsConfig *sensor_1 = TdsGetConfig(TDS_SENSOR_0);
    TdsConfig *sensor_2 = TdsGetConfig(TDS_SENSOR_1);
    if(NULL == sensor_1 || NULL == sensor_2)
    {
        xTask_tds = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t start1_err = TdsStartPulses(TDS_SENSOR_0);
    esp_err_t start2_err = TdsStartPulses(TDS_SENSOR_1);
    ESP_LOGW(TAG, "===>>> TdsTask created sucessfully");
    vTaskDelay(pdMS_TO_TICKS(3000));

    s_sample_trace_done = false;
    memset(s_sample_trace, 0, sizeof(s_sample_trace));
    int64_t last_log_us = 0;
    while (!s_tds_stop_requested) {
        float pos_value1 = 0.0f; float neg_value1 = 0.0f;
        float pos_value2 = 0.0f; float neg_value2 = 0.0f;
        int64_t positive1_us = 0; int64_t negative1_us = 0;
        int64_t positive2_us = 0; int64_t negative2_us = 0;
        esp_err_t stop1_err = TdsStopPulses(TDS_SENSOR_0);
        esp_err_t stop2_err = TdsStopPulses(TDS_SENSOR_1);
        esp_err_t read1_err = ESP_ERR_INVALID_STATE;
        esp_err_t read2_err = ESP_ERR_INVALID_STATE;

        // Keep both probes quiet while measuring either probe.
        if (start1_err == ESP_OK && start2_err == ESP_OK &&
            stop1_err == ESP_OK && stop2_err == ESP_OK) {
            read1_err = TdsReadPair(sensor_1, &pos_value1, &neg_value1, &positive1_us, &negative1_us);
            read2_err = TdsReadPair(sensor_2, &pos_value2, &neg_value2, &positive2_us, &negative2_us);
        }
        start1_err = TdsStartPulses(TDS_SENSOR_0);
        start2_err = TdsStartPulses(TDS_SENSOR_1);
        int64_t pulse_start_us = esp_timer_get_time();
        bool log_due = last_log_us == 0 || pulse_start_us - last_log_us >= TDS_LOG_PERIOD_US;

        if (read1_err == ESP_OK && start1_err == ESP_OK && start2_err == ESP_OK) {
            float resistance1 = AdcToResistance(pos_value1, neg_value1);
            float measured_ppm = table_tds(resistance1);
            float corrected_ppm = measured_ppm;
            corrected_ppm = TdsCorrectValue(corrected_ppm); // Comment out to use raw TDS.
            sensor_1->tds_value = (UI16)(corrected_ppm + 0.5f);
            if (log_due) {
                ESP_LOGI(TAG, "inlet: ADC+=%.2f ADC-=%.2f R=%.1f ohm raw_TDS=%.2f TDS=%u ppm sample_end=+%lld/-%lld us",
                         (double)pos_value1, (double)neg_value1, (double)resistance1,
                         (double)measured_ppm,
                         (unsigned)sensor_1->tds_value,
                         (long long)positive1_us, (long long)negative1_us);
            }
        }
        if (read2_err == ESP_OK && start1_err == ESP_OK && start2_err == ESP_OK) {
            float resistance2 = AdcToResistance(pos_value2, neg_value2);
            float measured_ppm = table_tds(resistance2);
            float corrected_ppm = measured_ppm;
            corrected_ppm = TdsCorrectValue(corrected_ppm); // Comment out to use raw TDS.
            sensor_2->tds_value = (UI16)(corrected_ppm + 0.5f);
            if (log_due) {
                ESP_LOGI(TAG, "ro: ADC+=%.2f ADC-=%.2f R=%.1f ohm raw_TDS=%.2f TDS=%u ppm sample_end=+%lld/-%lld us",
                         (double)pos_value2, (double)neg_value2, (double)resistance2,
                         (double)measured_ppm,
                         (unsigned)sensor_2->tds_value,
                         (long long)positive2_us, (long long)negative2_us);
            }
        }
        if (log_due && (read1_err != ESP_OK || read2_err != ESP_OK ||
            start1_err != ESP_OK || start2_err != ESP_OK)) {
            ESP_LOGW(TAG, "TDS measurement failed: read=%s/%s, start=%s/%s",
                     esp_err_to_name(read1_err), esp_err_to_name(read2_err),
                     esp_err_to_name(start1_err), esp_err_to_name(start2_err));
        }
        if (!s_sample_trace_done && read1_err == ESP_OK && read2_err == ESP_OK &&
            start1_err == ESP_OK && start2_err == ESP_OK) {
            s_sample_trace_done = true;
            TdsPrintSampleTrace();
        }
        if (log_due) last_log_us = pulse_start_us;
        // Anchor the next measurement to the RMT restart, excluding log time.
        while (!s_tds_stop_requested) {
            int64_t remaining_us = TDS_MEASURE_PERIOD_US - (esp_timer_get_time() - pulse_start_us);
            if (remaining_us <= 0) break;
            if (remaining_us > 3000) {
                TickType_t delay_ticks = pdMS_TO_TICKS((remaining_us - 2000) / 1000);
                vTaskDelay(delay_ticks > 0 ? delay_ticks : 1);
            }
            else {
                esp_rom_delay_us((uint32_t)remaining_us);
            }
        }
    }

    // Finish the current driver operation, then release owned resources here.
    TdsStopInternal(TDS_SENSOR_0);
    TdsStopInternal(TDS_SENSOR_1);
    TdsDeinit(TDS_SENSOR_0);
    TdsDeinit(TDS_SENSOR_1);
    AdcOneshotDeinit();
    s_tds_stop_requested = false;
    xTask_tds = NULL;
    vTaskDelete(NULL);
}

void TdsTaskDeinit(void)
{
    if (xTask_tds != NULL)
    {
        if (xTask_tds == xTaskGetCurrentTaskHandle())
        {
            ESP_LOGE(TAG, "TdsTaskDeinit cannot be called from the TDS task");
            return;
        }
        s_tds_stop_requested = true;
        // Let the task finish any blocking ADC/RMT call and clean up its resources.
        while (xTask_tds != NULL) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return;
    }

    if (TdsGetConfig(TDS_SENSOR_0) != NULL)
    {
        TdsDeinit(TDS_SENSOR_0);
    }
    if (TdsGetConfig(TDS_SENSOR_1) != NULL)
    {
        TdsDeinit(TDS_SENSOR_1);
    }

    esp_err_t err = AdcOneshotDeinit();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to deinitialize ADC: %s", esp_err_to_name(err));
    }
}

esp_err_t TdsTaskResume(void)
{
    if (xTask_tds != NULL)
    {
        return ESP_OK;
    }
    if (!s_tds_has_saved_config)
    {
        return ESP_ERR_INVALID_STATE;
    }

    TdsPinConfig config[TDS_MAX_SENSOR_COUNT];
    memcpy(config, s_tds_saved_config, sizeof(config));
    return TdsInit(config);
}

esp_err_t TdsInit(TdsPinConfig config[2])
{
    s_tds_stop_requested = false;
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (xTask_tds != NULL || s_tds_count != 0U)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if(config[0].sensor_id == config[1].sensor_id
        || TDS_SENSOR_UNKNOWN == config[0].sensor_id
        || TDS_SENSOR_UNKNOWN == config[1].sensor_id)
        return ESP_ERR_INVALID_ARG;

    memcpy(s_tds_saved_config, config, sizeof(s_tds_saved_config));
    s_tds_has_saved_config = true;

    esp_err_t err = TdsConfigure(config[0].pulse1_pin, config[0].pulse2_pin, config[0].adc_channel, config[0].sensor_id);
    if (err != ESP_OK) {
        TdsTaskDeinit();
        return err;
    }
    err = TdsConfigure(config[1].pulse1_pin, config[1].pulse2_pin, config[1].adc_channel, config[1].sensor_id);
    if (err != ESP_OK) {
        TdsTaskDeinit();
        return err;
    }

    BaseType_t ret = xTaskCreate(TdsTask, "TdsTask", 4096, NULL, configMAX_PRIORITIES - 3, &xTask_tds);
    if (pdPASS != ret)
    {
        ESP_LOGE(TAG, "Failed to create TDS task");
        xTask_tds = NULL;
        TdsTaskDeinit();
        return ESP_FAIL;
    }    
    return ESP_OK;
}

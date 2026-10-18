#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

#include "esp_err.h"
#include "common.h"

typedef enum {
    DRV_ADC_CHANNEL_0,     ///< ADC channel
    DRV_ADC_CHANNEL_1,     ///< ADC channel
    DRV_ADC_CHANNEL_2,     ///< ADC channel
    DRV_ADC_CHANNEL_3,     ///< ADC channel
    DRV_ADC_CHANNEL_4,     ///< ADC channel
    DRV_ADC_CHANNEL_5,     ///< ADC channel
    DRV_ADC_CHANNEL_6,     ///< ADC channel
    DRV_ADC_CHANNEL_7,     ///< ADC channel
    DRV_ADC_CHANNEL_8,     ///< ADC channel
    DRV_ADC_CHANNEL_9,     ///< ADC channel
    DRV_ADC_CHANNEL_10,    ///< ADC channel
} AdcChannel;

typedef enum {
    DRV_ADC_ATTEN_DB_0   = 0,  ///<No input attenuation, ADC can measure up to approx.
    DRV_ADC_ATTEN_DB_2_5 = 1,  ///<The input voltage of ADC will be attenuated extending the range of measurement by about 2.5 dB
    DRV_ADC_ATTEN_DB_6   = 2,  ///<The input voltage of ADC will be attenuated extending the range of measurement by about 6 dB
    DRV_ADC_ATTEN_DB_12  = 3,  ///<The input voltage of ADC will be attenuated extending the range of measurement by about 12 dB
} AdcAtten;

typedef enum {
    DRV_ADC_BITWIDTH_DEFAULT = 0, ///< Default ADC output bits, max supported width will be selected
    DRV_ADC_BITWIDTH_9  = 9,      ///< ADC output width is 9Bit
    DRV_ADC_BITWIDTH_10 = 10,     ///< ADC output width is 10Bit
    DRV_ADC_BITWIDTH_11 = 11,     ///< ADC output width is 11Bit
    DRV_ADC_BITWIDTH_12 = 12,     ///< ADC output width is 12Bit
    DRV_ADC_BITWIDTH_13 = 13,     ///< ADC output width is 13Bit
} AdcBitwith;

esp_err_t AdcContinousInit(AdcChannel *channel, UI8 channel_num, UI32 max_buff_size, UI32 conv_frame_size, UI32 freq_hz);
esp_err_t AdcReadRawData(UI8 *result, UI16 length, UI32 *ret_num);
esp_err_t AdcContinousDeinit(void);
esp_err_t AdcContinousFlushPool(void);

esp_err_t AdcOneshotInit(AdcChannel channel, AdcAtten atten, AdcBitwith bitwith);
esp_err_t AdcOneshotRead(AdcChannel channel, UI16 *raw_data);
esp_err_t AdcOneshotDeinit(void);
esp_err_t AdcOneshotReconfigure(AdcChannel channel, AdcAtten atten, AdcBitwith bitwith);


#endif /*ADC_DRIVER_H*/
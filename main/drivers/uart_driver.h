#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include "driver/uart.h"
#include <driver/gpio.h>

typedef struct {
    gpio_num_t tx_pin;
    gpio_num_t rx_pin;
    uart_port_t uart_num;
    int baudrate;
    uint16_t txrx_buff_size;
    uint8_t* data_buffer;
    void (*reset_state)(void);
    void (*process_data)(const uint8_t *data_buff, uint8_t len);
}UartConfig_t;

esp_err_t UartInit(const UartConfig_t *uart_config);
esp_err_t UartWriteBytes(uart_port_t port_num, uint8_t *data, size_t size);
int       UartReadBytes(uart_port_t port_num, uint8_t *data, size_t size);
esp_err_t UartClearBuffer(uart_port_t port_num);

#endif /*UART_DRIVER_H*/
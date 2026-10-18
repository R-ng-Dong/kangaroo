#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include "common.h"
#include "driver/uart.h"
#include <driver/gpio.h>

typedef struct {
    gpio_num_t tx_pin;
    gpio_num_t rx_pin;
    uart_port_t uart_num;
    int baudrate;
    UI16 txrx_buff_size;
    UI8 *data_buffer;
    void (*reset_state)(void);
    void (*process_data)(const UI8 *data_buff, UI8 len);
} UartConfig;

esp_err_t UartInit(const UartConfig *uart_config);
esp_err_t UartWriteBytes(uart_port_t port_num, UI8 *data, size_t size);
int       UartReadBytes(uart_port_t port_num, UI8 *data, size_t size);
esp_err_t UartClearBuffer(uart_port_t port_num);

#endif /*UART_DRIVER_H*/
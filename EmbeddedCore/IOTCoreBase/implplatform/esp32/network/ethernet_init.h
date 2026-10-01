/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once

#include "esp_eth_driver.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "IOT_DeviceConfiguration.h"
// #define CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET 1 // temp
// #define CONFIG_ETH_USE_SPI_ETHERNET 1
// #define CONFIG_EXAMPLE_USE_SPI_ETHERNET 1
// #ifdef CONFIG_EXAMPLE_USE_SPI_ETHERNET || CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
//     #define CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO 19
//     #define CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO 22
//     #define CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO 21
//     #define CONFIG_EXAMPLE_ETH_SPI_HOST 
// #endif // CONFIG_EXAMPLE_USE_SPI_ETHERNET || CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
/**
 * @brief Initialize Ethernet driver based on Espressif IoT Development Framework Configuration
 *
 * @param[out] eth_handles_out array of initialized Ethernet driver handles
 * @param[out] eth_cnt_out number of initialized Ethernets
 * @return
 *          - ESP_OK on success
 *          - ESP_ERR_INVALID_ARG when passed invalid pointers
 *          - ESP_ERR_NO_MEM when there is no memory to allocate for Ethernet driver handles array
 *          - ESP_FAIL on any other failure
 */
esp_err_t example_eth_init(esp_eth_handle_t *eth_handles_out[], uint8_t *eth_cnt_out);

#ifdef __cplusplus
}
#endif

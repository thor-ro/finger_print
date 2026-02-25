/**
 * @file sdf_mock_linux_drivers.h
 * @brief Mock types and defines for UART/GPIO when building for Linux host.
 *
 * Included by sdf_drivers.c in place of driver/uart.h and driver/gpio.h.
 */
#ifndef SDF_MOCK_LINUX_DRIVERS_H
#define SDF_MOCK_LINUX_DRIVERS_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/* --------------- UART types & defines --------------- */

#define UART_NUM_MAX 3
#define UART_DATA_8_BITS 0
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 0
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1

typedef int uart_port_t;
typedef struct {
  int baud_rate;
  int data_bits;
  int parity;
  int stop_bits;
  int flow_ctrl;
  int source_clk;
} uart_config_t;

int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length,
                    uint32_t ticks_to_wait);
esp_err_t uart_flush_input(uart_port_t uart_num);
int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size);
esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
                              int tx_buffer_size, int queue_size,
                              void *uart_queue, int intr_alloc_flags);
esp_err_t uart_param_config(uart_port_t uart_num,
                            const uart_config_t *uart_config);
esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
                       int rts_io_num, int cts_io_num);
esp_err_t uart_driver_delete(uart_port_t uart_num);

/* --------------- GPIO types & defines --------------- */

typedef int gpio_num_t;
typedef struct {
  uint64_t pin_bit_mask;
  int mode;
  int pull_up_en;
  int pull_down_en;
  int intr_type;
} gpio_config_t;

#define GPIO_MODE_OUTPUT 0
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0

esp_err_t gpio_config_mock(const gpio_config_t *pGPIOConfig);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);

/* Redirect gpio_config calls to mock to avoid name collision with the struct */
#define gpio_config gpio_config_mock

#endif /* SDF_MOCK_LINUX_DRIVERS_H */

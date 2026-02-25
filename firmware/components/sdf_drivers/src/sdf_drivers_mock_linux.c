/**
 * @file sdf_drivers_mock_linux.c
 * @brief Linux host mock implementations of UART and GPIO driver functions.
 */
#ifdef CONFIG_IDF_TARGET_LINUX

#include "sdf_mock_linux_drivers.h"

/* --------------- UART mock functions --------------- */

int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length,
                    uint32_t ticks_to_wait) {
  (void)uart_num;
  (void)buf;
  (void)length;
  (void)ticks_to_wait;
  return -1;
}

esp_err_t uart_flush_input(uart_port_t uart_num) {
  (void)uart_num;
  return ESP_OK;
}

int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size) {
  (void)uart_num;
  (void)src;
  return (int)size;
}

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
                              int tx_buffer_size, int queue_size,
                              void *uart_queue, int intr_alloc_flags) {
  (void)uart_num;
  (void)rx_buffer_size;
  (void)tx_buffer_size;
  (void)queue_size;
  (void)uart_queue;
  (void)intr_alloc_flags;
  return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t uart_num,
                            const uart_config_t *uart_config) {
  (void)uart_num;
  (void)uart_config;
  return ESP_OK;
}

esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
                       int rts_io_num, int cts_io_num) {
  (void)uart_num;
  (void)tx_io_num;
  (void)rx_io_num;
  (void)rts_io_num;
  (void)cts_io_num;
  return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t uart_num) {
  (void)uart_num;
  return ESP_OK;
}

/* --------------- GPIO mock functions --------------- */

#undef gpio_config
esp_err_t gpio_config_mock(const gpio_config_t *pGPIOConfig) {
  (void)pGPIOConfig;
  return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level) {
  (void)gpio_num;
  (void)level;
  return ESP_OK;
}

#endif /* CONFIG_IDF_TARGET_LINUX */

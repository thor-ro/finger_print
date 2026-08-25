/**
 * @file sdf_mock_linux_drivers.h
 * @brief Mock types and defines for UART/GPIO when building for Linux host.
 *
 * Included by sdf_drivers.c in place of driver/uart.h and driver/gpio.h.
 */
#ifndef SDF_MOCK_LINUX_DRIVERS_H
#define SDF_MOCK_LINUX_DRIVERS_H

#include "sdkconfig.h"

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/* Shared GPIO mock types */
#include "sdf_mock_linux_gpio.h"

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

/* --------------- Test-only control hooks --------------- */

/* Makes uart_read_bytes() block for delay_ms (still reporting "no data")
 * before returning, so owner-task tests can simulate a slow sensor
 * round-trip and observe how concurrent fp_* callers are serialized. 0
 * (the default) preserves the old immediate-failure behavior. Not used by
 * any production code path. */
void sdf_mock_uart_set_read_delay_ms(uint32_t delay_ms);

/* Scripts the sensor's next reply: the queued bytes are consumed by the
 * following uart_read_bytes() calls (a real fp_* response frame arrives in
 * exactly this piecemeal fashion via fp_uart_read_exact()), and once drained
 * reads revert to "no data". Queueing a well-formed ACK frame therefore lets
 * host tests drive fp_delete_user()/fp_delete_all_users() - and the
 * sdf_services mutation paths behind them - through their success paths,
 * which the plain no-data mock could never do. A second call replaces the
 * pending reply. Not used by any production code path. */
void sdf_mock_uart_queue_response(const uint8_t *data, size_t len);

/* Cumulative bytes handed to uart_write_bytes() since the last reset. Tests
 * assert this stays unchanged to prove an operation was refused before any
 * sensor command went out on the wire. */
uint32_t sdf_mock_uart_write_count(void);

/* Clears the queued reply and both counters (including the read delay), so
 * tests start from a deterministic UART state regardless of what earlier
 * suites did. */
void sdf_mock_uart_reset(void);

#endif /* SDF_MOCK_LINUX_DRIVERS_H */

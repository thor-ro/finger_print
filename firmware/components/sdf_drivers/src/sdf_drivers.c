#include "sdf_drivers.h"

#include <string.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"
#include "driver/uart.h"
#else
// Linux Host Mocks
#define UART_NUM_MAX 3
#define UART_DATA_8_BITS 0
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 0
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1

typedef int uart_port_t;
typedef int gpio_num_t;
typedef struct {
  int baud_rate;
  int data_bits;
  int parity;
  int stop_bits;
  int flow_ctrl;
  int source_clk;
} uart_config_t;
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

static inline int uart_read_bytes(uart_port_t uart_num, void *buf,
                                  uint32_t length, uint32_t ticks_to_wait) {
  return -1;
}
static inline esp_err_t uart_flush_input(uart_port_t uart_num) {
  return ESP_OK;
}
static inline int uart_write_bytes(uart_port_t uart_num, const void *src,
                                   size_t size) {
  return size;
}
static inline esp_err_t uart_driver_install(uart_port_t uart_num,
                                            int rx_buffer_size,
                                            int tx_buffer_size, int queue_size,
                                            void *uart_queue,
                                            int intr_alloc_flags) {
  return ESP_OK;
}
static inline esp_err_t uart_param_config(uart_port_t uart_num,
                                          const uart_config_t *uart_config) {
  return ESP_OK;
}
static inline esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num,
                                     int rx_io_num, int rts_io_num,
                                     int cts_io_num) {
  return ESP_OK;
}
static inline esp_err_t uart_driver_delete(uart_port_t uart_num) {
  return ESP_OK;
}

static inline esp_err_t gpio_config(const gpio_config_t *pGPIOConfig) {
  return ESP_OK;
}
static inline esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level) {
  return ESP_OK;
}
#endif

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define SDF_FP_FRAME_LEN 8u
#define SDF_FP_MARKER 0xF5u

#define SDF_FP_CMD_ENROLL_1 0x01u
#define SDF_FP_CMD_ENROLL_2 0x02u
#define SDF_FP_CMD_ENROLL_3 0x03u
#define SDF_FP_CMD_DELETE_USER 0x04u
#define SDF_FP_CMD_DELETE_ALL_USERS 0x05u
#define SDF_FP_CMD_MATCH_1_N 0x0Cu

#define SDF_FP_MUTEX_WAIT_MS 250u

static const char *TAG = "sdf_fingerprint_drv";

typedef struct {
  bool initialized;
  int uart_port;
  int power_en_pin;
  uint32_t response_timeout_ms;
  SemaphoreHandle_t lock;
} sdf_fingerprint_driver_state_t;

static sdf_fingerprint_driver_state_t s_state = {
    .initialized = false,
    .uart_port = -1,
    .power_en_pin = -1,
    .response_timeout_ms = 0,
    .lock = NULL,
};

static uint8_t sdf_fingerprint_checksum(const uint8_t frame[SDF_FP_FRAME_LEN]) {
  uint8_t checksum = 0;
  for (size_t i = 1; i <= 5; ++i) {
    checksum ^= frame[i];
  }
  return checksum;
}

static bool sdf_fingerprint_user_id_valid(uint16_t user_id) {
  return user_id >= SDF_FINGERPRINT_USER_ID_MIN &&
         user_id <= SDF_FINGERPRINT_USER_ID_MAX;
}

static esp_err_t sdf_fingerprint_uart_read_exact(int uart_port, uint8_t *buffer,
                                                 size_t expected_len,
                                                 uint32_t timeout_ms) {
  if (buffer == NULL || expected_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t offset = 0;
  int64_t deadline_us = esp_timer_get_time() + ((int64_t)timeout_ms * 1000LL);

  while (offset < expected_len) {
    int64_t now_us = esp_timer_get_time();
    if (now_us >= deadline_us) {
      return ESP_ERR_TIMEOUT;
    }

    uint32_t remaining_ms = (uint32_t)((deadline_us - now_us + 999LL) / 1000LL);
    TickType_t read_timeout_ticks = pdMS_TO_TICKS(remaining_ms);
    if (read_timeout_ticks == 0) {
      read_timeout_ticks = 1;
    }

    int read_now = uart_read_bytes((uart_port_t)uart_port, buffer + offset,
                                   expected_len - offset, read_timeout_ticks);
    if (read_now < 0) {
      return ESP_FAIL;
    }

    offset += (size_t)read_now;
  }

  return ESP_OK;
}

static esp_err_t
sdf_fingerprint_send_command_locked(uint8_t cmd, uint8_t p1, uint8_t p2,
                                    uint8_t p3,
                                    uint8_t response[SDF_FP_FRAME_LEN]) {
  if (!s_state.initialized || response == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t request[SDF_FP_FRAME_LEN] = {
      SDF_FP_MARKER, cmd, p1, p2, p3, 0x00, 0x00, SDF_FP_MARKER,
  };
  request[6] = sdf_fingerprint_checksum(request);

  uart_flush_input((uart_port_t)s_state.uart_port);

  int written = uart_write_bytes((uart_port_t)s_state.uart_port, request,
                                 sizeof(request));
  if (written != (int)sizeof(request)) {
    return ESP_FAIL;
  }

  esp_err_t err = sdf_fingerprint_uart_read_exact(s_state.uart_port, response,
                                                  SDF_FP_FRAME_LEN,
                                                  s_state.response_timeout_ms);
  if (err != ESP_OK) {
    return err;
  }

  if (response[0] != SDF_FP_MARKER || response[7] != SDF_FP_MARKER) {
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (response[1] != cmd) {
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint8_t expected_checksum = sdf_fingerprint_checksum(response);
  if (response[6] != expected_checksum) {
    return ESP_ERR_INVALID_CRC;
  }

  return ESP_OK;
}

static sdf_fingerprint_op_result_t
sdf_fingerprint_map_ack_code(uint8_t ack_code) {
  switch (ack_code) {
  case SDF_FINGERPRINT_ACK_SUCCESS:
    return SDF_FINGERPRINT_OP_OK;
  case SDF_FINGERPRINT_ACK_TIMEOUT:
    return SDF_FINGERPRINT_OP_TIMEOUT;
  case SDF_FINGERPRINT_ACK_FULL:
    return SDF_FINGERPRINT_OP_FULL;
  case SDF_FINGERPRINT_ACK_USER_OCCUPIED:
    return SDF_FINGERPRINT_OP_USER_OCCUPIED;
  case SDF_FINGERPRINT_ACK_FINGER_OCCUPIED:
    return SDF_FINGERPRINT_OP_FINGER_OCCUPIED;
  case SDF_FINGERPRINT_ACK_FAIL:
  case SDF_FINGERPRINT_ACK_NOUSER:
  default:
    return SDF_FINGERPRINT_OP_FAILED;
  }
}

void sdf_drivers_init(void) {}

void sdf_fingerprint_driver_set_power(bool enabled) {
  if (s_state.power_en_pin < 0) {
    return;
  }
  gpio_set_level((gpio_num_t)s_state.power_en_pin, enabled ? 1 : 0);
}

esp_err_t
sdf_fingerprint_driver_init(const sdf_fingerprint_driver_config_t *config) {
  if (config == NULL || config->uart_port < 0 ||
      config->uart_port >= UART_NUM_MAX || config->baud_rate == 0 ||
      config->tx_pin < 0 || config->rx_pin < 0 ||
      config->response_timeout_ms == 0 || config->rx_buffer_size < 64 ||
      config->tx_buffer_size < 64) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_state.lock == NULL) {
    s_state.lock = xSemaphoreCreateMutex();
    if (s_state.lock == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_FP_MUTEX_WAIT_MS)) !=
      pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return ESP_OK;
  }

  uart_config_t uart_config = {
      .baud_rate = (int)config->baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t err = uart_driver_install((uart_port_t)config->uart_port,
                                      config->rx_buffer_size,
                                      config->tx_buffer_size, 0, NULL, 0);
  if (err != ESP_OK) {
    xSemaphoreGive(s_state.lock);
    return err;
  }

  err = uart_param_config((uart_port_t)config->uart_port, &uart_config);
  if (err != ESP_OK) {
    uart_driver_delete((uart_port_t)config->uart_port);
    xSemaphoreGive(s_state.lock);
    return err;
  }

  err = uart_set_pin((uart_port_t)config->uart_port, config->tx_pin,
                     config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    uart_driver_delete((uart_port_t)config->uart_port);
    xSemaphoreGive(s_state.lock);
    return err;
  }

  if (config->power_en_pin >= 0) {
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << config->power_en_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_config);
    gpio_set_level((gpio_num_t)config->power_en_pin, 1);
  }

  s_state.initialized = true;
  s_state.uart_port = config->uart_port;
  s_state.power_en_pin = config->power_en_pin;
  s_state.response_timeout_ms = config->response_timeout_ms;
  xSemaphoreGive(s_state.lock);

  ESP_LOGI(TAG, "Fingerprint UART initialized (port=%d, baud=%u, tx=%d, rx=%d)",
           config->uart_port, (unsigned)config->baud_rate, config->tx_pin,
           config->rx_pin);

  return ESP_OK;
}

void sdf_fingerprint_driver_deinit(void) {
  if (s_state.lock == NULL) {
    return;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_FP_MUTEX_WAIT_MS)) !=
      pdTRUE) {
    return;
  }

  if (s_state.initialized) {
    if (s_state.power_en_pin >= 0) {
      gpio_set_level((gpio_num_t)s_state.power_en_pin, 0);
    }
    uart_driver_delete((uart_port_t)s_state.uart_port);
    s_state.initialized = false;
    s_state.uart_port = -1;
    s_state.power_en_pin = -1;
    s_state.response_timeout_ms = 0;
  }

  xSemaphoreGive(s_state.lock);
}

bool sdf_fingerprint_driver_is_ready(void) {
  if (s_state.lock == NULL) {
    return false;
  }

  bool ready = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_FP_MUTEX_WAIT_MS)) ==
      pdTRUE) {
    ready = s_state.initialized;
    xSemaphoreGive(s_state.lock);
  }
  return ready;
}

sdf_fingerprint_op_result_t
sdf_fingerprint_driver_match_1n(sdf_fingerprint_match_t *match) {
  if (match == NULL || s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_FP_MUTEX_WAIT_MS)) !=
      pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  uint8_t response[SDF_FP_FRAME_LEN] = {0};
  esp_err_t err = sdf_fingerprint_send_command_locked(SDF_FP_CMD_MATCH_1_N, 0,
                                                      0, 0, response);
  xSemaphoreGive(s_state.lock);

  if (err != ESP_OK) {
    return err == ESP_ERR_TIMEOUT ? SDF_FINGERPRINT_OP_TIMEOUT
                                  : SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  uint8_t q3 = response[4];
  if (q3 == SDF_FINGERPRINT_ACK_NOUSER) {
    return SDF_FINGERPRINT_OP_NO_MATCH;
  }
  if (q3 == SDF_FINGERPRINT_ACK_TIMEOUT) {
    return SDF_FINGERPRINT_OP_TIMEOUT;
  }

  if (q3 >= 1u && q3 <= 3u) {
    match->user_id = ((uint16_t)response[2] << 8) | response[3];
    match->permission = q3;
    return SDF_FINGERPRINT_OP_OK;
  }

  if (q3 == SDF_FINGERPRINT_ACK_SUCCESS) {
    match->user_id = ((uint16_t)response[2] << 8) | response[3];
    match->permission = 0;
    return SDF_FINGERPRINT_OP_OK;
  }

  return SDF_FINGERPRINT_OP_FAILED;
}

sdf_fingerprint_op_result_t
sdf_fingerprint_driver_enroll_step(sdf_fingerprint_enroll_step_t step,
                                   uint16_t user_id, uint8_t permission) {
  if (!sdf_fingerprint_user_id_valid(user_id) || permission < 1u ||
      permission > 3u || s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  uint8_t cmd = 0;
  switch (step) {
  case SDF_FINGERPRINT_ENROLL_STEP_1:
    cmd = SDF_FP_CMD_ENROLL_1;
    break;
  case SDF_FINGERPRINT_ENROLL_STEP_2:
    cmd = SDF_FP_CMD_ENROLL_2;
    break;
  case SDF_FINGERPRINT_ENROLL_STEP_3:
    cmd = SDF_FP_CMD_ENROLL_3;
    break;
  default:
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_FP_MUTEX_WAIT_MS)) !=
      pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  uint8_t response[SDF_FP_FRAME_LEN] = {0};
  esp_err_t err = sdf_fingerprint_send_command_locked(
      cmd, (uint8_t)((user_id >> 8) & 0xFF), (uint8_t)(user_id & 0xFF),
      permission, response);
  xSemaphoreGive(s_state.lock);

  if (err != ESP_OK) {
    return err == ESP_ERR_TIMEOUT ? SDF_FINGERPRINT_OP_TIMEOUT
                                  : SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  return sdf_fingerprint_map_ack_code(response[4]);
}

sdf_fingerprint_op_result_t
sdf_fingerprint_driver_delete_user(uint16_t user_id) {
  if (!sdf_fingerprint_user_id_valid(user_id) || s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_FP_MUTEX_WAIT_MS)) !=
      pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  uint8_t response[SDF_FP_FRAME_LEN] = {0};
  esp_err_t err = sdf_fingerprint_send_command_locked(
      SDF_FP_CMD_DELETE_USER, (uint8_t)((user_id >> 8) & 0xFF),
      (uint8_t)(user_id & 0xFF), 0x00, response);
  xSemaphoreGive(s_state.lock);

  if (err != ESP_OK) {
    return err == ESP_ERR_TIMEOUT ? SDF_FINGERPRINT_OP_TIMEOUT
                                  : SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  return sdf_fingerprint_map_ack_code(response[4]);
}

sdf_fingerprint_op_result_t sdf_fingerprint_driver_delete_all_users(void) {
  if (s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_FP_MUTEX_WAIT_MS)) !=
      pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  uint8_t response[SDF_FP_FRAME_LEN] = {0};
  esp_err_t err = sdf_fingerprint_send_command_locked(
      SDF_FP_CMD_DELETE_ALL_USERS, 0x00, 0x00, 0x00, response);
  xSemaphoreGive(s_state.lock);

  if (err != ESP_OK) {
    return err == ESP_ERR_TIMEOUT ? SDF_FINGERPRINT_OP_TIMEOUT
                                  : SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  return sdf_fingerprint_map_ack_code(response[4]);
}

sdf_fingerprint_op_result_t
sdf_fingerprint_driver_query_users(uint16_t *user_ids, uint8_t *permissions,
                                   size_t *count, size_t max_count) {
  if (user_ids == NULL || permissions == NULL || count == NULL ||
      max_count == 0 || s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_FP_MUTEX_WAIT_MS)) !=
      pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  uint8_t response[SDF_FP_FRAME_LEN] = {0};
  esp_err_t err =
      sdf_fingerprint_send_command_locked(0x2B, 0x00, 0x00, 0x00, response);

  if (err != ESP_OK) {
    xSemaphoreGive(s_state.lock);
    return err == ESP_ERR_TIMEOUT ? SDF_FINGERPRINT_OP_TIMEOUT
                                  : SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  sdf_fingerprint_op_result_t res = sdf_fingerprint_map_ack_code(response[4]);
  if (res != SDF_FINGERPRINT_OP_OK) {
    xSemaphoreGive(s_state.lock);
    return res;
  }

  /* The length of the upcoming data packet is in bytes 2 and 3 of the header */
  uint16_t data_len = ((uint16_t)response[2] << 8) | response[3];
  if (data_len < 2) {
    /* Empty database */
    *count = 0;
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_OK;
  }

  /* Allocate buffer for the data packet: Header(2) + Data(data_len) + CRC(1) +
   * Tail(1) */
  size_t packet_size = 2 + data_len + 2;
  uint8_t *packet = calloc(1, packet_size);
  if (packet == NULL) {
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  /* Read the remaining data packet */
  err = sdf_fingerprint_uart_read_exact(s_state.uart_port, packet, packet_size,
                                        s_state.response_timeout_ms);
  if (err != ESP_OK) {
    free(packet);
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_TIMEOUT;
  }

  if (packet[0] != SDF_FP_MARKER || packet[packet_size - 1] != SDF_FP_MARKER) {
    free(packet);
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  /* Verify packet checksum (XOR of bytes 1 to length inclusive, comparing to
   * length+1) */
  uint8_t checksum = 0;
  for (size_t i = 1; i <= data_len; i++) {
    checksum ^= packet[i];
  }
  if (checksum != packet[data_len + 1]) {
    free(packet);
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  /* Data format: [CMD:0xF5, 0x00, User1_Hi, User1_Lo, Perm1, User2_Hi... ] */
  /* The format is 3 bytes per user (2 for ID, 1 for Permission), starting at
   * offset 2 */
  size_t parsed_count = 0;
  size_t offset = 2; // Skip Marker and padding 0x00
  while (offset + 2 < data_len + 1 && parsed_count < max_count) {
    uint16_t uid = ((uint16_t)packet[offset] << 8) | packet[offset + 1];
    uint8_t perm = packet[offset + 2];

    user_ids[parsed_count] = uid;
    permissions[parsed_count] = perm;
    parsed_count++;
    offset += 3;
  }

  *count = parsed_count;
  free(packet);
  xSemaphoreGive(s_state.lock);
  return SDF_FINGERPRINT_OP_OK;
}

#include "fingerprint.h"

#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_task_wdt.h"
#else
#include "sdf_mock_linux_drivers.h"
#endif

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FP_FRAME_LEN 8u
#define FP_MARKER 0xF5u

#define FP_CMD_ENROLL_1 0x01u
#define FP_CMD_ENROLL_2 0x02u
#define FP_CMD_ENROLL_3 0x03u
#define FP_CMD_DELETE_USER 0x04u
#define FP_CMD_DELETE_ALL_USERS 0x05u
#define FP_CMD_QUERY_PERMISSION 0x0Au
#define FP_CMD_MATCH_1_N 0x0Cu
#define FP_CMD_UPLOAD_EIGENVALUES 0x31u
#define FP_CMD_SAVE_EIGENVALUES 0x41u
#define FP_CMD_QUERY_SN 0x2Au
#define FP_CMD_QUERY_USERS 0x2Bu

#define FP_MUTEX_WAIT_MS 250u
#define FP_POWER_SETTLE_MS 500u

static const char *TAG = "fp";

typedef struct {
  bool initialized;
  bool keep_power_on;
  bool power_is_on;
  int uart_port;
  int power_en_pin;
  uint32_t response_timeout_ms;
  SemaphoreHandle_t lock;
} fp_state_t;

static fp_state_t s_state = {
    .initialized = false,
    .keep_power_on = false,
    .power_is_on = false,
    .uart_port = -1,
    .power_en_pin = -1,
    .response_timeout_ms = 0,
    .lock = NULL,
};

#ifdef SDF_DRIVERS_TESTING
#define FP_STATIC
#else
#define FP_STATIC static
#endif

FP_STATIC uint8_t fp_checksum(const uint8_t frame[FP_FRAME_LEN]) {
  uint8_t checksum = 0;
  for (size_t i = 1; i <= 5; ++i) {
    checksum ^= frame[i];
  }
  return checksum;
}

FP_STATIC bool fp_user_id_valid(uint16_t user_id) {
  return user_id >= SDF_FINGERPRINT_USER_ID_MIN &&
         user_id <= SDF_FINGERPRINT_USER_ID_MAX;
}

static uint8_t fp_packet_checksum(const uint8_t *payload, size_t payload_len) {
  uint8_t checksum = 0;
  if (payload == NULL) {
    return checksum;
  }

  for (size_t i = 0; i < payload_len; ++i) {
    checksum ^= payload[i];
  }
  return checksum;
}

static const char *fp_command_name(uint8_t cmd) {
  switch (cmd) {
  case FP_CMD_ENROLL_1:
    return "ENROLL_1";
  case FP_CMD_ENROLL_2:
    return "ENROLL_2";
  case FP_CMD_ENROLL_3:
    return "ENROLL_3";
  case FP_CMD_DELETE_USER:
    return "DELETE_USER";
  case FP_CMD_DELETE_ALL_USERS:
    return "DELETE_ALL_USERS";
  case FP_CMD_QUERY_PERMISSION:
    return "QUERY_PERMISSION";
  case FP_CMD_MATCH_1_N:
    return "MATCH_1_N";
  case FP_CMD_UPLOAD_EIGENVALUES:
    return "UPLOAD_EIGENVALUES";
  case FP_CMD_SAVE_EIGENVALUES:
    return "SAVE_EIGENVALUES";
  case FP_CMD_QUERY_SN:
    return "QUERY_SN";
  case FP_CMD_QUERY_USERS:
    return "QUERY_USERS";
  default:
    return "UNKNOWN";
  }
}

static const char *fp_ack_name(uint8_t ack_code) {
  switch (ack_code) {
  case SDF_FINGERPRINT_ACK_SUCCESS:
    return "ACK_SUCCESS";
  case SDF_FINGERPRINT_ACK_FAIL:
    return "ACK_FAIL";
  case SDF_FINGERPRINT_ACK_FULL:
    return "ACK_FULL";
  case SDF_FINGERPRINT_ACK_NOUSER:
    return "ACK_NOUSER";
  case SDF_FINGERPRINT_ACK_USER_OCCUPIED:
    return "ACK_USER_OCCUPIED";
  case SDF_FINGERPRINT_ACK_FINGER_OCCUPIED:
    return "ACK_FINGER_OCCUPIED";
  case SDF_FINGERPRINT_ACK_TIMEOUT:
    return "ACK_TIMEOUT";
  default:
    return "ACK_UNKNOWN";
  }
}

static const char *fp_result_name(sdf_fingerprint_op_result_t result) {
  switch (result) {
  case SDF_FINGERPRINT_OP_OK:
    return "OK";
  case SDF_FINGERPRINT_OP_NO_MATCH:
    return "NO_MATCH";
  case SDF_FINGERPRINT_OP_TIMEOUT:
    return "TIMEOUT";
  case SDF_FINGERPRINT_OP_FULL:
    return "FULL";
  case SDF_FINGERPRINT_OP_USER_OCCUPIED:
    return "USER_OCCUPIED";
  case SDF_FINGERPRINT_OP_FINGER_OCCUPIED:
    return "FINGER_OCCUPIED";
  case SDF_FINGERPRINT_OP_FAILED:
    return "FAILED";
  case SDF_FINGERPRINT_OP_IO_ERROR:
    return "IO_ERROR";
  case SDF_FINGERPRINT_OP_PROTOCOL_ERROR:
    return "PROTOCOL_ERROR";
  case SDF_FINGERPRINT_OP_BAD_ARG:
    return "BAD_ARG";
  default:
    return "UNKNOWN";
  }
}

static bool fp_config_valid(const sdf_fingerprint_driver_config_t *config) {
  return config != NULL && config->uart_port >= 0 &&
         config->uart_port < UART_NUM_MAX && config->baud_rate != 0 &&
         config->tx_pin >= 0 && config->rx_pin >= 0 &&
         config->response_timeout_ms != 0 && config->rx_buffer_size >= 64 &&
         config->tx_buffer_size >= 64;
}

static esp_err_t fp_ensure_lock(void) {
  if (s_state.lock != NULL) {
    return ESP_OK;
  }

  s_state.lock = xSemaphoreCreateMutex();
  if (s_state.lock == NULL) {
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

static void fp_wait_for_power_ready(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
  if (s_state.power_en_pin >= 0) {
    vTaskDelay(pdMS_TO_TICKS(FP_POWER_SETTLE_MS));
  }
#endif
}

static esp_err_t fp_begin_uart_access_locked(void) {
  if (!s_state.initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_state.power_is_on) {
    fp_set_power(true);
    fp_wait_for_power_ready();
    s_state.power_is_on = true;
  }
  return ESP_OK;
}

static void fp_end_uart_access_locked(void) {
  if (!s_state.keep_power_on && s_state.power_is_on) {
    fp_set_power(false);
    s_state.power_is_on = false;
  }
}

/* Forward declaration – needed by fp_probe below. */
static esp_err_t fp_send_command_locked(uint8_t cmd, uint8_t p1, uint8_t p2,
                                        uint8_t p3,
                                        uint8_t response[FP_FRAME_LEN]);

esp_err_t fp_probe(void) {
  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (!s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return ESP_ERR_INVALID_STATE;
  }

  /* Try to reach the sensor with increasing power-settle delays.
   * Use the "Query SN" (0x2A) command – it is a simple 8-byte
   * request/response that always succeeds when the sensor is alive. */
  static const uint32_t settle_ms[] = {500, 1000, 1500};
  static const size_t max_attempts =
      sizeof(settle_ms) / sizeof(settle_ms[0]);
  uint32_t saved_timeout = s_state.response_timeout_ms;
  /* Use a short 1-second UART timeout for the probe. */
  s_state.response_timeout_ms = 1000;

  esp_err_t err = ESP_ERR_TIMEOUT;
  for (size_t attempt = 0; attempt < max_attempts; attempt++) {
    fp_set_power(true);
    s_state.power_is_on = true;
#ifndef CONFIG_IDF_TARGET_LINUX
    vTaskDelay(pdMS_TO_TICKS(settle_ms[attempt]));
#endif
    uart_flush_input((uart_port_t)s_state.uart_port);

    uint8_t response[FP_FRAME_LEN] = {0};
    err = fp_send_command_locked(FP_CMD_QUERY_SN, 0, 0, 0, response);

    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Sensor probe OK on attempt %u (settle %lu ms), "
                    "SN=0x%02X%02X%02X",
               (unsigned)(attempt + 1), (unsigned long)settle_ms[attempt],
               response[2], response[3], response[4]);
      fp_set_power(false);
      s_state.power_is_on = false;
      s_state.response_timeout_ms = saved_timeout;
      xSemaphoreGive(s_state.lock);
      return ESP_OK;
    }

    ESP_LOGW(TAG, "Sensor probe attempt %u/%u failed (settle %lu ms): %s",
             (unsigned)(attempt + 1), (unsigned)max_attempts,
             (unsigned long)settle_ms[attempt], esp_err_to_name(err));
    fp_set_power(false);
    s_state.power_is_on = false;
#ifndef CONFIG_IDF_TARGET_LINUX
    vTaskDelay(pdMS_TO_TICKS(100)); /* brief pause before next cycle */
#endif
  }

  ESP_LOGE(TAG, "Sensor probe FAILED after %u attempts – check wiring "
                "(TX=%d, RX=%d, power_en=%d)",
           (unsigned)max_attempts, -1, -1, s_state.power_en_pin);
  s_state.response_timeout_ms = saved_timeout;
  xSemaphoreGive(s_state.lock);
  return err;
}

static esp_err_t fp_uart_read_exact(int uart_port, uint8_t *buffer,
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
    
    // Process in chunks of max 1000ms to allow feeding the watchdog timer
    uint32_t wait_ms = remaining_ms > 1000 ? 1000 : remaining_ms;
    
    TickType_t read_timeout_ticks = pdMS_TO_TICKS(wait_ms);
    if (read_timeout_ticks == 0) {
      read_timeout_ticks = 1;
    }

    int read_now = uart_read_bytes((uart_port_t)uart_port, buffer + offset,
                                   expected_len - offset, read_timeout_ticks);

#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif

    if (read_now < 0) {
      return ESP_FAIL;
    }

    offset += (size_t)read_now;
  }

  return ESP_OK;
}

static esp_err_t fp_validate_response_locked(uint8_t cmd,
                                             const uint8_t response[FP_FRAME_LEN]) {
  if (response[0] != FP_MARKER || response[7] != FP_MARKER) {
    ESP_LOGE(TAG, "RX invalid marker: %02X ... %02X", response[0], response[7]);
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (response[1] != cmd) {
    ESP_LOGE(TAG, "RX cmd mismatch: exp %02X, got %02X", cmd, response[1]);
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint8_t expected_checksum = fp_checksum(response);
  if (response[6] != expected_checksum) {
    ESP_LOGE(TAG, "RX checksum error: exp %02X, got %02X", expected_checksum,
             response[6]);
    return ESP_ERR_INVALID_CRC;
  }

  return ESP_OK;
}

static esp_err_t fp_read_data_packet_locked(uint8_t *payload,
                                            uint16_t payload_len) {
  if (payload == NULL || payload_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  const size_t packet_size = (size_t)payload_len + 3u;
  uint8_t *packet = malloc(packet_size);
  if (packet == NULL) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = fp_uart_read_exact(s_state.uart_port, packet, packet_size,
                                     s_state.response_timeout_ms);
  if (err != ESP_OK) {
    free(packet);
    return err;
  }

  if (packet[0] != FP_MARKER || packet[packet_size - 1] != FP_MARKER) {
    free(packet);
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint8_t expected_checksum = fp_packet_checksum(packet + 1, payload_len);
  if (packet[payload_len + 1u] != expected_checksum) {
    free(packet);
    return ESP_ERR_INVALID_CRC;
  }

  memcpy(payload, packet + 1, payload_len);
  free(packet);
  return ESP_OK;
}

static esp_err_t fp_send_large_command_locked(uint8_t cmd,
                                              const uint8_t *payload,
                                              uint16_t payload_len,
                                              uint8_t response[FP_FRAME_LEN]) {
  if (!s_state.initialized || payload == NULL || payload_len == 0 ||
      response == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t head[FP_FRAME_LEN] = {
      FP_MARKER, cmd, (uint8_t)((payload_len >> 8) & 0xFFu),
      (uint8_t)(payload_len & 0xFFu), 0x00, 0x00, 0x00, FP_MARKER,
  };
  head[6] = fp_checksum(head);

  const size_t packet_size = (size_t)payload_len + 3u;
  uint8_t *packet = malloc(packet_size);
  if (packet == NULL) {
    return ESP_ERR_NO_MEM;
  }

  packet[0] = FP_MARKER;
  memcpy(packet + 1, payload, payload_len);
  packet[payload_len + 1u] = fp_packet_checksum(payload, payload_len);
  packet[payload_len + 2u] = FP_MARKER;

  uart_flush_input((uart_port_t)s_state.uart_port);

  int written = uart_write_bytes((uart_port_t)s_state.uart_port, head,
                                 sizeof(head));
  if (written != (int)sizeof(head)) {
    free(packet);
    return ESP_FAIL;
  }

  written = uart_write_bytes((uart_port_t)s_state.uart_port, packet,
                             packet_size);
  free(packet);
  if (written != (int)packet_size) {
    return ESP_FAIL;
  }

  esp_err_t err = fp_uart_read_exact(s_state.uart_port, response, FP_FRAME_LEN,
                                     s_state.response_timeout_ms);
  ESP_LOGI(TAG, "TX %s cmd=0x%02X payload_len=%u", fp_command_name(cmd), cmd,
           (unsigned)payload_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RX timeout/error on %s (cmd=0x%02X): %s",
             fp_command_name(cmd), cmd, esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG,
           "RX %s cmd=0x%02X frame=%02X %02X %02X %02X %02X %02X %02X %02X",
           fp_command_name(response[1]), response[1], response[0], response[1],
           response[2], response[3], response[4], response[5], response[6],
           response[7]);
  return fp_validate_response_locked(cmd, response);
}

static sdf_fingerprint_op_result_t fp_transport_err_to_result(esp_err_t err) {
  return err == ESP_ERR_TIMEOUT ? SDF_FINGERPRINT_OP_TIMEOUT
                                : SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
}

static esp_err_t fp_send_command_locked(uint8_t cmd, uint8_t p1, uint8_t p2,
                                        uint8_t p3,
                                        uint8_t response[FP_FRAME_LEN]) {
  if (!s_state.initialized || response == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t request[FP_FRAME_LEN] = {
      FP_MARKER, cmd, p1, p2, p3, 0x00, 0x00, FP_MARKER,
  };
  request[6] = fp_checksum(request);

  uart_flush_input((uart_port_t)s_state.uart_port);

  int written = uart_write_bytes((uart_port_t)s_state.uart_port, request,
                                 sizeof(request));
  if (written != (int)sizeof(request)) {
    return ESP_FAIL;
  }

  esp_err_t err = fp_uart_read_exact(s_state.uart_port, response, FP_FRAME_LEN,
                                     s_state.response_timeout_ms);

  ESP_LOGI(TAG,
           "TX %s cmd=0x%02X frame=%02X %02X %02X %02X %02X %02X %02X %02X",
           fp_command_name(cmd), cmd, request[0], request[1], request[2],
           request[3], request[4], request[5], request[6], request[7]);

  if (err != ESP_OK) {
    if (err == ESP_ERR_TIMEOUT && cmd == FP_CMD_MATCH_1_N) {
      /* Timeout is expected during normal polling when no finger is present */
      ESP_LOGD(TAG, "RX timeout on %s (cmd=0x%02X, expected during idle match)",
               fp_command_name(cmd), cmd);
    } else {
      ESP_LOGE(TAG, "RX timeout/error on %s (cmd=0x%02X): %s",
               fp_command_name(cmd), cmd, esp_err_to_name(err));
    }
    return err;
  }

  ESP_LOGI(TAG,
           "RX %s cmd=0x%02X frame=%02X %02X %02X %02X %02X %02X %02X %02X",
           fp_command_name(response[1]), response[1], response[0], response[1],
           response[2], response[3], response[4], response[5], response[6],
           response[7]);
  return fp_validate_response_locked(cmd, response);
}

FP_STATIC sdf_fingerprint_op_result_t fp_map_ack_code(uint8_t ack_code) {
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

static sdf_fingerprint_op_result_t
fp_query_user_permission_locked(uint16_t user_id, uint8_t *permission) {
  uint8_t response[FP_FRAME_LEN] = {0};
  esp_err_t err = fp_send_command_locked(FP_CMD_QUERY_PERMISSION,
                                         (uint8_t)((user_id >> 8) & 0xFFu),
                                         (uint8_t)(user_id & 0xFFu), 0x00,
                                         response);
  if (err != ESP_OK) {
    return fp_transport_err_to_result(err);
  }

  if (response[4] >= 1u && response[4] <= 3u) {
    if (permission != NULL) {
      *permission = response[4];
    }
    return SDF_FINGERPRINT_OP_OK;
  }

  if (response[4] == SDF_FINGERPRINT_ACK_SUCCESS) {
    if (permission != NULL) {
      *permission = 0;
    }
    return SDF_FINGERPRINT_OP_OK;
  }

  return fp_map_ack_code(response[4]);
}

static sdf_fingerprint_op_result_t fp_delete_user_locked(uint16_t user_id) {
  uint8_t response[FP_FRAME_LEN] = {0};
  esp_err_t err = fp_send_command_locked(FP_CMD_DELETE_USER,
                                         (uint8_t)((user_id >> 8) & 0xFFu),
                                         (uint8_t)(user_id & 0xFFu), 0x00,
                                         response);
  if (err != ESP_OK) {
    return fp_transport_err_to_result(err);
  }

  return fp_map_ack_code(response[4]);
}

static sdf_fingerprint_op_result_t
fp_upload_eigenvalues_locked(uint16_t user_id, uint8_t *permission,
                             uint8_t eigenvalues[SDF_FINGERPRINT_EIGENVALUE_SIZE]) {
  uint8_t response[FP_FRAME_LEN] = {0};
  esp_err_t err = fp_send_command_locked(FP_CMD_UPLOAD_EIGENVALUES,
                                         (uint8_t)((user_id >> 8) & 0xFFu),
                                         (uint8_t)(user_id & 0xFFu), 0x00,
                                         response);
  if (err != ESP_OK) {
    return fp_transport_err_to_result(err);
  }

  sdf_fingerprint_op_result_t result = fp_map_ack_code(response[4]);
  if (result != SDF_FINGERPRINT_OP_OK) {
    return result;
  }

  const uint16_t data_len = ((uint16_t)response[2] << 8) | response[3];
  if (data_len != (uint16_t)(3u + SDF_FINGERPRINT_EIGENVALUE_SIZE)) {
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  uint8_t payload[3u + SDF_FINGERPRINT_EIGENVALUE_SIZE] = {0};
  err = fp_read_data_packet_locked(payload, data_len);
  if (err != ESP_OK) {
    return fp_transport_err_to_result(err);
  }

  uint16_t payload_user_id = ((uint16_t)payload[0] << 8) | payload[1];
  if (payload_user_id != user_id) {
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  if (permission != NULL) {
    *permission = payload[2];
  }
  memcpy(eigenvalues, &payload[3], SDF_FINGERPRINT_EIGENVALUE_SIZE);
  return SDF_FINGERPRINT_OP_OK;
}

static sdf_fingerprint_op_result_t
fp_save_eigenvalues_locked(uint16_t user_id, uint8_t permission,
                           const uint8_t eigenvalues[SDF_FINGERPRINT_EIGENVALUE_SIZE]) {
  uint8_t payload[3u + SDF_FINGERPRINT_EIGENVALUE_SIZE] = {0};
  payload[0] = (uint8_t)((user_id >> 8) & 0xFFu);
  payload[1] = (uint8_t)(user_id & 0xFFu);
  payload[2] = permission;
  memcpy(&payload[3], eigenvalues, SDF_FINGERPRINT_EIGENVALUE_SIZE);

  uint8_t response[FP_FRAME_LEN] = {0};
  esp_err_t err =
      fp_send_large_command_locked(FP_CMD_SAVE_EIGENVALUES, payload,
                                   sizeof(payload), response);
  if (err != ESP_OK) {
    return fp_transport_err_to_result(err);
  }

  return fp_map_ack_code(response[4]);
}

void fp_set_power(bool enabled) {
  if (s_state.power_en_pin < 0) {
    return;
  }

  gpio_set_level((gpio_num_t)s_state.power_en_pin, enabled ? 1 : 0);
}

esp_err_t fp_set_keep_power_on(bool keep_power_on) {
  if (s_state.lock == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (!s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return ESP_ERR_INVALID_STATE;
  }

  s_state.keep_power_on = keep_power_on;
  if (keep_power_on && !s_state.power_is_on) {
    fp_set_power(true);
    fp_wait_for_power_ready();
    s_state.power_is_on = true;
  } else if (!keep_power_on && s_state.power_is_on) {
    fp_set_power(false);
    s_state.power_is_on = false;
  }

  xSemaphoreGive(s_state.lock);
  ESP_LOGI(TAG, "Fingerprint power hold %s",
           keep_power_on ? "enabled" : "disabled");
  return ESP_OK;
}

esp_err_t fp_init(const sdf_fingerprint_driver_config_t *config) {
  if (!fp_config_valid(config)) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = fp_ensure_lock();
  if (err != ESP_OK) {
    return err;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (s_state.initialized) {
    xSemaphoreGive(s_state.lock);
    return ESP_OK;
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
  }

  s_state.power_en_pin = config->power_en_pin;
  fp_set_power(true);
  s_state.power_is_on = true;
  fp_wait_for_power_ready();

  uart_config_t uart_config = {
      .baud_rate = (int)config->baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  err = uart_driver_install((uart_port_t)config->uart_port,
                            config->rx_buffer_size, config->tx_buffer_size, 0,
                            NULL, 0);
  if (err != ESP_OK) {
    fp_set_power(false);
    s_state.power_is_on = false;
    s_state.power_en_pin = -1;
    xSemaphoreGive(s_state.lock);
    return err;
  }

  err = uart_param_config((uart_port_t)config->uart_port, &uart_config);
  if (err != ESP_OK) {
    uart_driver_delete((uart_port_t)config->uart_port);
    fp_set_power(false);
    s_state.power_is_on = false;
    s_state.power_en_pin = -1;
    xSemaphoreGive(s_state.lock);
    return err;
  }

  err = uart_set_pin((uart_port_t)config->uart_port, config->tx_pin,
                     config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    uart_driver_delete((uart_port_t)config->uart_port);
    fp_set_power(false);
    s_state.power_is_on = false;
    s_state.power_en_pin = -1;
    xSemaphoreGive(s_state.lock);
    return err;
  }

  s_state.initialized = true;
  s_state.keep_power_on = false;
  s_state.uart_port = config->uart_port;
  s_state.response_timeout_ms = config->response_timeout_ms;
  fp_set_power(false);
  s_state.power_is_on = false;
  xSemaphoreGive(s_state.lock);

  ESP_LOGI(TAG, "Fingerprint initialized (port=%d, baud=%u, tx=%d, rx=%d)",
           config->uart_port, (unsigned)config->baud_rate, config->tx_pin,
           config->rx_pin);

  return ESP_OK;
}

void fp_deinit(void) {
  if (s_state.lock == NULL) {
    return;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return;
  }

  if (s_state.initialized) {
    if (!s_state.power_is_on) {
      fp_set_power(true);
      fp_wait_for_power_ready();
      s_state.power_is_on = true;
    }
    uart_driver_delete((uart_port_t)s_state.uart_port);
    fp_set_power(false);
    s_state.power_is_on = false;
  } else {
    fp_set_power(false);
    s_state.power_is_on = false;
  }

  s_state.initialized = false;
  s_state.keep_power_on = false;
  s_state.uart_port = -1;
  s_state.power_en_pin = -1;
  s_state.response_timeout_ms = 0;

  xSemaphoreGive(s_state.lock);
}

bool fp_is_ready(void) {
  if (s_state.lock == NULL) {
    return false;
  }

  bool ready = false;
  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) == pdTRUE) {
    ready = s_state.initialized;
    xSemaphoreGive(s_state.lock);
  }

  return ready;
}

sdf_fingerprint_op_result_t fp_match_1n(sdf_fingerprint_match_t *match) {
  if (match == NULL || s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  esp_err_t err = fp_begin_uart_access_locked();
  if (err != ESP_OK) {
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  uint8_t response[FP_FRAME_LEN] = {0};
  err = fp_send_command_locked(FP_CMD_MATCH_1_N, 0, 0, 0, response);
  fp_end_uart_access_locked();
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
fp_enroll_step(sdf_fingerprint_enroll_step_t step, uint16_t user_id,
               uint8_t permission) {
  if (!fp_user_id_valid(user_id) || permission < 1u || permission > 3u ||
      s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  uint8_t cmd = 0;
  switch (step) {
  case SDF_FINGERPRINT_ENROLL_STEP_1:
    cmd = FP_CMD_ENROLL_1;
    break;
  case SDF_FINGERPRINT_ENROLL_STEP_2:
    cmd = FP_CMD_ENROLL_2;
    break;
  case SDF_FINGERPRINT_ENROLL_STEP_3:
    cmd = FP_CMD_ENROLL_3;
    break;
  default:
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  esp_err_t err = fp_begin_uart_access_locked();
  if (err != ESP_OK) {
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  uint8_t response[FP_FRAME_LEN] = {0};
  ESP_LOGI(TAG, "Enrollment step %u (%s, cmd=0x%02X) user_id=%u permission=%u",
           (unsigned)step, fp_command_name(cmd), cmd, (unsigned)user_id,
           (unsigned)permission);
  err = fp_send_command_locked(cmd, (uint8_t)((user_id >> 8) & 0xFF),
                               (uint8_t)(user_id & 0xFF), permission, response);
  fp_end_uart_access_locked();
  xSemaphoreGive(s_state.lock);

  if (err != ESP_OK) {
    sdf_fingerprint_op_result_t result =
        err == ESP_ERR_TIMEOUT ? SDF_FINGERPRINT_OP_TIMEOUT
                               : SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
    ESP_LOGW(TAG,
             "Enrollment step %u (%s, cmd=0x%02X) transport error=%s -> %s",
             (unsigned)step, fp_command_name(cmd), cmd, esp_err_to_name(err),
             fp_result_name(result));
    return result;
  }

  sdf_fingerprint_op_result_t result = fp_map_ack_code(response[4]);
  ESP_LOGI(TAG,
           "Enrollment step %u (%s, cmd=0x%02X) ack=%s (0x%02X) -> %s",
           (unsigned)step, fp_command_name(cmd), cmd, fp_ack_name(response[4]),
           response[4], fp_result_name(result));
  return result;
}

sdf_fingerprint_op_result_t fp_delete_user(uint16_t user_id) {
  if (!fp_user_id_valid(user_id) || s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  esp_err_t err = fp_begin_uart_access_locked();
  if (err != ESP_OK) {
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  sdf_fingerprint_op_result_t result = fp_delete_user_locked(user_id);
  fp_end_uart_access_locked();
  xSemaphoreGive(s_state.lock);
  return result;
}

sdf_fingerprint_op_result_t fp_query_user_permission(uint16_t user_id,
                                                     uint8_t *permission) {
  if (!fp_user_id_valid(user_id) || permission == NULL || s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  esp_err_t err = fp_begin_uart_access_locked();
  if (err != ESP_OK) {
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  sdf_fingerprint_op_result_t result =
      fp_query_user_permission_locked(user_id, permission);
  fp_end_uart_access_locked();
  xSemaphoreGive(s_state.lock);
  return result;
}

sdf_fingerprint_op_result_t fp_change_user_permission(uint16_t user_id,
                                                      uint8_t permission) {
  if (!fp_user_id_valid(user_id) || permission < 1u || permission > 3u ||
      s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  esp_err_t err = fp_begin_uart_access_locked();
  if (err != ESP_OK) {
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  uint8_t current_permission = 0;
  uint8_t eigenvalues[SDF_FINGERPRINT_EIGENVALUE_SIZE] = {0};
  sdf_fingerprint_op_result_t result =
      fp_upload_eigenvalues_locked(user_id, &current_permission, eigenvalues);
  if (result == SDF_FINGERPRINT_OP_OK && current_permission != permission) {
    result = fp_delete_user_locked(user_id);
    if (result == SDF_FINGERPRINT_OP_OK) {
      result = fp_save_eigenvalues_locked(user_id, permission, eigenvalues);
      if (result != SDF_FINGERPRINT_OP_OK) {
        sdf_fingerprint_op_result_t rollback_result =
            fp_save_eigenvalues_locked(user_id, current_permission,
                                       eigenvalues);
        if (rollback_result == SDF_FINGERPRINT_OP_OK) {
          ESP_LOGW(TAG,
                   "Permission change rollback restored user_id=%u "
                   "permission=%u after failure",
                   (unsigned)user_id, (unsigned)current_permission);
        } else {
          ESP_LOGE(TAG,
                   "Permission change rollback failed for user_id=%u: %s",
                   (unsigned)user_id, fp_result_name(rollback_result));
        }
      }
    }
  }

  fp_end_uart_access_locked();
  xSemaphoreGive(s_state.lock);
  return result;
}

sdf_fingerprint_op_result_t fp_delete_all_users(void) {
  if (s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  esp_err_t err = fp_begin_uart_access_locked();
  if (err != ESP_OK) {
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  uint8_t response[FP_FRAME_LEN] = {0};
  err = fp_send_command_locked(FP_CMD_DELETE_ALL_USERS, 0x00, 0x00, 0x00,
                               response);
  fp_end_uart_access_locked();
  xSemaphoreGive(s_state.lock);

  if (err != ESP_OK) {
    return err == ESP_ERR_TIMEOUT ? SDF_FINGERPRINT_OP_TIMEOUT
                                  : SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  return fp_map_ack_code(response[4]);
}

sdf_fingerprint_op_result_t fp_query_users(uint16_t *user_ids,
                                           uint8_t *permissions,
                                           size_t *count, size_t max_count) {
  if (user_ids == NULL || permissions == NULL || count == NULL ||
      max_count == 0 || s_state.lock == NULL) {
    return SDF_FINGERPRINT_OP_BAD_ARG;
  }

  if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(FP_MUTEX_WAIT_MS)) != pdTRUE) {
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  esp_err_t err = fp_begin_uart_access_locked();
  if (err != ESP_OK) {
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  uint8_t response[FP_FRAME_LEN] = {0};
  err = fp_send_command_locked(FP_CMD_QUERY_USERS, 0x00, 0x00, 0x00, response);
  if (err != ESP_OK) {
    fp_end_uart_access_locked();
    xSemaphoreGive(s_state.lock);
    return err == ESP_ERR_TIMEOUT ? SDF_FINGERPRINT_OP_TIMEOUT
                                  : SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  sdf_fingerprint_op_result_t res = fp_map_ack_code(response[4]);
  if (res != SDF_FINGERPRINT_OP_OK) {
    /* The sensor returns ACK_FAIL when no users are enrolled.
     * Treat this as an empty result rather than an error. */
    if (response[4] == SDF_FINGERPRINT_ACK_FAIL) {
      *count = 0;
      fp_end_uart_access_locked();
      xSemaphoreGive(s_state.lock);
      return SDF_FINGERPRINT_OP_OK;
    }
    fp_end_uart_access_locked();
    xSemaphoreGive(s_state.lock);
    return res;
  }

  uint16_t data_len = ((uint16_t)response[2] << 8) | response[3];
  if (data_len < 2) {
    *count = 0;
    fp_end_uart_access_locked();
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_OK;
  }

  // Format: F5 + payload (data_len) + CHK + F5
  // Total size = 1 + data_len + 1 + 1 = data_len + 3
  size_t packet_size = data_len + 3;
  uint8_t *packet = calloc(1, packet_size);
  if (packet == NULL) {
    fp_end_uart_access_locked();
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_IO_ERROR;
  }

  err = fp_uart_read_exact(s_state.uart_port, packet, packet_size,
                           s_state.response_timeout_ms);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Query users: Data packet read timeout/error: %s", esp_err_to_name(err));
    free(packet);
    fp_end_uart_access_locked();
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_TIMEOUT;
  }

  ESP_LOGI(TAG, "Query users: Data packet received (len=%u)", (unsigned)packet_size);

  if (packet[0] != FP_MARKER || packet[packet_size - 1] != FP_MARKER) {
    free(packet);
    fp_end_uart_access_locked();
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  uint8_t checksum = 0;
  for (size_t i = 1; i <= data_len; ++i) {
    checksum ^= packet[i];
  }
  if (checksum != packet[data_len + 1]) {
    free(packet);
    fp_end_uart_access_locked();
    xSemaphoreGive(s_state.lock);
    return SDF_FINGERPRINT_OP_PROTOCOL_ERROR;
  }

  // The payload (starting at packet[1]) has:
  // [1..2] : Total user count
  // [3..5] : User 1 (ID High, ID Low, Permission)
  // [6..8] : User 2 ...
  uint16_t total_users = ((uint16_t)packet[1] << 8) | packet[2];
  
  size_t parsed_count = 0;
  size_t offset = 3; // First user starts here
  while (offset + 2 <= data_len && parsed_count < max_count && parsed_count < total_users) {
    user_ids[parsed_count] = ((uint16_t)packet[offset] << 8) | packet[offset + 1];
    permissions[parsed_count] = packet[offset + 2];
    ++parsed_count;
    offset += 3;
  }

  *count = parsed_count;
  free(packet);
  fp_end_uart_access_locked();
  xSemaphoreGive(s_state.lock);
  return SDF_FINGERPRINT_OP_OK;
}

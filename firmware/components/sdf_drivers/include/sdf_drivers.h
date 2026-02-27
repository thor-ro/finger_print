#ifndef SDF_DRIVERS_H
#define SDF_DRIVERS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SDF_FINGERPRINT_USER_ID_MIN 1u
#define SDF_FINGERPRINT_USER_ID_MAX 0x0FFFu

typedef struct {
  int uart_port;
  int tx_pin;
  int rx_pin;
  int power_en_pin;
  uint32_t baud_rate;
  uint32_t response_timeout_ms;
  uint16_t rx_buffer_size;
  uint16_t tx_buffer_size;
} sdf_fingerprint_driver_config_t;

typedef enum {
  SDF_FINGERPRINT_ACK_SUCCESS = 0x00,
  SDF_FINGERPRINT_ACK_FAIL = 0x01,
  SDF_FINGERPRINT_ACK_FULL = 0x04,
  SDF_FINGERPRINT_ACK_NOUSER = 0x05,
  SDF_FINGERPRINT_ACK_USER_OCCUPIED = 0x06,
  SDF_FINGERPRINT_ACK_FINGER_OCCUPIED = 0x07,
  SDF_FINGERPRINT_ACK_TIMEOUT = 0x08,
} sdf_fingerprint_ack_code_t;

typedef enum {
  SDF_FINGERPRINT_OP_OK = 0,
  SDF_FINGERPRINT_OP_NO_MATCH,
  SDF_FINGERPRINT_OP_TIMEOUT,
  SDF_FINGERPRINT_OP_FULL,
  SDF_FINGERPRINT_OP_USER_OCCUPIED,
  SDF_FINGERPRINT_OP_FINGER_OCCUPIED,
  SDF_FINGERPRINT_OP_FAILED,
  SDF_FINGERPRINT_OP_IO_ERROR,
  SDF_FINGERPRINT_OP_PROTOCOL_ERROR,
  SDF_FINGERPRINT_OP_BAD_ARG,
} sdf_fingerprint_op_result_t;

typedef struct {
  uint16_t user_id;
  uint8_t permission;
} sdf_fingerprint_match_t;

typedef enum {
  SDF_FINGERPRINT_ENROLL_STEP_1 = 1,
  SDF_FINGERPRINT_ENROLL_STEP_2 = 2,
  SDF_FINGERPRINT_ENROLL_STEP_3 = 3,
} sdf_fingerprint_enroll_step_t;

void sdf_fingerprint_driver_set_power(bool enabled);

void sdf_drivers_init(void);

esp_err_t sdf_drivers_battery_adc_init(int adc_pin);
int sdf_drivers_battery_get_percent(void);

esp_err_t
sdf_fingerprint_driver_init(const sdf_fingerprint_driver_config_t *config);
void sdf_fingerprint_driver_deinit(void);
bool sdf_fingerprint_driver_is_ready(void);

sdf_fingerprint_op_result_t
sdf_fingerprint_driver_match_1n(sdf_fingerprint_match_t *match);

sdf_fingerprint_op_result_t
sdf_fingerprint_driver_enroll_step(sdf_fingerprint_enroll_step_t step,
                                   uint16_t user_id, uint8_t permission);

sdf_fingerprint_op_result_t
sdf_fingerprint_driver_delete_user(uint16_t user_id);
sdf_fingerprint_op_result_t sdf_fingerprint_driver_delete_all_users(void);

sdf_fingerprint_op_result_t
sdf_fingerprint_driver_query_users(uint16_t *user_ids, uint8_t *permissions,
                                   size_t *count, size_t max_count);

#endif /* SDF_DRIVERS_H */

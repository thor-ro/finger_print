#ifndef SDF_PROTOCOL_ZIGBEE_H
#define SDF_PROTOCOL_ZIGBEE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    SDF_PROTOCOL_ZIGBEE_COMMAND_LOCK = 0,
    SDF_PROTOCOL_ZIGBEE_COMMAND_UNLOCK = 1,
    SDF_PROTOCOL_ZIGBEE_COMMAND_LATCH = 2,
    SDF_PROTOCOL_ZIGBEE_COMMAND_PROGRAMMING_EVENT = 3
} sdf_protocol_zigbee_command_t;

typedef enum {
    SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED = 0x00,
    SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED = 0x01,
    SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED = 0x02,
    SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED = 0xFF
} sdf_protocol_zigbee_lock_state_t;

typedef esp_err_t (*sdf_protocol_zigbee_command_cb)(
    void *ctx,
    sdf_protocol_zigbee_command_t command);

esp_err_t sdf_protocol_zigbee_init(void);

esp_err_t sdf_protocol_zigbee_set_command_handler(
    sdf_protocol_zigbee_command_cb cb,
    void *ctx);

esp_err_t sdf_protocol_zigbee_update_lock_state(sdf_protocol_zigbee_lock_state_t lock_state);

esp_err_t sdf_protocol_zigbee_update_battery_percent(uint8_t battery_percent);

esp_err_t sdf_protocol_zigbee_update_alarm_mask(uint16_t alarm_mask);

bool sdf_protocol_zigbee_is_ready(void);

#endif /* SDF_PROTOCOL_ZIGBEE_H */

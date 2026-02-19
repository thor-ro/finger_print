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

typedef struct {
    uint8_t zcl_command_id;
    uint16_t src_short_addr;
    uint8_t src_endpoint;
    bool has_user_id;
    uint16_t user_id;
    bool has_user_status;
    uint8_t user_status;
    bool has_user_type;
    uint8_t user_type;
    bool has_credential;
    uint8_t credential_len;
    uint8_t credential[32];
} sdf_protocol_zigbee_programming_event_t;

typedef struct {
    sdf_protocol_zigbee_command_t command;
    sdf_protocol_zigbee_programming_event_t programming_event;
} sdf_protocol_zigbee_command_event_t;

typedef enum {
    SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED = 0x00,
    SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED = 0x01,
    SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED = 0x02,
    SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED = 0xFF
} sdf_protocol_zigbee_lock_state_t;

typedef esp_err_t (*sdf_protocol_zigbee_command_cb)(
    void *ctx,
    const sdf_protocol_zigbee_command_event_t *event);

esp_err_t sdf_protocol_zigbee_init(void);

esp_err_t sdf_protocol_zigbee_set_command_handler(
    sdf_protocol_zigbee_command_cb cb,
    void *ctx);

esp_err_t sdf_protocol_zigbee_update_lock_state(sdf_protocol_zigbee_lock_state_t lock_state);

esp_err_t sdf_protocol_zigbee_update_battery_percent(uint8_t battery_percent);

esp_err_t sdf_protocol_zigbee_update_alarm_mask(uint16_t alarm_mask);

bool sdf_protocol_zigbee_is_ready(void);

#endif /* SDF_PROTOCOL_ZIGBEE_H */

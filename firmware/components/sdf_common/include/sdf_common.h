#ifndef SDF_COMMON_H
#define SDF_COMMON_H

#include <stdbool.h>
#include <stdint.h>

typedef uint16_t sdf_user_id_t;

typedef enum {
  SDF_LOCK_ACTION_UNLOCK = 0x01,
  SDF_LOCK_ACTION_LOCK = 0x02,
  SDF_LOCK_ACTION_UNLATCH = 0x03,
  SDF_LOCK_ACTION_LOCK_N_GO = 0x04,
  SDF_LOCK_ACTION_LOCK_N_GO_UNLATCH = 0x05,
  SDF_LOCK_ACTION_FULL_LOCK = 0x06
} sdf_lock_action_t;

typedef enum {
  SDF_STATUS_COMPLETE = 0x00,
  SDF_STATUS_ACCEPTED = 0x01
} sdf_status_t;

typedef struct {
  uint8_t nuki_state;
  uint8_t lock_state;
  uint8_t trigger;
  uint16_t current_time_year;
  uint8_t current_time_month;
  uint8_t current_time_day;
  uint8_t current_time_hour;
  uint8_t current_time_minute;
  uint8_t current_time_second;
  int16_t timezone_offset_minutes;
  uint8_t critical_battery_state;
  uint8_t lock_n_go_timer;
  uint8_t last_lock_action;
  bool has_last_lock_action_trigger;
  uint8_t last_lock_action_trigger;
  bool has_last_lock_action_completion_status;
  uint8_t last_lock_action_completion_status;
  bool has_door_sensor_state;
  uint8_t door_sensor_state;
  bool has_nightmode_active;
  uint8_t nightmode_active;
} sdf_keyturner_state_t;

typedef struct {
  int8_t error_code;
  uint16_t command_identifier;
} sdf_error_report_t;

typedef enum {
  SDF_AUDIT_STORAGE_POLICY_OK = 0,
  SDF_AUDIT_STORAGE_POLICY_FAILED = 1,
  SDF_AUDIT_BIOMETRIC_FAILED = 2,
  SDF_AUDIT_BIOMETRIC_LOCKOUT = 3,
  SDF_AUDIT_BIOMETRIC_LOCKOUT_CLEARED = 4,
  SDF_AUDIT_BIOMETRIC_MATCH_SUCCESS = 5,
  SDF_AUDIT_NONCE_REPLAY_BLOCKED = 6,
  SDF_AUDIT_PROTOCOL_ERROR = 7,
  SDF_AUDIT_PAIRING_COMPLETE = 8,
  SDF_AUDIT_PAIRING_FAILED = 9,
  SDF_AUDIT_OTA_TRIGGERED = 10,
  SDF_AUDIT_OTA_STARTED = 11,
  SDF_AUDIT_OTA_VERIFYING = 12,
  SDF_AUDIT_OTA_COMMITTED = 13,
  SDF_AUDIT_OTA_ROLLED_BACK = 14,
  SDF_AUDIT_OTA_FAILED = 15,
  SDF_AUDIT_OTA_SIGNATURE_INVALID = 16,
  SDF_AUDIT_OTA_SIGNATURE_MISSING = 17,
  SDF_AUDIT_OTA_VERSION_DOWNGRADE = 18,
  SDF_AUDIT_OTA_VERSION_UPGRADE = 19
} sdf_audit_event_type_t;

void sdf_common_init(void);

#endif /* SDF_COMMON_H */

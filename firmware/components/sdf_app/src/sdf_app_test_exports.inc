const char *test_sdf_app_status_name(uint8_t status) {
  return sdf_app_status_name(status);
}

const char *test_sdf_app_zb_programming_cmd_name(uint8_t cmd_id) {
  return sdf_app_zb_programming_cmd_name(cmd_id);
}

const char *test_sdf_app_enrollment_state_name(sdf_enrollment_state_t state) {
  return sdf_app_enrollment_state_name(state);
}

const char *
test_sdf_app_enrollment_result_name(sdf_enrollment_result_t result) {
  return sdf_app_enrollment_result_name(result);
}

const char *
test_sdf_app_power_wake_reason_name(sdf_power_wake_reason_t reason) {
  return sdf_app_power_wake_reason_name(reason);
}

const char *test_sdf_app_error_name(int8_t code) {
  return sdf_app_error_name(code);
}

const char *test_sdf_app_audit_event_name(sdf_audit_event_type_t type) {
  return sdf_app_audit_event_name(type);
}

bool test_sdf_app_valid_lock_action(uint8_t lock_action) {
  return sdf_app_valid_lock_action(lock_action);
}

sdf_protocol_zigbee_lock_state_t
test_sdf_app_map_lock_state_to_zigbee(uint8_t nuki_lock_state) {
  return sdf_app_map_lock_state_to_zigbee(nuki_lock_state);
}

uint8_t test_sdf_app_choose_fingerprint_permission(
    const sdf_protocol_zigbee_programming_event_t *pe) {
  return sdf_app_choose_fingerprint_permission(pe);
}

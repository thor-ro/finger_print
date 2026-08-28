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
  return sdf_app_map_device_lock_state_to_zigbee(
      sdf_app_map_nuki_state_to_device(nuki_lock_state));
}

uint8_t test_sdf_app_choose_fingerprint_permission(
    const sdf_protocol_zigbee_programming_event_t *pe) {
  return sdf_app_choose_fingerprint_permission(pe);
}

void test_sdf_app_on_admin_action(void *ctx, sdf_services_admin_action_t action) {
  sdf_app_on_admin_action(ctx, action);
}

/* --- Task/queue seams (give-sdf-app-a-task) ------------------------------ */

esp_err_t test_sdf_app_start_task(void) {
  return sdf_app_start_task();
}

TaskHandle_t test_sdf_app_task_handle(void) {
  return s_app_task;
}

void test_sdf_app_subscribe_trampoline(sdf_event_router_type_t type) {
  ESP_ERROR_CHECK(sdf_event_router_subscribe(
      type, SDF_EVENT_ROUTER_PRIO_LOW, sdf_app_event_trampoline, NULL));
}

/* Calls the subscriber callback exactly as the router's dispatch task would. */
void test_sdf_app_event_trampoline(const sdf_event_router_event_t *event) {
  sdf_app_event_trampoline(NULL, event);
}

/* Builds a probe event: recorded and dropped by the app task instead of being
 * handled, so no production handler runs. `tag` identifies it in the log. */
sdf_event_router_event_t
test_sdf_app_make_probe(sdf_event_router_type_t type,
                        sdf_event_router_priority_t priority, uint16_t tag) {
  sdf_event_router_event_t event = {
      .type = type,
      .priority = priority,
      .timestamp_ms = SDF_APP_TEST_PROBE_MAGIC | (uint32_t)tag,
  };
  return event;
}

void test_sdf_app_probe_log_reset(void) {
  s_test_handled_count = 0;
  s_test_trampoline_task = NULL;
  memset(s_test_handled_task, 0, sizeof(s_test_handled_task));
  memset(s_test_handled_tag, 0, sizeof(s_test_handled_tag));
}

uint32_t test_sdf_app_probe_handled_count(void) {
  return s_test_handled_count;
}

TaskHandle_t test_sdf_app_probe_handled_task(uint32_t idx) {
  return (idx < SDF_APP_TEST_PROBE_MAX) ? s_test_handled_task[idx] : NULL;
}

uint16_t test_sdf_app_probe_handled_tag(uint32_t idx) {
  return (idx < SDF_APP_TEST_PROBE_MAX) ? s_test_handled_tag[idx] : 0;
}

TaskHandle_t test_sdf_app_probe_trampoline_task(void) {
  return s_test_trampoline_task;
}

uint32_t test_sdf_app_evt_dropped(void) {
  return s_app_evt_dropped;
}

uint32_t test_sdf_app_event_queue_depth(void) {
  return SDF_APP_EVENT_QUEUE_DEPTH;
}

/* --- Zigbee alarm mask seams (design.md - D7) ---------------------------- */

void test_sdf_app_set_alarm_mask_bits(uint16_t set_bits, uint16_t clear_bits) {
  sdf_app_set_alarm_mask_bits(set_bits, clear_bits);
}

uint16_t test_sdf_app_get_alarm_mask(void) {
  return atomic_load(&s_zigbee_alarm_mask);
}

void test_sdf_app_store_alarm_mask(uint16_t mask) {
  atomic_store(&s_zigbee_alarm_mask, mask);
}

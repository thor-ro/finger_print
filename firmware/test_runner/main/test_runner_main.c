#include "unity.h"
#include <stdio.h>
#include <stdlib.h>

#include "sdf_config.h"

/* Enrollment SM tests */
extern void test_enrollment_sm_initialization(void);
extern void test_enrollment_sm_start_valid(void);
extern void test_enrollment_sm_start_invalid(void);
extern void test_enrollment_sm_success_sequence(void);
extern void test_enrollment_sm_failure_handling(void);
extern void test_enrollment_sm_user_occupied(void);
extern void test_enrollment_sm_is_active_idle(void);
extern void test_enrollment_sm_is_active_steps(void);
extern void test_enrollment_sm_is_active_error(void);

extern void test_enrollment_sm_current_step(void);
extern void test_enrollment_sm_current_command(void);
extern void test_enrollment_sm_reset(void);
extern void test_enrollment_sm_null_safety(void);
extern void test_enrollment_sm_start_while_active(void);
extern void test_enrollment_sm_failure_at_step2(void);
extern void test_enrollment_sm_failure_at_step3(void);
extern void test_enrollment_sm_apply_on_idle(void);
extern void test_enrollment_sm_apply_after_success(void);
extern void test_enrollment_sm_start_permission_boundaries(void);
extern void test_enrollment_sm_finger_occupied(void);

/* Driver utils tests */
extern void test_driver_user_id_validation(void);

/* Driver protocol tests */
extern void test_checksum_all_zeros(void);
extern void test_checksum_known_command(void);
extern void test_checksum_with_parameters(void);
extern void test_checksum_all_ff(void);
extern void test_checksum_cancellation(void);
extern void test_user_id_valid_min(void);
extern void test_user_id_valid_max(void);
extern void test_user_id_valid_mid(void);
extern void test_user_id_invalid_zero(void);
extern void test_user_id_invalid_above_max(void);
extern void test_user_id_invalid_uint16_max(void);
extern void test_map_ack_success(void);
extern void test_map_ack_timeout(void);
extern void test_map_ack_full(void);
extern void test_map_ack_user_occupied(void);
extern void test_map_ack_finger_occupied(void);
extern void test_map_ack_fail(void);
extern void test_map_ack_nouser(void);
extern void test_map_ack_unknown(void);

/* Storage tests */
extern void test_sdf_storage_nuki_save_and_load_success(void);
extern void test_sdf_storage_nuki_load_not_found(void);
extern void test_sdf_storage_nuki_save_invalid_args(void);
extern void test_sdf_storage_nuki_load_invalid_args(void);
extern void test_sdf_storage_nuki_clear_success(void);
extern void test_sdf_storage_nuki_clear_already_cleared(void);
extern void test_sdf_storage_ble_target_save_and_load_success(void);
extern void test_sdf_storage_ble_target_load_not_found(void);
extern void test_sdf_storage_ble_target_clear_success(void);
extern void test_sdf_storage_ble_target_clear_already_cleared(void);
extern void test_sdf_storage_erase_all_success(void);
extern void test_sdf_storage_erase_all_idempotent(void);
extern void test_sdf_storage_web_user_save_and_load_success(void);
extern void test_sdf_storage_web_user_load_not_found(void);
extern void test_sdf_storage_web_user_find_by_name_hit(void);
extern void test_sdf_storage_web_user_find_by_name_miss(void);
extern void test_sdf_storage_web_user_clear_success(void);
extern void test_sdf_storage_web_user_clear_all(void);
extern void test_sdf_storage_web_user_count(void);
extern void test_sdf_storage_web_user_save_index_at_max_rejected(void);

/* SDF Config tests */
extern void test_sdf_config_set_checkin_interval_bounds(void);
extern void test_sdf_config_set_failed_attempt_threshold_bounds(void);
extern void test_sdf_config_set_failed_attempt_window_bounds(void);
extern void test_sdf_config_set_lockout_duration_bounds(void);
extern void test_sdf_config_set_battery_default_percent_bounds(void);
extern void test_sdf_config_wdt_timeout_ms_populated_after_init(void);
extern void test_sdf_config_deep_sleep_fallback_follows_kconfig(void);
extern void test_sdf_config_zigbee_enabled_drives_protocol_is_enabled(void);

/* SDF Platform tests */
extern void test_sdf_platform_gpio_set_level_returns_mock_ok(void);
extern void test_sdf_platform_gpio_get_level_returns_mock_fixed_value(void);
extern void test_sdf_platform_gpio_is_rtc_capable_boundary(void);
extern void test_sdf_platform_map_wakeup_reason_all_causes(void);
extern void test_sdf_power_crc16_ccitt_known_vector(void);
extern void test_sdf_platform_sleep_retention_linux_noops(void);
extern void test_sdf_platform_sleep_wakeup_from_linux_noops(void);
extern void test_sdf_platform_nvs_security_status_defaults_and_erase_before_init(void);

/* SDF Platform Power tests */
extern void test_sdf_platform_power_enable_gpio_wake_delegates(void);
extern void test_sdf_platform_power_gate_ble_radio_always_invalid_state(void);

/* Zigbee protocol tests */
extern void test_sdf_protocol_zigbee_factory_reset_disabled_returns_not_supported(void);
extern void test_sdf_protocol_zigbee_factory_reset_enabled_returns_ok(void);

/* SDF Services tests */
extern void test_sdf_services_reset_state_returns_ok(void);
extern void test_sdf_services_reset_state_can_be_called_multiple_times(void);
extern void test_web_auth_verify_login_matching_hash_is_valid(void);
extern void test_web_auth_verify_login_mismatched_hash_is_invalid(void);
extern void test_web_auth_verify_login_wrong_hash_len_is_invalid(void);
extern void test_web_auth_verify_login_all_zero_hash_is_invalid(void);
extern void test_web_auth_decide_registration_authorized_persists_user(void);
extern void test_web_auth_decide_registration_denied_does_not_persist(void);
extern void test_web_auth_should_resolve_on_web_reg_auth_failure(void);
extern void test_web_auth_should_not_resolve_on_web_reg_auth_success(void);
extern void test_web_auth_should_not_resolve_for_other_actions(void);

/* Event Router tests */
extern void test_sdf_event_router_init_returns_ok(void);
extern void test_sdf_event_router_init_idempotent(void);
extern void test_sdf_event_router_subscribe_and_emit(void);
extern void test_sdf_event_router_subscribe_rejects_invalid_type(void);
extern void test_sdf_event_router_unsubscribe(void);

/* Tasks tests */
extern void test_sdf_power_wakeup_reason_mapping(void);
extern void test_sdf_power_checkin_clamping(void);
extern void test_sdf_power_battery_bounds(void);
extern void test_sdf_power_calculate_checkin_interval_disabled_returns_base(void);
extern void test_sdf_power_calculate_checkin_interval_enabled_scales_with_battery(void);

/* Nuki crypto tests */
extern void test_crypto_secretbox_round_trip(void);
extern void test_crypto_secretbox_tampered_ciphertext(void);
extern void test_crypto_secretbox_too_short(void);
extern void test_crypto_secretbox_open_too_short(void);
extern void test_crypto_core_hsalsa20_deterministic(void);
extern void test_crypto_core_hsalsa20_different_keys(void);
extern void test_crypto_scalarmult_basepoint(void);
extern void test_crypto_scalarmult_null_args(void);
extern void test_crypto_scalarmult_dh_agreement(void);

/* sdf_cli tests */
extern void test_sdf_cli_initial_state_is_unauthenticated(void);
extern void test_sdf_cli_can_authenticate_and_logout(void);
extern void test_sdf_cli_register_commands_runs_without_crash(void);
extern void test_user_list_formats_output(void);
extern void test_user_get_valid_id(void);
extern void test_user_get_invalid_id(void);
extern void test_nuki_status_not_paired(void);
extern void test_nuki_status_paired(void);
extern void test_nuki_connect_not_paired(void);
extern void test_nuki_connect_already_connected(void);
extern void test_nuki_pair_already_paired_warns(void);
extern void test_zigbee_status_disabled(void);
extern void test_zigbee_connect_already_joined(void);
extern void test_zigbee_unpair_not_joined(void);

/* Nuki pairing tests */
extern void test_pairing_init_success(void);
extern void test_pairing_init_null_args(void);
extern void test_pairing_init_long_name_truncated(void);
extern void test_pairing_start_sends_request(void);
extern void test_pairing_start_null_args(void);
extern void test_pairing_get_credentials_not_complete(void);
extern void test_pairing_get_credentials_null_args(void);
extern void test_pairing_get_credentials_when_complete(void);
extern void test_pairing_handle_unencrypted_null_args(void);
extern void test_pairing_handle_unencrypted_overflow_protection(void);
extern void test_pairing_first_challenge_sends_only_authenticator(void);
extern void test_pairing_second_challenge_sends_authorization_data(void);
extern void test_pairing_authorization_id_completes_pairing(void);
extern void test_pairing_handle_encrypted_null_args(void);

/* sdf_ota tests */
extern void test_sdf_ota_version_compare_equal(void);
extern void test_sdf_ota_version_compare_leading_v_ignored(void);
extern void test_sdf_ota_version_compare_major_newer_and_older(void);
extern void test_sdf_ota_version_compare_minor_newer_and_older(void);
extern void test_sdf_ota_version_compare_patch_newer_and_older(void);
extern void test_sdf_ota_version_compare_release_newer_than_pre_release(void);
extern void test_sdf_ota_version_compare_pre_release_alphanumeric_order(void);
extern void test_sdf_ota_version_compare_build_metadata_ignored(void);
extern void test_sdf_ota_version_compare_malformed_input_returns_equal(void);
extern void test_sdf_ota_verify_signature_default_config_returns_ok(void);

/* Protocol BLE tests */
extern void test_client_init_success(void);
extern void test_client_init_null_args(void);
extern void test_client_reset_rx(void);
extern void test_client_reset_rx_null_safety(void);
extern void test_parse_challenge_success(void);
extern void test_parse_challenge_wrong_command(void);
extern void test_parse_challenge_null_args(void);
extern void test_parse_challenge_too_short(void);
extern void test_parse_status_success(void);
extern void test_parse_status_wrong_command(void);
extern void test_parse_keyturner_states_minimum(void);
extern void test_parse_keyturner_states_extended(void);
extern void test_parse_keyturner_states_too_short(void);
extern void test_parse_error_report_success(void);
extern void test_parse_error_report_too_short(void);
extern void test_compute_authenticator_deterministic(void);
extern void test_compute_authenticator_null_args(void);
extern void test_compute_authenticator_different_data(void);
extern void test_compute_shared_key_null_args(void);
extern void test_encrypt_decrypt_round_trip(void);
extern void test_nonce_replay_detection(void);
extern void test_nonce_replay_window_honors_configured_value(void);
extern void test_nonce_replay_window_oversized_clamps_without_oob(void);
extern void test_nonce_replay_window_zero_disables_tracking_without_fault(void);
extern void test_feed_encrypted_partial_data(void);
extern void test_feed_encrypted_null_args(void);
extern void test_send_unencrypted_sends_framed_message(void);

extern void test_ble_ota_protocol_parse_begin_valid(void);
extern void test_ble_ota_protocol_parse_begin_wrong_length(void);
extern void test_ble_ota_protocol_parse_begin_zero_size(void);
extern void test_ble_ota_protocol_parse_begin_null_args(void);
extern void test_ble_ota_protocol_validate_end_empty_accepted(void);
extern void test_ble_ota_protocol_validate_end_nonempty_rejected(void);
extern void test_ble_ota_protocol_max_chunk_len_typical_mtu(void);
extern void test_ble_ota_protocol_max_chunk_len_too_small_mtu(void);
extern void test_ble_ota_protocol_validate_chunk_at_max_accepted(void);
extern void test_ble_ota_protocol_validate_chunk_over_max_rejected(void);
extern void test_ble_ota_protocol_validate_chunk_empty_rejected(void);
extern void test_ble_ota_protocol_validate_chunk_mtu_too_small_rejected(void);
extern void test_ble_ota_protocol_decide_begin_no_session_starts_new(void);
extern void test_ble_ota_protocol_decide_begin_matching_size_resumes(void);
extern void test_ble_ota_protocol_decide_begin_mismatched_size_rejected(void);

void app_main(void) {
  printf("Starting Smart Door Firmware (SDF) Tests...\n");

  /* Several suites (sdf_event_router's queue depth, sdf_services' default
   * config) read Kconfig-derived values via sdf_config_get(), which is
   * zero-initialized until sdf_config_init() runs. On real hardware this
   * happens as part of normal boot before any subsystem starts; mirror
   * that here so tests see the same non-zero defaults. */
  sdf_config_init();

  UNITY_BEGIN();

  /* Enrollment SM */
  RUN_TEST(test_enrollment_sm_initialization);
  RUN_TEST(test_enrollment_sm_start_valid);
  RUN_TEST(test_enrollment_sm_start_invalid);
  RUN_TEST(test_enrollment_sm_success_sequence);
  RUN_TEST(test_enrollment_sm_failure_handling);
  RUN_TEST(test_enrollment_sm_user_occupied);
  RUN_TEST(test_enrollment_sm_is_active_idle);
  RUN_TEST(test_enrollment_sm_is_active_steps);
  RUN_TEST(test_enrollment_sm_is_active_error);

  RUN_TEST(test_enrollment_sm_current_step);
  RUN_TEST(test_enrollment_sm_current_command);
  RUN_TEST(test_enrollment_sm_reset);
  RUN_TEST(test_enrollment_sm_null_safety);
  RUN_TEST(test_enrollment_sm_start_while_active);
  RUN_TEST(test_enrollment_sm_failure_at_step2);
  RUN_TEST(test_enrollment_sm_failure_at_step3);
  RUN_TEST(test_enrollment_sm_apply_on_idle);
  RUN_TEST(test_enrollment_sm_apply_after_success);
  RUN_TEST(test_enrollment_sm_start_permission_boundaries);
  RUN_TEST(test_enrollment_sm_finger_occupied);

  /* Driver utils */
  RUN_TEST(test_driver_user_id_validation);

  /* Driver protocol */
  RUN_TEST(test_checksum_all_zeros);
  RUN_TEST(test_checksum_known_command);
  RUN_TEST(test_checksum_with_parameters);
  RUN_TEST(test_checksum_all_ff);
  RUN_TEST(test_checksum_cancellation);
  RUN_TEST(test_user_id_valid_min);
  RUN_TEST(test_user_id_valid_max);
  RUN_TEST(test_user_id_valid_mid);
  RUN_TEST(test_user_id_invalid_zero);
  RUN_TEST(test_user_id_invalid_above_max);
  RUN_TEST(test_user_id_invalid_uint16_max);
  RUN_TEST(test_map_ack_success);
  RUN_TEST(test_map_ack_timeout);
  RUN_TEST(test_map_ack_full);
  RUN_TEST(test_map_ack_user_occupied);
  RUN_TEST(test_map_ack_finger_occupied);
  RUN_TEST(test_map_ack_fail);
  RUN_TEST(test_map_ack_nouser);
  RUN_TEST(test_map_ack_unknown);

  /* Storage tests */
  RUN_TEST(test_sdf_storage_nuki_save_and_load_success);
  RUN_TEST(test_sdf_storage_nuki_load_not_found);
  RUN_TEST(test_sdf_storage_nuki_save_invalid_args);
  RUN_TEST(test_sdf_storage_nuki_load_invalid_args);
  RUN_TEST(test_sdf_storage_nuki_clear_success);
  RUN_TEST(test_sdf_storage_nuki_clear_already_cleared);
  RUN_TEST(test_sdf_storage_ble_target_save_and_load_success);
  RUN_TEST(test_sdf_storage_ble_target_load_not_found);
  RUN_TEST(test_sdf_storage_ble_target_clear_success);
  RUN_TEST(test_sdf_storage_ble_target_clear_already_cleared);
  RUN_TEST(test_sdf_storage_erase_all_success);
  RUN_TEST(test_sdf_storage_erase_all_idempotent);
  RUN_TEST(test_sdf_storage_web_user_save_and_load_success);
  RUN_TEST(test_sdf_storage_web_user_load_not_found);
  RUN_TEST(test_sdf_storage_web_user_find_by_name_hit);
  RUN_TEST(test_sdf_storage_web_user_find_by_name_miss);
  RUN_TEST(test_sdf_storage_web_user_clear_success);
  RUN_TEST(test_sdf_storage_web_user_clear_all);
  RUN_TEST(test_sdf_storage_web_user_count);
  RUN_TEST(test_sdf_storage_web_user_save_index_at_max_rejected);

  /* SDF Config tests */
  RUN_TEST(test_sdf_config_set_checkin_interval_bounds);
  RUN_TEST(test_sdf_config_set_failed_attempt_threshold_bounds);
  RUN_TEST(test_sdf_config_set_failed_attempt_window_bounds);
  RUN_TEST(test_sdf_config_set_lockout_duration_bounds);
  RUN_TEST(test_sdf_config_set_battery_default_percent_bounds);
  RUN_TEST(test_sdf_config_wdt_timeout_ms_populated_after_init);
  RUN_TEST(test_sdf_config_deep_sleep_fallback_follows_kconfig);
  RUN_TEST(test_sdf_config_zigbee_enabled_drives_protocol_is_enabled);

  /* SDF Platform tests */
  RUN_TEST(test_sdf_platform_gpio_set_level_returns_mock_ok);
  RUN_TEST(test_sdf_platform_gpio_get_level_returns_mock_fixed_value);
  RUN_TEST(test_sdf_platform_gpio_is_rtc_capable_boundary);
  RUN_TEST(test_sdf_platform_map_wakeup_reason_all_causes);
  RUN_TEST(test_sdf_power_crc16_ccitt_known_vector);
  RUN_TEST(test_sdf_platform_sleep_retention_linux_noops);
  RUN_TEST(test_sdf_platform_sleep_wakeup_from_linux_noops);
  RUN_TEST(test_sdf_platform_nvs_security_status_defaults_and_erase_before_init);

  /* SDF Platform Power tests */
  RUN_TEST(test_sdf_platform_power_enable_gpio_wake_delegates);
  RUN_TEST(test_sdf_platform_power_gate_ble_radio_always_invalid_state);

  /* Zigbee protocol tests */
  RUN_TEST(test_sdf_protocol_zigbee_factory_reset_disabled_returns_not_supported);
  RUN_TEST(test_sdf_protocol_zigbee_factory_reset_enabled_returns_ok);

  /* SDF Services tests */
  RUN_TEST(test_sdf_services_reset_state_returns_ok);
  RUN_TEST(test_sdf_services_reset_state_can_be_called_multiple_times);
  RUN_TEST(test_web_auth_verify_login_matching_hash_is_valid);
  RUN_TEST(test_web_auth_verify_login_mismatched_hash_is_invalid);
  RUN_TEST(test_web_auth_verify_login_wrong_hash_len_is_invalid);
  RUN_TEST(test_web_auth_verify_login_all_zero_hash_is_invalid);
  RUN_TEST(test_web_auth_decide_registration_authorized_persists_user);
  RUN_TEST(test_web_auth_decide_registration_denied_does_not_persist);
  RUN_TEST(test_web_auth_should_resolve_on_web_reg_auth_failure);
  RUN_TEST(test_web_auth_should_not_resolve_on_web_reg_auth_success);
  RUN_TEST(test_web_auth_should_not_resolve_for_other_actions);

  /* Event Router tests */
  RUN_TEST(test_sdf_event_router_init_returns_ok);
  RUN_TEST(test_sdf_event_router_init_idempotent);
  RUN_TEST(test_sdf_event_router_subscribe_and_emit);
  RUN_TEST(test_sdf_event_router_subscribe_rejects_invalid_type);
  RUN_TEST(test_sdf_event_router_unsubscribe);

  /* Tasks tests */
  RUN_TEST(test_sdf_power_wakeup_reason_mapping);
  RUN_TEST(test_sdf_power_checkin_clamping);
  RUN_TEST(test_sdf_power_battery_bounds);
  RUN_TEST(test_sdf_power_calculate_checkin_interval_disabled_returns_base);
  RUN_TEST(test_sdf_power_calculate_checkin_interval_enabled_scales_with_battery);

  /* Nuki crypto tests */
  RUN_TEST(test_crypto_secretbox_round_trip);
  RUN_TEST(test_crypto_secretbox_tampered_ciphertext);
  RUN_TEST(test_crypto_secretbox_too_short);
  RUN_TEST(test_crypto_secretbox_open_too_short);
  RUN_TEST(test_crypto_core_hsalsa20_deterministic);
  RUN_TEST(test_crypto_core_hsalsa20_different_keys);
  RUN_TEST(test_crypto_scalarmult_basepoint);
  RUN_TEST(test_crypto_scalarmult_null_args);
  RUN_TEST(test_crypto_scalarmult_dh_agreement);

  /* Nuki pairing tests */
  RUN_TEST(test_pairing_init_success);
  RUN_TEST(test_pairing_init_null_args);
  RUN_TEST(test_pairing_init_long_name_truncated);
  RUN_TEST(test_pairing_start_sends_request);
  RUN_TEST(test_pairing_start_null_args);
  RUN_TEST(test_pairing_get_credentials_not_complete);
  RUN_TEST(test_pairing_get_credentials_null_args);
  RUN_TEST(test_pairing_get_credentials_when_complete);
  RUN_TEST(test_pairing_handle_unencrypted_null_args);
  RUN_TEST(test_pairing_handle_unencrypted_overflow_protection);
  RUN_TEST(test_pairing_first_challenge_sends_only_authenticator);
  RUN_TEST(test_pairing_second_challenge_sends_authorization_data);
  RUN_TEST(test_pairing_authorization_id_completes_pairing);
  RUN_TEST(test_pairing_handle_encrypted_null_args);

  /* sdf_cli tests */
  RUN_TEST(test_sdf_cli_initial_state_is_unauthenticated);
  RUN_TEST(test_sdf_cli_can_authenticate_and_logout);
  RUN_TEST(test_sdf_cli_register_commands_runs_without_crash);
  RUN_TEST(test_user_list_formats_output);
  RUN_TEST(test_user_get_valid_id);
  RUN_TEST(test_user_get_invalid_id);
  RUN_TEST(test_nuki_status_not_paired);
  RUN_TEST(test_nuki_status_paired);
  RUN_TEST(test_nuki_connect_not_paired);
  RUN_TEST(test_nuki_connect_already_connected);
  RUN_TEST(test_nuki_pair_already_paired_warns);
  RUN_TEST(test_zigbee_status_disabled);
  RUN_TEST(test_zigbee_connect_already_joined);
  RUN_TEST(test_zigbee_unpair_not_joined);

  /* sdf_ota tests */
  RUN_TEST(test_sdf_ota_version_compare_equal);
  RUN_TEST(test_sdf_ota_version_compare_leading_v_ignored);
  RUN_TEST(test_sdf_ota_version_compare_major_newer_and_older);
  RUN_TEST(test_sdf_ota_version_compare_minor_newer_and_older);
  RUN_TEST(test_sdf_ota_version_compare_patch_newer_and_older);
  RUN_TEST(test_sdf_ota_version_compare_release_newer_than_pre_release);
  RUN_TEST(test_sdf_ota_version_compare_pre_release_alphanumeric_order);
  RUN_TEST(test_sdf_ota_version_compare_build_metadata_ignored);
  RUN_TEST(test_sdf_ota_version_compare_malformed_input_returns_equal);
  RUN_TEST(test_sdf_ota_verify_signature_default_config_returns_ok);

  printf("\n-----------------------\n");
  /* Protocol BLE tests */
  RUN_TEST(test_client_init_success);
  RUN_TEST(test_client_init_null_args);
  RUN_TEST(test_client_reset_rx);
  RUN_TEST(test_client_reset_rx_null_safety);
  RUN_TEST(test_parse_challenge_success);
  RUN_TEST(test_parse_challenge_wrong_command);
  RUN_TEST(test_parse_challenge_null_args);
  RUN_TEST(test_parse_challenge_too_short);
  RUN_TEST(test_parse_status_success);
  RUN_TEST(test_parse_status_wrong_command);
  RUN_TEST(test_parse_keyturner_states_minimum);
  RUN_TEST(test_parse_keyturner_states_extended);
  RUN_TEST(test_parse_keyturner_states_too_short);
  RUN_TEST(test_parse_error_report_success);
  RUN_TEST(test_parse_error_report_too_short);
  RUN_TEST(test_compute_authenticator_deterministic);
  RUN_TEST(test_compute_authenticator_null_args);
  RUN_TEST(test_compute_authenticator_different_data);
  RUN_TEST(test_compute_shared_key_null_args);
  RUN_TEST(test_encrypt_decrypt_round_trip);
  RUN_TEST(test_nonce_replay_detection);
  RUN_TEST(test_nonce_replay_window_honors_configured_value);
  RUN_TEST(test_nonce_replay_window_oversized_clamps_without_oob);
  RUN_TEST(test_nonce_replay_window_zero_disables_tracking_without_fault);
  RUN_TEST(test_feed_encrypted_partial_data);
  RUN_TEST(test_feed_encrypted_null_args);
  RUN_TEST(test_send_unencrypted_sends_framed_message);

  /* BLE OTA wire-format protocol tests */
  RUN_TEST(test_ble_ota_protocol_parse_begin_valid);
  RUN_TEST(test_ble_ota_protocol_parse_begin_wrong_length);
  RUN_TEST(test_ble_ota_protocol_parse_begin_zero_size);
  RUN_TEST(test_ble_ota_protocol_parse_begin_null_args);
  RUN_TEST(test_ble_ota_protocol_validate_end_empty_accepted);
  RUN_TEST(test_ble_ota_protocol_validate_end_nonempty_rejected);
  RUN_TEST(test_ble_ota_protocol_max_chunk_len_typical_mtu);
  RUN_TEST(test_ble_ota_protocol_max_chunk_len_too_small_mtu);
  RUN_TEST(test_ble_ota_protocol_validate_chunk_at_max_accepted);
  RUN_TEST(test_ble_ota_protocol_validate_chunk_over_max_rejected);
  RUN_TEST(test_ble_ota_protocol_validate_chunk_empty_rejected);
  RUN_TEST(test_ble_ota_protocol_validate_chunk_mtu_too_small_rejected);
  RUN_TEST(test_ble_ota_protocol_decide_begin_no_session_starts_new);
  RUN_TEST(test_ble_ota_protocol_decide_begin_matching_size_resumes);
  RUN_TEST(test_ble_ota_protocol_decide_begin_mismatched_size_rejected);

  /* app_main() is declared void and called without capturing a return value
   * by the linux target's main_task() (see FreeRTOS-Kernel port_idf.c), so
   * "return UNITY_END();" alone wouldn't reach the process exit code. Call
   * exit() directly instead, so a failing suite makes this binary usable as
   * a CI gate (non-zero exit == test failure). */
  int failures = UNITY_END();
  exit(failures == 0 ? 0 : 1);
}

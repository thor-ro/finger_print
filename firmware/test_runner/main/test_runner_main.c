#include "unity.h"
#include <stdio.h>

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

/* Lock flow tests */
extern void test_lock_flow_init(void);
extern void test_lock_flow_reset_preserves_max_retries(void);
extern void test_lock_flow_is_idle_on_init(void);
extern void test_lock_flow_is_not_idle_when_active(void);
extern void test_lock_flow_begin_success(void);
extern void test_lock_flow_begin_rejected_when_active(void);
extern void test_lock_flow_retry_increments(void);
extern void test_lock_flow_retry_exhaustion(void);
extern void test_lock_flow_on_status_complete(void);
extern void test_lock_flow_on_status_accepted_noop(void);

/* Storage tests */
extern void test_sdf_storage_nuki_save_and_load_success(void);
extern void test_sdf_storage_nuki_load_not_found(void);
extern void test_sdf_storage_nuki_save_invalid_args(void);
extern void test_sdf_storage_nuki_load_invalid_args(void);
extern void test_sdf_storage_nuki_clear_success(void);
extern void test_sdf_storage_nuki_clear_already_cleared(void);
extern void test_sdf_storage_ble_target_save_and_load_success(void);
extern void test_sdf_storage_ble_target_load_not_found(void);

/* Tasks tests */
extern void test_sdf_power_wakeup_reason_mapping(void);
extern void test_sdf_power_checkin_clamping(void);
extern void test_sdf_power_battery_bounds(void);

/* SDF App tests */
extern void test_sdf_app_string_mappers(void);
extern void test_sdf_app_valid_lock_action_logic(void);
extern void test_sdf_app_map_lock_state_to_zigbee_logic(void);
extern void test_sdf_app_choose_fingerprint_permission_logic(void);

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
extern void test_feed_encrypted_partial_data(void);
extern void test_feed_encrypted_null_args(void);
extern void test_send_unencrypted_sends_framed_message(void);

void app_main(void) {
  printf("Starting Smart Door Firmware (SDF) Tests...\n");

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

  /* Lock flow */
  RUN_TEST(test_lock_flow_init);
  RUN_TEST(test_lock_flow_reset_preserves_max_retries);
  RUN_TEST(test_lock_flow_is_idle_on_init);
  RUN_TEST(test_lock_flow_is_not_idle_when_active);
  RUN_TEST(test_lock_flow_begin_success);
  RUN_TEST(test_lock_flow_begin_rejected_when_active);
  RUN_TEST(test_lock_flow_retry_increments);
  RUN_TEST(test_lock_flow_retry_exhaustion);
  RUN_TEST(test_lock_flow_on_status_complete);
  RUN_TEST(test_lock_flow_on_status_accepted_noop);

  /* Storage tests */
  RUN_TEST(test_sdf_storage_nuki_save_and_load_success);
  RUN_TEST(test_sdf_storage_nuki_load_not_found);
  RUN_TEST(test_sdf_storage_nuki_save_invalid_args);
  RUN_TEST(test_sdf_storage_nuki_load_invalid_args);
  RUN_TEST(test_sdf_storage_nuki_clear_success);
  RUN_TEST(test_sdf_storage_nuki_clear_already_cleared);
  RUN_TEST(test_sdf_storage_ble_target_save_and_load_success);
  RUN_TEST(test_sdf_storage_ble_target_load_not_found);

  /* Tasks tests */
  RUN_TEST(test_sdf_power_wakeup_reason_mapping);
  RUN_TEST(test_sdf_power_checkin_clamping);
  RUN_TEST(test_sdf_power_battery_bounds);

  /* SDF App tests */
  RUN_TEST(test_sdf_app_string_mappers);
  RUN_TEST(test_sdf_app_valid_lock_action_logic);
  RUN_TEST(test_sdf_app_map_lock_state_to_zigbee_logic);
  RUN_TEST(test_sdf_app_choose_fingerprint_permission_logic);

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

  // sdf_protocol_ble (Nuki) tests:
  // (Assuming these are handled elsewhere, or were incorrectly added in my
  // previous step, removing invalid macro)

  // sdf_cli tests
  extern void test_sdf_cli_initial_state_is_unauthenticated(void);
  extern void test_sdf_cli_can_authenticate_and_logout(void);

  // Mapped from test names in test_sdf_cli.c:
  // TEST_CASE("sdf_cli initial state is unauthenticated", "[sdf_cli]")
  // TEST_CASE("sdf_cli can authenticate and logout", "[sdf_cli]")
  // Note: esp-idf unity translates TEST_CASE macro to a function name. But we
  // can just use unity's RUN_TEST directly if we wrap them properly, or include
  // unity_fixture.h. Actually, ESP-IDF uses `unity_run_menu()` or similar to
  // run all `TEST_CASE`s automatically when building test apps, but this is a
  // custom linux test runner using standard Unity. Let me just manually declare
  // the functions for the tests I created. Wait, ESP-IDF's TEST_CASE macro in
  // `unity.h` registers them in a linker section. Let me replace the
  // RUN_TEST_SUITE call with a call to `unity_run_menu` to run all registered
  // tests, or I can just rename my tests to standard void functions.
  RUN_TEST(test_sdf_cli_initial_state_is_unauthenticated);
  RUN_TEST(test_sdf_cli_can_authenticate_and_logout);

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
  RUN_TEST(test_feed_encrypted_partial_data);
  RUN_TEST(test_feed_encrypted_null_args);
  RUN_TEST(test_send_unencrypted_sends_framed_message);

  UNITY_END();
}

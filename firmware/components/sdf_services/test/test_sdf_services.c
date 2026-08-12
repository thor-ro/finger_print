#include "unity.h"

#include <string.h>

#include "sdf_services.h"
#include "sdf_services_internal.h"

/* Needed only for make_response()'s independent reference HMAC below - see
 * sdf_services_web_auth.c for why these defines/private headers are needed
 * with mbedtls 4.x. */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/md.h"

/* sdf_services_reset_state() operates on a mutex created by
 * sdf_services_init(); on real hardware sdf_app brings services up during
 * boot before anything can call reset_state(). Mirror that here so the
 * test doesn't depend on some other suite happening to have initialized
 * services first. Idempotent, so safe to call from both tests below. */
static void ensure_services_initialized(void) {
  sdf_services_config_t cfg;
  sdf_services_get_default_config(&cfg);
  sdf_services_init(&cfg);
}

void test_sdf_services_reset_state_returns_ok(void) {
  ensure_services_initialized();
  esp_err_t err = sdf_services_reset_state();
  TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_sdf_services_reset_state_can_be_called_multiple_times(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
}

/* Bitmap helper tests */

void test_bitmap_set_clears_correct_bit(void) {
  uint16_t bmp = 0;
  SDF_SERVICES_BMP_SET(bmp, 1);
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(bmp, 1));
  TEST_ASSERT_FALSE(SDF_SERVICES_BMP_TEST(bmp, 2));
  SDF_SERVICES_BMP_SET(bmp, 10);
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(bmp, 10));
}

void test_bitmap_clear_clears_correct_bit(void) {
  uint16_t bmp = 0;
  SDF_SERVICES_BMP_SET(bmp, 3);
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(bmp, 3));
  SDF_SERVICES_BMP_CLEAR(bmp, 3);
  TEST_ASSERT_FALSE(SDF_SERVICES_BMP_TEST(bmp, 3));
}

void test_bitmap_multiple_bits_independent(void) {
  uint16_t bmp = 0;
  SDF_SERVICES_BMP_SET(bmp, 1);
  SDF_SERVICES_BMP_SET(bmp, 5);
  SDF_SERVICES_BMP_SET(bmp, 10);
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(bmp, 1));
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(bmp, 5));
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(bmp, 10));
  TEST_ASSERT_FALSE(SDF_SERVICES_BMP_TEST(bmp, 2));
  TEST_ASSERT_FALSE(SDF_SERVICES_BMP_TEST(bmp, 4));
  SDF_SERVICES_BMP_CLEAR(bmp, 5);
  TEST_ASSERT_FALSE(SDF_SERVICES_BMP_TEST(bmp, 5));
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(bmp, 1));
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(bmp, 10));
}

/* Permission packing helper tests */

void test_perm_get_returns_zero_for_unset(void) {
  uint8_t packed[4] = {0};
  TEST_ASSERT_EQUAL(0, sdf_services_perm_get(packed, 1));
  TEST_ASSERT_EQUAL(0, sdf_services_perm_get(packed, 10));
}

void test_perm_set_and_get_roundtrip(void) {
  uint8_t packed[4] = {0};
  sdf_services_perm_set(packed, 1, 1);
  TEST_ASSERT_EQUAL(1, sdf_services_perm_get(packed, 1));
  sdf_services_perm_set(packed, 1, 3);
  TEST_ASSERT_EQUAL(3, sdf_services_perm_get(packed, 1));
}

void test_perm_set_and_get_for_max_user(void) {
  uint8_t packed[4] = {0};
  sdf_services_perm_set(packed, 10, 2);
  TEST_ASSERT_EQUAL(2, sdf_services_perm_get(packed, 10));
}

void test_perm_set_does_not_corrupt_other_users(void) {
  uint8_t packed[4] = {0};
  sdf_services_perm_set(packed, 1, 1);
  sdf_services_perm_set(packed, 5, 2);
  sdf_services_perm_set(packed, 10, 3);
  TEST_ASSERT_EQUAL(1, sdf_services_perm_get(packed, 1));
  TEST_ASSERT_EQUAL(2, sdf_services_perm_get(packed, 5));
  TEST_ASSERT_EQUAL(3, sdf_services_perm_get(packed, 10));
  TEST_ASSERT_EQUAL(0, sdf_services_perm_get(packed, 2));
  TEST_ASSERT_EQUAL(0, sdf_services_perm_get(packed, 3));
  TEST_ASSERT_EQUAL(0, sdf_services_perm_get(packed, 4));
  TEST_ASSERT_EQUAL(0, sdf_services_perm_get(packed, 6));
}

void test_perm_set_masks_high_bits(void) {
  uint8_t packed[4] = {0};
  sdf_services_perm_set(packed, 1, 5);
  TEST_ASSERT_EQUAL(1, sdf_services_perm_get(packed, 1));
  sdf_services_perm_set(packed, 1, 7);
  TEST_ASSERT_EQUAL(3, sdf_services_perm_get(packed, 1));
}

/* Pack user list test */

void test_pack_user_list_empty(void) {
  uint16_t user_ids[1] = {0};
  uint8_t perms[1] = {0};
  uint16_t bmp = 0;
  uint8_t perm_packed[4] = {0};
  sdf_services_pack_user_list(user_ids, perms, 0, &bmp, perm_packed);
  TEST_ASSERT_EQUAL(0, bmp);
  TEST_ASSERT_EQUAL(0, perm_packed[0]);
  TEST_ASSERT_EQUAL(0, perm_packed[1]);
  TEST_ASSERT_EQUAL(0, perm_packed[2]);
  TEST_ASSERT_EQUAL(0, perm_packed[3]);
}

/* Web auth: PBKDF2-HMAC-SHA256 credential stretch */

void test_web_auth_stretch_credential_is_deterministic(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0xAB, sizeof(hash));
  uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(salt, 0x11, sizeof(salt));

  uint8_t out1[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  uint8_t out2[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  /* Low iteration count - correctness/determinism only, not calibration. */
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_web_auth_stretch_credential(hash, sizeof(hash), salt, 4, out1));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_web_auth_stretch_credential(hash, sizeof(hash), salt, 4, out2));

  TEST_ASSERT_EQUAL_UINT8_ARRAY(out1, out2, SDF_STORAGE_WEB_USER_STRETCHED_LEN);
}

void test_web_auth_stretch_credential_differs_by_salt(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0xAB, sizeof(hash));
  uint8_t salt_a[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(salt_a, 0x11, sizeof(salt_a));
  uint8_t salt_b[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(salt_b, 0x22, sizeof(salt_b));

  uint8_t out_a[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  uint8_t out_b[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_web_auth_stretch_credential(hash, sizeof(hash), salt_a, 4, out_a));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_web_auth_stretch_credential(hash, sizeof(hash), salt_b, 4, out_b));

  TEST_ASSERT_NOT_EQUAL(0, memcmp(out_a, out_b, SDF_STORAGE_WEB_USER_STRETCHED_LEN));
}

void test_web_auth_stretch_credential_invalid_args(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN] = {0};
  uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN] = {0};
  uint8_t out[SDF_STORAGE_WEB_USER_STRETCHED_LEN] = {0};

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                     sdf_services_web_auth_stretch_credential(NULL, sizeof(hash), salt, 4, out));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                     sdf_services_web_auth_stretch_credential(hash, sizeof(hash) - 1, salt, 4, out));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                     sdf_services_web_auth_stretch_credential(hash, sizeof(hash), salt, 0, out));
}

/* Web auth: LOGIN_VERIFY response verification */

static void make_response(const uint8_t stretched[SDF_STORAGE_WEB_USER_STRETCHED_LEN],
                           const uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN],
                           uint8_t response_out[SDF_SERVICES_WEB_AUTH_RESPONSE_LEN]) {
  /* Mirrors the device's own HMAC so tests don't depend on an internal
   * helper: HMAC-SHA256(stretched_credential, nonce). Reuses
   * verify_response's own correctness indirectly by round-tripping through
   * it below - this just needs *a* correct reference value, produced the
   * same way the browser would via crypto.subtle.sign. Computed here with
   * mbedtls directly since this test file already links mbedtls transitively
   * via sdf_services. */
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), stretched,
                   SDF_STORAGE_WEB_USER_STRETCHED_LEN, nonce, SDF_SERVICES_WEB_AUTH_NONCE_LEN,
                   response_out);
}

void test_web_auth_verify_response_matching_is_valid(void) {
  uint8_t stretched[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  memset(stretched, 0xAB, sizeof(stretched));
  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x42, sizeof(nonce));
  uint8_t response[SDF_SERVICES_WEB_AUTH_RESPONSE_LEN];
  make_response(stretched, nonce, response);

  TEST_ASSERT_TRUE(sdf_services_web_auth_verify_response(stretched, nonce, sizeof(nonce), response, sizeof(response)));
}

void test_web_auth_verify_response_mismatched_is_invalid(void) {
  uint8_t stretched[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  memset(stretched, 0xAB, sizeof(stretched));
  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x42, sizeof(nonce));

  uint8_t wrong_response[SDF_SERVICES_WEB_AUTH_RESPONSE_LEN];
  memset(wrong_response, 0xFF, sizeof(wrong_response));

  TEST_ASSERT_FALSE(sdf_services_web_auth_verify_response(stretched, nonce, sizeof(nonce), wrong_response, sizeof(wrong_response)));
}

void test_web_auth_verify_response_wrong_nonce_len_is_invalid(void) {
  uint8_t stretched[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  memset(stretched, 0xAB, sizeof(stretched));
  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x42, sizeof(nonce));
  uint8_t response[SDF_SERVICES_WEB_AUTH_RESPONSE_LEN];
  make_response(stretched, nonce, response);

  TEST_ASSERT_FALSE(sdf_services_web_auth_verify_response(stretched, nonce, sizeof(nonce) - 1, response, sizeof(response)));
}

void test_web_auth_verify_response_wrong_response_len_is_invalid(void) {
  uint8_t stretched[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  memset(stretched, 0xAB, sizeof(stretched));
  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x42, sizeof(nonce));
  uint8_t response[SDF_SERVICES_WEB_AUTH_RESPONSE_LEN];
  make_response(stretched, nonce, response);

  TEST_ASSERT_FALSE(sdf_services_web_auth_verify_response(stretched, nonce, sizeof(nonce), response, sizeof(response) - 1));
}

void test_web_auth_verify_response_all_zero_response_is_invalid(void) {
  uint8_t stretched[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  memset(stretched, 0xAB, sizeof(stretched));
  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x42, sizeof(nonce));
  uint8_t zero_response[SDF_SERVICES_WEB_AUTH_RESPONSE_LEN] = {0};

  TEST_ASSERT_FALSE(sdf_services_web_auth_verify_response(stretched, nonce, sizeof(nonce), zero_response, sizeof(zero_response)));
}

/* Web auth: LOGIN_INIT challenge construction */

void test_web_auth_make_login_challenge_uses_stored_salt_and_nonce(void) {
  sdf_storage_web_user_t user = {0};
  strncpy(user.username, "alice", sizeof(user.username) - 1);
  memset(user.salt, 0x33, sizeof(user.salt));
  user.valid = true;

  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x77, sizeof(nonce));

  sdf_services_web_auth_challenge_t challenge = sdf_services_web_auth_make_login_challenge(&user, nonce);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(user.salt, challenge.salt, SDF_STORAGE_WEB_USER_SALT_LEN);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(nonce, challenge.nonce, SDF_SERVICES_WEB_AUTH_NONCE_LEN);
  TEST_ASSERT_EQUAL(SDF_SERVICES_WEB_AUTH_PBKDF2_ITERATIONS, challenge.iteration_count);
}

void test_web_auth_make_pseudo_challenge_is_deterministic_per_username(void) {
  uint8_t key[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN];
  memset(key, 0x55, sizeof(key));
  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x77, sizeof(nonce));

  sdf_services_web_auth_challenge_t c1 = sdf_services_web_auth_make_pseudo_challenge(key, "nobody", nonce);
  sdf_services_web_auth_challenge_t c2 = sdf_services_web_auth_make_pseudo_challenge(key, "nobody", nonce);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(c1.salt, c2.salt, SDF_STORAGE_WEB_USER_SALT_LEN);
}

void test_web_auth_make_pseudo_challenge_differs_by_username(void) {
  uint8_t key[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN];
  memset(key, 0x55, sizeof(key));
  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x77, sizeof(nonce));

  sdf_services_web_auth_challenge_t c1 = sdf_services_web_auth_make_pseudo_challenge(key, "nobody", nonce);
  sdf_services_web_auth_challenge_t c2 = sdf_services_web_auth_make_pseudo_challenge(key, "someone-else", nonce);

  TEST_ASSERT_NOT_EQUAL(0, memcmp(c1.salt, c2.salt, SDF_STORAGE_WEB_USER_SALT_LEN));
}

/* Known-user vs. unknown-user challenges must be indistinguishable in shape:
 * same field sizes, same iteration count, salt populated either way (never
 * an error/empty response that would itself leak "user not found"). */
void test_web_auth_known_and_unknown_challenges_are_same_shape(void) {
  sdf_storage_web_user_t user = {0};
  strncpy(user.username, "alice", sizeof(user.username) - 1);
  memset(user.salt, 0x33, sizeof(user.salt));
  user.valid = true;

  uint8_t pseudo_key[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN];
  memset(pseudo_key, 0x55, sizeof(pseudo_key));

  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x77, sizeof(nonce));

  sdf_services_web_auth_challenge_t known = sdf_services_web_auth_make_login_challenge(&user, nonce);
  sdf_services_web_auth_challenge_t unknown = sdf_services_web_auth_make_pseudo_challenge(pseudo_key, "nobody", nonce);

  TEST_ASSERT_EQUAL(sizeof(known.salt), sizeof(unknown.salt));
  TEST_ASSERT_EQUAL(sizeof(known.nonce), sizeof(unknown.nonce));
  TEST_ASSERT_EQUAL(known.iteration_count, unknown.iteration_count);
  /* Neither salt is all-zero/empty - both look like real random salts. */
  uint8_t zero_salt[SDF_STORAGE_WEB_USER_SALT_LEN] = {0};
  TEST_ASSERT_NOT_EQUAL(0, memcmp(zero_salt, known.salt, sizeof(zero_salt)));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(zero_salt, unknown.salt, sizeof(zero_salt)));
}

/* Web auth: registration decision */

void test_web_auth_decide_registration_authorized_persists_user(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0x11, sizeof(hash));
  uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(salt, 0x22, sizeof(salt));

  sdf_services_web_auth_registration_decision_t decision =
      sdf_services_web_auth_decide_registration("bob", hash, sizeof(hash), salt, 2, true);

  TEST_ASSERT_TRUE(decision.should_persist);
  TEST_ASSERT_TRUE(decision.reply_authorized);
  TEST_ASSERT_EQUAL_STRING("bob", decision.user.username);
  TEST_ASSERT_EQUAL(2, decision.user.permission);
  TEST_ASSERT_TRUE(decision.user.valid);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(salt, decision.user.salt, sizeof(salt));
  /* Stretched credential must not equal the raw received hash - that's the
   * entire point of this change (never persist the raw hash). */
  TEST_ASSERT_NOT_EQUAL(0, memcmp(hash, decision.user.stretched_credential, sizeof(hash)));

  /* And it must match running the stretch function directly with the same
   * inputs and the compile-time iteration count. */
  uint8_t expected[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_web_auth_stretch_credential(
                                 hash, sizeof(hash), salt, SDF_SERVICES_WEB_AUTH_PBKDF2_ITERATIONS, expected));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, decision.user.stretched_credential, SDF_STORAGE_WEB_USER_STRETCHED_LEN);
}

void test_web_auth_decide_registration_denied_does_not_persist(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0x11, sizeof(hash));
  uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(salt, 0x22, sizeof(salt));

  sdf_services_web_auth_registration_decision_t decision =
      sdf_services_web_auth_decide_registration("bob", hash, sizeof(hash), salt, 2, false);

  TEST_ASSERT_FALSE(decision.should_persist);
  TEST_ASSERT_FALSE(decision.reply_authorized);
}

/* Web auth: pending-registration resolve guard */

void test_web_auth_should_resolve_on_web_reg_auth_failure(void) {
  TEST_ASSERT_TRUE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH, ESP_FAIL));
  TEST_ASSERT_TRUE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH, ESP_ERR_TIMEOUT));
}

void test_web_auth_should_not_resolve_on_web_reg_auth_success(void) {
  TEST_ASSERT_FALSE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH, ESP_OK));
}

void test_web_auth_should_not_resolve_for_other_actions(void) {
  TEST_ASSERT_FALSE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_NONE, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ENROLL, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ZB_JOIN, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_web_auth_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW, ESP_FAIL));
}

/* BLE-triggered admin actions (Nuki re-pair, Enroll-Admin, Zigbee Join):
 * shared pending-request resolve guard */

void test_ble_admin_action_should_resolve_on_denial_or_timeout(void) {
  TEST_ASSERT_TRUE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR, ESP_FAIL));
  TEST_ASSERT_TRUE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR, ESP_ERR_TIMEOUT));
  TEST_ASSERT_TRUE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN, ESP_FAIL));
  TEST_ASSERT_TRUE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN, ESP_ERR_TIMEOUT));
  TEST_ASSERT_TRUE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ZB_JOIN, ESP_FAIL));
  TEST_ASSERT_TRUE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ZB_JOIN, ESP_ERR_TIMEOUT));
}

void test_ble_admin_action_should_not_resolve_on_success(void) {
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR, ESP_OK));
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN, ESP_OK));
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ZB_JOIN, ESP_OK));
}

void test_ble_admin_action_should_not_resolve_for_other_actions(void) {
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_NONE, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_ENROLL, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH, ESP_FAIL));
  TEST_ASSERT_FALSE(sdf_services_ble_admin_action_should_resolve_on_action_complete(
      SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW, ESP_FAIL));
}

/* Setup-state helper */

void test_setup_state_unclaimed_when_no_enrolled_users(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_UNCLAIMED, sdf_services_get_setup_state());
}

/* Button dispatch: BLE Companion pairing-window admin action (double-click).
 * enrolled_user_count is set directly on the internal state (rather than via
 * a real enrollment) so the "0 users, execute immediately" bypass in
 * sdf_button_dispatch_action() is not taken and the admin-fingerprint
 * pending-action gate is actually exercised - mirrors how NUKI_PAIR's own
 * gate is only reachable once a device is claimed. */

void test_button_dispatch_ble_pairing_window_sets_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_services_state()->enrolled_user_count = 1;

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW);

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW,
                     sdf_services_state()->pending_admin_action);
}

void test_button_dispatch_ble_pairing_window_ignored_when_action_already_pending(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_services_state()->enrolled_user_count = 1;

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR,
                     sdf_services_state()->pending_admin_action);

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW);

  /* The already-pending NUKI_PAIR request must not be clobbered by the
   * later double-click. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR,
                     sdf_services_state()->pending_admin_action);
}
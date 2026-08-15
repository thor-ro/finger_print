#include "unity.h"

#include <string.h>

#include "nvs_flash.h"

#include "sdf_services.h"
#include "sdf_services_internal.h"
#include "sdf_event_router.h"
#include "sdf_platform_time.h"

/* Test-only fault injector for sdf_storage_enrolled_users_save(), compiled
 * into sdf_storage only when SDF_STORAGE_TESTING is defined (see
 * firmware/test_runner/main/CMakeLists.txt) - see its definition in
 * sdf_storage.c for why cache-enrolled-user-state needs this seam. */
extern void test_sdf_storage_set_enrolled_users_save_fail_count(uint32_t count);

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
  sdf_event_router_init();
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
 * enrolled_user_bmp is set directly on the internal state (rather than via
 * a real enrollment) so the "0 users, execute immediately" bypass in
 * sdf_button_dispatch_action() is not taken and the admin-fingerprint
 * pending-action gate is actually exercised - mirrors how NUKI_PAIR's own
 * gate is only reachable once a device is claimed. */

void test_button_dispatch_ble_pairing_window_sets_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW);

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW,
                     sdf_services_state()->pending_admin_action);
}

void test_button_dispatch_ble_pairing_window_ignored_when_action_already_pending(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR,
                     sdf_services_state()->pending_admin_action);

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW);

  /* The already-pending NUKI_PAIR request must not be clobbered by the
   * later double-click. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR,
                     sdf_services_state()->pending_admin_action);
}

void test_pulse_pending_action_led_covers_all_actions(void) {
  /* Exercising every enum variant ensures no unhandled case in the switch */
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_NONE);
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_ENROLL);
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR);
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_ZB_JOIN);
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET);
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_CHANGE_PERMISSION);
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN);
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH);
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR);
  sdf_services_pulse_pending_action_led(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW);
}

void test_request_admin_action_ble_pairing_window_sets_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_services_state()->initialized = true;

  TEST_ASSERT_EQUAL(
      ESP_OK,
      sdf_services_request_admin_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW));
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW,
                    sdf_services_state()->pending_admin_action);
}

void test_bootstrap_bypass_rejected_for_unspecified_origin_on_zero_user_device(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_services_state()->initialized = true;

  /* Helper explicitly rejects unspecified origin (0) on zero-user device */
  TEST_ASSERT_EQUAL(0, sdf_services_enrolled_user_count(sdf_services_state()->enrolled_user_bmp));
  TEST_ASSERT_FALSE(sdf_services_try_bootstrap_admin_action(
      SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW, SDF_SERVICES_ADMIN_ORIGIN_UNSPECIFIED));
}

void test_bootstrap_bypass_rejected_for_remote_origin_on_zero_user_device(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_services_state()->initialized = true;

  /* Helper explicitly rejects remote origin on zero-user device */
  TEST_ASSERT_EQUAL(0, sdf_services_enrolled_user_count(sdf_services_state()->enrolled_user_bmp));
  TEST_ASSERT_FALSE(sdf_services_try_bootstrap_admin_action(
      SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW, SDF_SERVICES_ADMIN_ORIGIN_REMOTE));

  /* sdf_services_request_admin_action() sets pending action rather than bypassing */
  TEST_ASSERT_EQUAL(
      ESP_OK,
      sdf_services_request_admin_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW));
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW,
                    sdf_services_state()->pending_admin_action);
}

void test_bootstrap_bypass_clears_existing_pending_action_before_execution(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_services_state()->initialized = true;

  /* Simulate a stale or pre-existing pending action */
  sdf_services_state()->pending_admin_action = SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET;
  sdf_services_state()->pending_admin_action_start_us = 12345678LL;

  /* On a 0-user device with local physical origin, bypass executes and clears pending state */
  TEST_ASSERT_EQUAL(0, sdf_services_enrolled_user_count(sdf_services_state()->enrolled_user_bmp));
  TEST_ASSERT_TRUE(sdf_services_try_bootstrap_admin_action(
      SDF_SERVICES_ADMIN_ACTION_ENROLL, SDF_SERVICES_ADMIN_ORIGIN_LOCAL_PHYSICAL));

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NONE,
                    sdf_services_state()->pending_admin_action);
  TEST_ASSERT_EQUAL(0, sdf_services_state()->pending_admin_action_start_us);
}

static sdf_services_admin_action_t s_test_bootstrap_cb_action = SDF_SERVICES_ADMIN_ACTION_NONE;
static void *s_test_bootstrap_cb_ctx = NULL;

static void test_bootstrap_action_cb(void *ctx, sdf_services_admin_action_t action) {
  s_test_bootstrap_cb_ctx = ctx;
  s_test_bootstrap_cb_action = action;
}

void test_bootstrap_bypass_routes_non_enroll_action_to_action_cb_without_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  s_test_bootstrap_cb_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  s_test_bootstrap_cb_ctx = NULL;
  sdf_services_state()->config.admin_action_cb = test_bootstrap_action_cb;
  sdf_services_state()->config.admin_action_ctx = (void *)0xABCD;

  /* On a 0-user device, button dispatch for a non-ENROLL action (e.g. BLE_PAIRING_WINDOW or FACTORY_RESET)
   * immediately invokes admin_action_cb and leaves no pending admin action set. */
  TEST_ASSERT_EQUAL(0, sdf_services_enrolled_user_count(sdf_services_state()->enrolled_user_bmp));
  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW);

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW, s_test_bootstrap_cb_action);
  TEST_ASSERT_EQUAL_PTR((void *)0xABCD, s_test_bootstrap_cb_ctx);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NONE,
                    sdf_services_state()->pending_admin_action);

  /* Also test FACTORY_RESET */
  s_test_bootstrap_cb_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  s_test_bootstrap_cb_ctx = NULL;
  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET);

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET, s_test_bootstrap_cb_action);
  TEST_ASSERT_EQUAL_PTR((void *)0xABCD, s_test_bootstrap_cb_ctx);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NONE,
                    sdf_services_state()->pending_admin_action);
}

void test_dispatch_admin_action_remote_origin_on_zero_user_device_sets_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  s_test_bootstrap_cb_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  s_test_bootstrap_cb_ctx = NULL;
  sdf_services_state()->config.admin_action_cb = test_bootstrap_action_cb;
  sdf_services_state()->config.admin_action_ctx = (void *)0xABCD;

  /* On a 0-user device, remote origin must NOT bypass authorization; it sets pending action */
  TEST_ASSERT_EQUAL(0, sdf_services_enrolled_user_count(sdf_services_state()->enrolled_user_bmp));
  sdf_services_dispatch_admin_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW,
                                     SDF_SERVICES_ADMIN_ORIGIN_REMOTE);

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NONE, s_test_bootstrap_cb_action);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW,
                    sdf_services_state()->pending_admin_action);
}

void test_dispatch_admin_action_local_physical_origin_on_zero_user_device_bypasses_authorization(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  s_test_bootstrap_cb_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  s_test_bootstrap_cb_ctx = NULL;
  sdf_services_state()->config.admin_action_cb = test_bootstrap_action_cb;
  sdf_services_state()->config.admin_action_ctx = (void *)0xABCD;

  /* On a 0-user device, local physical origin bypasses authorization immediately */
  TEST_ASSERT_EQUAL(0, sdf_services_enrolled_user_count(sdf_services_state()->enrolled_user_bmp));
  sdf_services_dispatch_admin_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW,
                                     SDF_SERVICES_ADMIN_ORIGIN_LOCAL_PHYSICAL);

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW, s_test_bootstrap_cb_action);
  TEST_ASSERT_EQUAL_PTR((void *)0xABCD, s_test_bootstrap_cb_ctx);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NONE,
                    sdf_services_state()->pending_admin_action);
}

void test_button_dispatch_claimed_device_sets_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);

  /* Claimed device: bypass is rejected for local physical origin */
  TEST_ASSERT_FALSE(sdf_services_try_bootstrap_admin_action(
      SDF_SERVICES_ADMIN_ACTION_ENROLL, SDF_SERVICES_ADMIN_ORIGIN_LOCAL_PHYSICAL));

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_ENROLL);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_ENROLL,
                    sdf_services_state()->pending_admin_action);
}

void test_button_single_click_resolves_to_enroll_on_unclaimed_device(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  TEST_ASSERT_EQUAL(0, sdf_services_enrolled_user_count(sdf_services_state()->enrolled_user_bmp));
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_ENROLL, sdf_button_resolve_single_click_action());
}

void test_button_single_click_resolves_to_nuki_pair_on_claimed_incomplete_device(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);
  sdf_storage_nuki_clear();
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_CLAIMED_INCOMPLETE, sdf_services_get_setup_state());
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR, sdf_button_resolve_single_click_action());
}

void test_button_single_click_resolves_to_enroll_on_claimed_complete_device(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);
  uint8_t key[32] = {0x42};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_nuki_save(12345, key));
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_CLAIMED_COMPLETE, sdf_services_get_setup_state());
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_ENROLL, sdf_button_resolve_single_click_action());
  sdf_storage_nuki_clear();
}

void test_button_dispatch_factory_reset_on_claimed_device_sets_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET,
                     sdf_services_state()->pending_admin_action);
}

void test_button_press_dropped_under_backpressure_leaves_no_state(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_services_state()->pending_admin_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  sdf_services_state()->pending_admin_action_start_us = 0;

  /* If a button press is dropped (e.g. emit fails / queue full), no pending
   * action is set, no LED is pulsed, and nothing executes. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NONE,
                    sdf_services_state()->pending_admin_action);
  TEST_ASSERT_EQUAL(0, sdf_services_state()->pending_admin_action_start_us);
}

/* Enrolled-user cache: boot-race regression + dispatch-gate coverage
 * (cache-enrolled-user-state). */

/* sdf_services_init() is a one-shot per process (guarded by s_state.initialized -
 * see sdf_services.c), so this MUST be the very first call to it anywhere in
 * this test binary, or it silently becomes a no-op that skips the NVS load
 * this test exists to exercise. Registered as the very first "SDF Services
 * tests" RUN_TEST in test_runner_main.c to guarantee that - see the ordering
 * comment there. Deliberately does NOT use ensure_services_initialized(),
 * which is only safe for tests that don't care whether init() actually ran
 * the load. */
void test_sdf_services_init_loads_enrolled_users_cache_before_return(void) {
  sdf_event_router_init();
  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_erase());
    nvs_err = nvs_flash_init();
  }
  TEST_ASSERT_EQUAL(ESP_OK, nvs_err);

  uint16_t saved_bmp = 0;
  SDF_SERVICES_BMP_SET(saved_bmp, 1);
  SDF_SERVICES_BMP_SET(saved_bmp, 5);
  uint8_t saved_perm[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};
  sdf_services_perm_set(saved_perm, 1, 3);
  sdf_services_perm_set(saved_perm, 5, 1);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_save(saved_bmp, saved_perm));

  /* Deliberately not asserting sdf_services_init()'s return value here: on
   * this Linux host target sdf_services_start_tasks() (called internally by
   * sdf_services_init(), after the cache load below) fails with
   * ESP_ERR_INVALID_STATE via sdf_services_is_ready() - a pre-existing
   * ordering quirk on main (s_state.initialized isn't set true until after
   * start_tasks() returns, but start_tasks() requires it true first) that
   * predates and is out of scope for cache-enrolled-user-state, and that
   * ensure_services_initialized() above also silently tolerates by not
   * checking the return value. The NVS cache load below happens earlier in
   * sdf_services_init(), synchronously and unconditionally, before that
   * later failure path, so it's unaffected either way. */
  sdf_services_config_t cfg;
  sdf_services_get_default_config(&cfg);
  sdf_services_init(&cfg);

  /* No task run, no delay - this is exactly what a task reading s_state
   * immediately after sdf_services_init() returns would see. */
  TEST_ASSERT_EQUAL(saved_bmp, sdf_services_state()->enrolled_user_bmp);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(saved_perm, sdf_services_state()->enrolled_perm_packed,
                                SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);
}

/* Must run immediately after the init test above, with no intervening
 * sdf_services_reset_state() call, so it exercises sdf_button_dispatch_action()
 * against the exact cache state sdf_services_init() just loaded from NVS -
 * i.e. as the very first admin action dispatched after boot. Before this
 * change, s_state.enrolled_user_count was zeroed unconditionally in init()
 * and only became correct once a slow sensor query finished, so this exact
 * scenario used to take the "0 users, execute immediately" bypass. */
void test_button_dispatch_never_bypasses_gate_as_first_action_after_init(void) {
  TEST_ASSERT_TRUE(sdf_services_enrolled_user_count(sdf_services_state()->enrolled_user_bmp) > 0);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NONE,
                    sdf_services_state()->pending_admin_action);

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR);

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR,
                    sdf_services_state()->pending_admin_action);
}

/* sdf_services_enrolled_user_count() popcount correctness */

void test_enrolled_user_count_zero_bitmap(void) {
  TEST_ASSERT_EQUAL(0, sdf_services_enrolled_user_count(0));
}

void test_enrolled_user_count_single_bit(void) {
  uint16_t bmp = 0;
  SDF_SERVICES_BMP_SET(bmp, 7);
  TEST_ASSERT_EQUAL(1, sdf_services_enrolled_user_count(bmp));
}

void test_enrolled_user_count_multiple_bits(void) {
  uint16_t bmp = 0;
  SDF_SERVICES_BMP_SET(bmp, 1);
  SDF_SERVICES_BMP_SET(bmp, 2);
  SDF_SERVICES_BMP_SET(bmp, 10);
  TEST_ASSERT_EQUAL(3, sdf_services_enrolled_user_count(bmp));
}

void test_enrolled_user_count_all_ten_users(void) {
  uint16_t bmp = 0;
  for (uint16_t id = 1; id <= 10; id++) {
    SDF_SERVICES_BMP_SET(bmp, id);
  }
  TEST_ASSERT_EQUAL(10, sdf_services_enrolled_user_count(bmp));
}

void test_enrolled_user_count_updates_after_clear(void) {
  uint16_t bmp = 0;
  SDF_SERVICES_BMP_SET(bmp, 3);
  SDF_SERVICES_BMP_SET(bmp, 4);
  SDF_SERVICES_BMP_CLEAR(bmp, 3);
  TEST_ASSERT_EQUAL(1, sdf_services_enrolled_user_count(bmp));
}

/* sdf_services_persist_enrolled_users_locked(): the shared cache-to-NVS
 * write helper every mutation path (enroll completion, delete_user,
 * clear_all_users, change_user_permission) funnels through after updating
 * s_state's cache fields and before reporting success (tasks 5.1-5.5). Its
 * caller-facing contract - "write s_state's current cache fields to NVS,
 * retrying with backoff, and report success/failure" - is exercised
 * directly here rather than through each of the four public mutation
 * functions: those all also require a successful fp_*() sensor round trip
 * before they ever reach this helper, and the Linux host target's mock UART
 * never produces a valid frame (see test_fingerprint_owner_task.c and the
 * fp-io-owner-task change this depends on), so fp_delete_user(),
 * fp_delete_all_users(), fp_change_user_permission() and fp_enroll_step()
 * cannot be made to succeed in a host test. That pre-existing, documented
 * constraint is why the sensor-rollback and red-LED wrapper logic around
 * each of those four call sites (e.g. sdf_services_enroll.c's
 * SDF_ENROLL_ACT_COMPLETE branch) is exercised on hardware (see tasks.md
 * section 8) rather than here. */

void test_persist_enrolled_users_locked_writes_current_cache_to_nvs(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  sdf_services_state_t *s = sdf_services_state();
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 2);
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 9);
  sdf_services_perm_set(s->enrolled_perm_packed, 2, 3);
  sdf_services_perm_set(s->enrolled_perm_packed, 9, 2);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_persist_enrolled_users_locked());

  uint16_t loaded_bmp = 0;
  uint8_t loaded_perm[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_load(&loaded_bmp, loaded_perm));
  TEST_ASSERT_EQUAL(s->enrolled_user_bmp, loaded_bmp);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(s->enrolled_perm_packed, loaded_perm,
                                SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);
}

/* Mirrors the retry count each mutation path relies on
 * (SDF_SERVICES_PERSIST_RETRY_COUNT = 3 in sdf_services.c): a persistent
 * failure (all 3 attempts fail) must be reported to the caller as failure,
 * without corrupting the in-RAM cache fields the caller is responsible for
 * rolling back. */
void test_persist_enrolled_users_locked_fails_after_exhausting_retries(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  sdf_services_state_t *s = sdf_services_state();
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 4);
  sdf_services_perm_set(s->enrolled_perm_packed, 4, 1);
  uint16_t bmp_before = s->enrolled_user_bmp;
  uint8_t perm_before[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN];
  memcpy(perm_before, s->enrolled_perm_packed, sizeof(perm_before));

  test_sdf_storage_set_enrolled_users_save_fail_count(3);
  TEST_ASSERT_EQUAL(ESP_FAIL, sdf_services_persist_enrolled_users_locked());

  /* The helper itself doesn't touch the cache on failure - only its callers
   * decide whether/how to roll back - so it must still hold what the caller
   * asked it to persist. */
  TEST_ASSERT_EQUAL(bmp_before, s->enrolled_user_bmp);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(perm_before, s->enrolled_perm_packed,
                                SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);

  /* One fewer failure than the retry count still succeeds on the final
   * attempt - confirms the fail-count fixture itself, and that a transient
   * (not persistent) failure doesn't cause a false failure report. */
  test_sdf_storage_set_enrolled_users_save_fail_count(2);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_persist_enrolled_users_locked());
}

void test_button_init_deinit_stop_start_cycle(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_button_init());
  TEST_ASSERT_EQUAL(ESP_OK, sdf_button_deinit());
  TEST_ASSERT_EQUAL(ESP_OK, sdf_button_init());
  TEST_ASSERT_EQUAL(ESP_OK, sdf_button_deinit());
}

void test_task_wake_helpers_safe_when_idle(void) {
  /* Task wake helpers can be called anytime (e.g. during stop_tasks or admin action request) */
  sdf_enroll_task_wake();
  sdf_admin_task_wake();
}

void test_sdf_services_start_stop_start_tasks_cycle(void) {
  ensure_services_initialized();
  esp_err_t err = sdf_services_stop_tasks();
  TEST_ASSERT_EQUAL(ESP_OK, err);
  TEST_ASSERT_NULL(sdf_services_state()->match_task);
  TEST_ASSERT_NULL(sdf_services_state()->enroll_task);
  TEST_ASSERT_NULL(sdf_services_state()->admin_task);

  err = sdf_services_start_tasks();
  TEST_ASSERT_EQUAL(ESP_OK, err);
  TEST_ASSERT_NOT_NULL(sdf_services_state()->match_task);
  TEST_ASSERT_NOT_NULL(sdf_services_state()->enroll_task);
  TEST_ASSERT_NOT_NULL(sdf_services_state()->admin_task);

  vTaskDelay(pdMS_TO_TICKS(50));

  TaskHandle_t match_task_1 = sdf_services_state()->match_task;
  TaskHandle_t enroll_task_1 = sdf_services_state()->enroll_task;
  TaskHandle_t admin_task_1 = sdf_services_state()->admin_task;
  TEST_ASSERT_TRUE(sdf_platform_time_wdt_is_registered(match_task_1));
  TEST_ASSERT_TRUE(sdf_platform_time_wdt_is_registered(enroll_task_1));
  TEST_ASSERT_TRUE(sdf_platform_time_wdt_is_registered(admin_task_1));

  err = sdf_services_stop_tasks();
  TEST_ASSERT_EQUAL(ESP_OK, err);
  TEST_ASSERT_NULL(sdf_services_state()->match_task);
  TEST_ASSERT_NULL(sdf_services_state()->enroll_task);
  TEST_ASSERT_NULL(sdf_services_state()->admin_task);

  TEST_ASSERT_FALSE(sdf_platform_time_wdt_is_registered(match_task_1));
  TEST_ASSERT_FALSE(sdf_platform_time_wdt_is_registered(enroll_task_1));
  TEST_ASSERT_FALSE(sdf_platform_time_wdt_is_registered(admin_task_1));

  err = sdf_services_start_tasks();
  TEST_ASSERT_EQUAL(ESP_OK, err);
  TEST_ASSERT_NOT_NULL(sdf_services_state()->match_task);
  TEST_ASSERT_NOT_NULL(sdf_services_state()->enroll_task);
  TEST_ASSERT_NOT_NULL(sdf_services_state()->admin_task);

  vTaskDelay(pdMS_TO_TICKS(50));

  TaskHandle_t match_task_2 = sdf_services_state()->match_task;
  TaskHandle_t enroll_task_2 = sdf_services_state()->enroll_task;
  TaskHandle_t admin_task_2 = sdf_services_state()->admin_task;
  TEST_ASSERT_TRUE(sdf_platform_time_wdt_is_registered(match_task_2));
  TEST_ASSERT_TRUE(sdf_platform_time_wdt_is_registered(enroll_task_2));
  TEST_ASSERT_TRUE(sdf_platform_time_wdt_is_registered(admin_task_2));
}
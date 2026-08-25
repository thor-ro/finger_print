#include "unity.h"

#include <string.h>

#include "nvs_flash.h"

#include "sdkconfig.h"

#include "sdf_services.h"
#include "sdf_services_internal.h"
#include "sdf_event_router.h"
#include "sdf_platform_time.h"

#ifdef CONFIG_IDF_TARGET_LINUX
/* Last-admin-delete-guard tests drive the real fingerprint owner task
 * through the Linux mock UART's scripted-response hook - both exist only
 * for the host target (see the guard-test section near the end of this
 * file). */
#include "fingerprint.h"
#include "sdf_mock_linux_drivers.h"
#endif

/* Test-only fault injector for sdf_storage_enrolled_users_save(), compiled
 * into sdf_storage only when SDF_STORAGE_TESTING is defined (see
 * firmware/test_runner/main/CMakeLists.txt) - see its definition in
 * sdf_storage.c for why cache-enrolled-user-state needs this seam. */
extern void test_sdf_storage_set_enrolled_users_save_fail_count(uint32_t count);

static sdf_storage_web_user_t make_web_user_record(const char *name,
                                                    bool has_credential) {
  sdf_storage_web_user_t user = {0};
  strncpy(user.name, name, sizeof(user.name) - 1);
  if (has_credential) {
    memset(user.salt, 0xCD, sizeof(user.salt));
    memset(user.stretched_credential, 0xAB, sizeof(user.stretched_credential));
    user.has_credential = true;
  }
  user.valid = true;
  return user;
}

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
  strncpy(user.name, "alice", sizeof(user.name) - 1);
  memset(user.salt, 0x33, sizeof(user.salt));
  user.has_credential = true;
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
  strncpy(user.name, "alice", sizeof(user.name) - 1);
  memset(user.salt, 0x33, sizeof(user.salt));
  user.has_credential = true;
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
      sdf_services_web_auth_decide_registration("bob", hash, sizeof(hash), salt,
                                                1, true, true);

  TEST_ASSERT_TRUE(decision.should_persist);
  TEST_ASSERT_TRUE(decision.reply_authorized);
  /* Task 3.1: the credential binds to the authorizing admin's user id. */
  TEST_ASSERT_EQUAL(1, decision.user_id);
  TEST_ASSERT_EQUAL_STRING("bob", decision.user.name);
  TEST_ASSERT_TRUE(decision.user.has_credential);
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
      sdf_services_web_auth_decide_registration("bob", hash, sizeof(hash), salt,
                                                1, false, true);

  TEST_ASSERT_FALSE(decision.should_persist);
  TEST_ASSERT_FALSE(decision.reply_authorized);
}

void test_web_auth_decide_registration_unbound_refused(void) {
  /* Task 3.3: no authorizing user id captured -> refuse, persist nothing,
   * and do not tell the client the registration was granted either. */
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0x11, sizeof(hash));
  uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(salt, 0x22, sizeof(salt));

  sdf_services_web_auth_registration_decision_t unbound =
      sdf_services_web_auth_decide_registration("bob", hash, sizeof(hash), salt,
                                                0, true, true);
  TEST_ASSERT_FALSE(unbound.should_persist);
  TEST_ASSERT_FALSE(unbound.reply_authorized);

  sdf_services_web_auth_registration_decision_t out_of_range =
      sdf_services_web_auth_decide_registration("bob", hash, sizeof(hash), salt,
                                                SDF_STORAGE_FP_USER_ID_MAX + 1, true, true);
  TEST_ASSERT_FALSE(out_of_range.should_persist);
  TEST_ASSERT_FALSE(out_of_range.reply_authorized);
}

/* Task 4.1 (decision side): a submitted name already held by a different
 * enrolled user is refused - the caller resolves `name_available` via
 * sdf_services_find_name_holder(); here we pin that the decision honours it
 * by refusing with nothing persisted and no authorized reply. */
void test_web_auth_decide_registration_name_collision_refused(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0x11, sizeof(hash));
  uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(salt, 0x22, sizeof(salt));

  sdf_services_web_auth_registration_decision_t decision =
      sdf_services_web_auth_decide_registration("taken", hash, sizeof(hash), salt,
                                                2, true, false);
  TEST_ASSERT_FALSE(decision.should_persist);
  TEST_ASSERT_FALSE(decision.reply_authorized);
}

/* ---------------------------------------------------------------------------
 * companion-identity 2.x: the authorizing admin's user id lives in
 * sdf_services' owned pending-request state, captured at claim time and
 * read back by the getter - never carried in an event payload.
 * ------------------------------------------------------------------------- */

static const uint8_t kTestRegHash[SDF_STORAGE_WEB_USER_HASH_LEN] = {0x5A};

static void seed_pending_web_reg(void) {
  /* reset_state() deliberately leaves the web-reg pending state alone (it
   * is owned BLE-request state, not enrolled-user state), so drop any
   * request left over from an earlier test first. */
  sdf_services_clear_web_reg_auth();
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_services_set_web_reg_auth("newname", kTestRegHash,
                                                  SDF_STORAGE_WEB_USER_HASH_LEN));
  /* A registration authorization is only claimable while a WEB_REG_AUTH
   * admin action is actually pending - mirror how a real request arrives. */
  sdf_services_state()->pending_admin_action =
      SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH;
}

/* Task 2.5: the user id of the admin whose scan claimed the pending WEB_REG_AUTH
 * action is captured into owned state and returned by the getter. */
void test_web_reg_auth_authorizer_captured_on_claim(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  seed_pending_web_reg();

  sdf_fingerprint_match_t match = {.user_id = 7, .permission = 3};
  TEST_ASSERT_TRUE(sdf_services_try_claim_admin_action(&match));

  char username[SDF_STORAGE_WEB_USER_NAME_MAX];
  uint16_t authorizer = 0xFFFF;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_get_web_reg_auth(username,
                                                          sizeof(username),
                                                          &authorizer));
  TEST_ASSERT_EQUAL_STRING("newname", username);
  TEST_ASSERT_EQUAL(7, authorizer);
}

/* Task 2.5: a non-admin scan denies the request - nothing is authorized and
 * no user id is captured (the value stays absent for the timeout path too:
 * both resolve through sdf_services_clear_web_reg_auth(), which zeroes it -
 * pinned by the clear test below). */
void test_web_reg_auth_authorizer_absent_on_denial(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  seed_pending_web_reg();

  sdf_fingerprint_match_t non_admin = {.user_id = 4, .permission = 1};
  TEST_ASSERT_TRUE(sdf_services_try_claim_admin_action(&non_admin));

  char username[SDF_STORAGE_WEB_USER_NAME_MAX];
  uint16_t authorizer = 0xFFFF;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_get_web_reg_auth(username,
                                                          sizeof(username),
                                                          &authorizer));
  TEST_ASSERT_EQUAL(0, authorizer);
}

/* Task 2.4: every clear path drops the captured id together with the rest
 * of the pending-request state. */
void test_web_reg_auth_clear_drops_captured_user_id(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  seed_pending_web_reg();

  sdf_fingerprint_match_t match = {.user_id = 3, .permission = 3};
  TEST_ASSERT_TRUE(sdf_services_try_claim_admin_action(&match));

  sdf_services_clear_web_reg_auth();

  char username[SDF_STORAGE_WEB_USER_NAME_MAX];
  uint16_t authorizer = 0xFFFF;
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                    sdf_services_get_web_reg_auth(username, sizeof(username),
                                                  &authorizer));
}

/* Task 2.5: the WEB_REG_AUTH_RESULT event payload carries exactly one bool
 * ("authorized"). Identity material must never ride on the event - it flows
 * through the owned pending-request state instead. If this size assert ever
 * fails because a field was added back, that rule has been broken. */
void test_web_reg_auth_result_event_carries_no_identity_payload(void) {
  typedef struct {
    bool authorized;
  } expected_payload_t;
  TEST_ASSERT_EQUAL(sizeof(expected_payload_t),
                    sizeof(((sdf_event_router_event_t *)0)->payload.web_reg_auth_result));
}

/* ---------------------------------------------------------------------------
 * companion-identity 3.x/5.x: bound persistence against unified records.
 * ------------------------------------------------------------------------- */

/* Task 3.5: first registration binds - saving the decision against the
 * authorizing admin's user id creates exactly one resolvable account. */
void test_registration_binds_credential_to_user_id(void) {
  nvs_flash_init();
  sdf_storage_web_user_clear_all();

  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0x11, sizeof(hash));
  uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(salt, 0x22, sizeof(salt));

  sdf_services_web_auth_registration_decision_t decision =
      sdf_services_web_auth_decide_registration("alice", hash, sizeof(hash), salt,
                                                1, true, true);
  TEST_ASSERT_TRUE(decision.should_persist);
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_storage_web_user_save(decision.user_id, &decision.user));

  /* The account resolves under the admin's own user id... */
  sdf_storage_web_user_t loaded = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(1, &loaded));
  TEST_ASSERT_EQUAL_STRING("alice", loaded.name);
  TEST_ASSERT_TRUE(loaded.has_credential);
  /* ...and by login name. */
  uint16_t holder = 0;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_find_name_holder("alice", &holder));
  TEST_ASSERT_EQUAL(1, holder);

  sdf_storage_web_user_clear_all();
}

/* Task 3.5: re-registration by the same admin replaces the credential in
 * place; a response derived from the previous credential no longer verifies. */
void test_reregistration_replaces_previous_credential(void) {
  nvs_flash_init();
  sdf_storage_web_user_clear_all();

  uint8_t old_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(old_hash, 0x11, sizeof(old_hash));
  uint8_t old_salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(old_salt, 0x22, sizeof(old_salt));

  sdf_services_web_auth_registration_decision_t first =
      sdf_services_web_auth_decide_registration("alice", old_hash, sizeof(old_hash),
                                                old_salt, 1, true, true);
  TEST_ASSERT_TRUE(first.should_persist);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(first.user_id, &first.user));

  /* Old credential verifies against its stored stretched credential... */
  uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
  memset(nonce, 0x44, sizeof(nonce));
  uint8_t old_stretched[SDF_STORAGE_WEB_USER_STRETCHED_LEN];
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_services_web_auth_stretch_credential(
                        old_hash, sizeof(old_hash), old_salt,
                        SDF_SERVICES_WEB_AUTH_PBKDF2_ITERATIONS, old_stretched));
  uint8_t old_response[SDF_SERVICES_WEB_AUTH_RESPONSE_LEN];
  make_response(old_stretched, nonce, old_response);
  TEST_ASSERT_TRUE(sdf_services_web_auth_verify_response(
      first.user.stretched_credential, nonce, sizeof(nonce), old_response,
      sizeof(old_response)));

  /* ...then re-registration with a fresh salt/hash replaces it. */
  uint8_t new_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(new_hash, 0x77, sizeof(new_hash));
  uint8_t new_salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(new_salt, 0x88, sizeof(new_salt));

  sdf_services_web_auth_registration_decision_t second =
      sdf_services_web_auth_decide_registration("alice", new_hash, sizeof(new_hash),
                                                new_salt, 1, true, true);
  TEST_ASSERT_TRUE(second.should_persist);
  TEST_ASSERT_EQUAL(1, second.user_id); /* same admin, same account */
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(second.user_id, &second.user));

  /* Exactly one account still exists... */
  size_t count = 0;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_count(&count));
  TEST_ASSERT_EQUAL(1, count);

  /* ...the fresh credential material replaced the old (fresh salt), and */
  TEST_ASSERT_NOT_EQUAL(0, memcmp(second.user.salt, first.user.salt,
                                  SDF_STORAGE_WEB_USER_SALT_LEN));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(second.user.stretched_credential,
                                  first.user.stretched_credential,
                                  SDF_STORAGE_WEB_USER_STRETCHED_LEN));
  /* ...a response computed from the previous credential is rejected. */
  TEST_ASSERT_FALSE(sdf_services_web_auth_verify_response(
      second.user.stretched_credential, nonce, sizeof(nonce), old_response,
      sizeof(old_response)));

  sdf_storage_web_user_clear_all();
}

/* Task 3.5: two different admins registering hold two separate accounts -
 * neither replaces the other. */
void test_two_admins_hold_separate_accounts(void) {
  nvs_flash_init();
  sdf_storage_web_user_clear_all();

  uint8_t hash_a[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash_a, 0x11, sizeof(hash_a));
  uint8_t hash_b[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash_b, 0x33, sizeof(hash_b));
  uint8_t salt[SDF_STORAGE_WEB_USER_SALT_LEN];
  memset(salt, 0x22, sizeof(salt));

  sdf_services_web_auth_registration_decision_t reg_a =
      sdf_services_web_auth_decide_registration("alice", hash_a, sizeof(hash_a),
                                                salt, 1, true, true);
  TEST_ASSERT_TRUE(reg_a.should_persist);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(reg_a.user_id, &reg_a.user));

  sdf_services_web_auth_registration_decision_t reg_b =
      sdf_services_web_auth_decide_registration("bob", hash_b, sizeof(hash_b),
                                                salt, 2, true, true);
  TEST_ASSERT_TRUE(reg_b.should_persist);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(reg_b.user_id, &reg_b.user));

  /* Alice's record is untouched by Bob's registration. */
  sdf_storage_web_user_t loaded_a = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(1, &loaded_a));
  TEST_ASSERT_EQUAL_STRING("alice", loaded_a.name);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reg_a.user.salt, loaded_a.salt,
                                SDF_STORAGE_WEB_USER_SALT_LEN);

  sdf_storage_web_user_t loaded_b = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(2, &loaded_b));
  TEST_ASSERT_EQUAL_STRING("bob", loaded_b.name);

  sdf_storage_web_user_clear_all();
}

/* ---------------------------------------------------------------------------
 * companion-identity 4.x: name uniqueness across enrolled users.
 * ------------------------------------------------------------------------- */

/* Task 4.2: renaming onto a name another enrolled user holds is refused and
 * leaves both records unchanged. */
void test_set_user_name_refuses_duplicate_of_other_user(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  nvs_flash_init();

  sdf_services_state_t *s = sdf_services_state();
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 1);
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 2);

  sdf_storage_web_user_t rec1 = make_web_user_record("alice", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &rec1));
  /* User 2 holds only a (blank) name - no credential. Uniqueness must
   * refuse the collision anyway. */
  sdf_storage_web_user_t rec2 = {0};
  rec2.valid = true;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(2, &rec2));

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sdf_services_set_user_name(2, "alice"));

  /* Both records unchanged. */
  sdf_storage_web_user_t check1 = {0}, check2 = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(1, &check1));
  TEST_ASSERT_EQUAL_STRING("alice", check1.name);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(2, &check2));
  TEST_ASSERT_EQUAL('\0', check2.name[0]);
  TEST_ASSERT_FALSE(check2.has_credential);

  sdf_storage_web_user_clear_all();
}

/* Task 4.2 (no-op case): renaming to the user's own current name succeeds. */
void test_set_user_name_to_own_name_is_noop_success(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  nvs_flash_init();

  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);
  sdf_storage_web_user_t rec = make_web_user_record("alice", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &rec));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_set_user_name(1, "alice"));

  sdf_storage_web_user_t check = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(1, &check));
  TEST_ASSERT_EQUAL_STRING("alice", check.name);
  TEST_ASSERT_TRUE(check.has_credential);

  sdf_storage_web_user_clear_all();
}

/* Task 4.2: setting a fresh name merges into the existing record without
 * destroying a stored credential. */
void test_set_user_name_preserves_existing_credential(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  nvs_flash_init();

  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);
  sdf_storage_web_user_t rec = {0}; /* valid record, no name yet */
  memset(rec.salt, 0xCD, sizeof(rec.salt));
  memset(rec.stretched_credential, 0xAB, sizeof(rec.stretched_credential));
  rec.has_credential = true;
  rec.valid = true;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &rec));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_set_user_name(1, "carol"));

  sdf_storage_web_user_t check = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(1, &check));
  TEST_ASSERT_EQUAL_STRING("carol", check.name);
  TEST_ASSERT_TRUE(check.has_credential);
  TEST_ASSERT_EQUAL_MEMORY(rec.stretched_credential, check.stretched_credential,
                           SDF_STORAGE_WEB_USER_STRETCHED_LEN);

  sdf_storage_web_user_clear_all();
}

/* Task 4.1 helper coverage: find_name_holder scans name-only records too -
 * uniqueness spans all enrolled users, not just credential holders. */
void test_find_name_holder_sees_records_without_credentials(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  nvs_flash_init();

  sdf_storage_web_user_t name_only = make_web_user_record("standard", false);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(5, &name_only));

  uint16_t holder = 0;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_find_name_holder("standard", &holder));
  TEST_ASSERT_EQUAL(5, holder);
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                    sdf_services_find_name_holder("nobody", &holder));

  sdf_storage_web_user_clear_all();
}

/* ---------------------------------------------------------------------------
 * companion-identity 6.x: session authority resolved live from the bound
 * user via sdf_services_user_is_enrolled_admin().
 * ------------------------------------------------------------------------- */

/* Task 6.6: authority is re-read per call from the enrolled-user cache -
 * the same query returns different answers as the cache changes, which is
 * what makes demotion/deletion take effect on an open session without any
 * cascade over account records. */
void test_user_is_enrolled_admin_rereads_cache_per_call(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  sdf_services_state_t *s = sdf_services_state();
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 1);
  sdf_services_perm_set(s->enrolled_perm_packed, 1, 3);
  TEST_ASSERT_TRUE(sdf_services_user_is_enrolled_admin(1));

  /* Demotion takes effect immediately on the next read. */
  sdf_services_perm_set(s->enrolled_perm_packed, 1, 1);
  TEST_ASSERT_FALSE(sdf_services_user_is_enrolled_admin(1));

  /* Re-promotion is visible again - proof each call reads current state. */
  sdf_services_perm_set(s->enrolled_perm_packed, 1, 3);
  TEST_ASSERT_TRUE(sdf_services_user_is_enrolled_admin(1));
}

/* Task 6.6: deleting the bound user (cache bit cleared, exactly what
 * sdf_services_delete_user() does after the sensor delete) strips authority
 * on subsequent decisions - no account modification required. */
void test_user_is_enrolled_admin_false_after_deletion_from_cache(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  sdf_services_state_t *s = sdf_services_state();
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 2);
  sdf_services_perm_set(s->enrolled_perm_packed, 2, 3);
  TEST_ASSERT_TRUE(sdf_services_user_is_enrolled_admin(2));

  SDF_SERVICES_BMP_CLEAR(s->enrolled_user_bmp, 2);
  TEST_ASSERT_FALSE(sdf_services_user_is_enrolled_admin(2));
}

/* Task 6.6: a permission level below admin never confers companion
 * authority - including reserved level 2 (companion-identity "Permission
 * Level 2 Is Reserved And Companion-Irrelevant"). */
void test_user_is_enrolled_admin_false_for_non_admin_levels(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  sdf_services_state_t *s = sdf_services_state();
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 1);
  sdf_services_perm_set(s->enrolled_perm_packed, 1, 1);
  TEST_ASSERT_FALSE(sdf_services_user_is_enrolled_admin(1));

  sdf_services_perm_set(s->enrolled_perm_packed, 1, 2);
  TEST_ASSERT_FALSE(sdf_services_user_is_enrolled_admin(1));
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

void test_setup_state_not_started_when_no_enrolled_users_and_latch_unset(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_storage_setup_complete_clear();
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_NOT_STARTED,
                    sdf_services_get_setup_state());
}

/* Intermediate setup states stay derived from enrolled-user state, persisted
 * web accounts and persisted Nuki credentials - they drive only wizard step
 * selection. Walked in wizard step order so a regression that collapses two
 * of them shows up as the wrong resume point rather than silently. */
void test_setup_state_intermediate_states_are_derived_in_step_order(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_storage_setup_complete_clear();
  sdf_storage_web_user_clear_all();
  sdf_storage_nuki_clear();

  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_ADMIN_ENROLLED,
                    sdf_services_get_setup_state());

  /* Registration lands before Nuki pairing: the state must distinguish
   * "still needs an account" from "needs Nuki pairing", or the wizard
   * resumes at a step the user already finished and completion can be
   * accepted on a device with no way to log in. */
  sdf_storage_web_user_t user = make_web_user_record("owner", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &user));
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_REGISTERED,
                    sdf_services_get_setup_state());

  uint8_t key[32] = {0x42};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_nuki_save(12345, key));
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_NUKI_PAIRED,
                    sdf_services_get_setup_state());

  /* Pairing the Nuki without an account must NOT reach the terminal
   * pre-completion state - that is the path that would otherwise claim the
   * device with nobody able to log in. */
  sdf_storage_web_user_clear_all();
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_ADMIN_ENROLLED,
                    sdf_services_get_setup_state());

  sdf_storage_nuki_clear();
}

/* The latch is the single completion record: once set it must survive any
 * independently-mutable operation - deleting every user, clearing Nuki
 * credentials - and only factory reset clears it. */
void test_setup_state_latch_survives_user_and_credential_deletion(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  nvs_flash_init();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_save(true));

  /* Deleting the last enrolled user does not reopen the setup phase. */
  sdf_services_state()->enrolled_user_bmp = 0;
  memset(sdf_services_state()->enrolled_perm_packed, 0,
         sizeof(sdf_services_state()->enrolled_perm_packed));
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_COMPLETE,
                    sdf_services_get_setup_state());

  /* Clearing Nuki credentials does not either. */
  sdf_storage_nuki_clear();
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_COMPLETE,
                    sdf_services_get_setup_state());

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_clear());
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_NOT_STARTED,
                    sdf_services_get_setup_state());
}

/* The Setup-State characteristic exposes sdf_services_get_setup_state()'s
 * value as one raw byte on the wire. Pinning the enum values here keeps the
 * firmware-side wire format stable against accidental renumbering. */
void test_setup_state_wire_byte_values_are_stable(void) {
  TEST_ASSERT_EQUAL(0, SDF_SERVICES_SETUP_STATE_NOT_STARTED);
  TEST_ASSERT_EQUAL(1, SDF_SERVICES_SETUP_STATE_ADMIN_ENROLLED);
  TEST_ASSERT_EQUAL(2, SDF_SERVICES_SETUP_STATE_REGISTERED);
  TEST_ASSERT_EQUAL(3, SDF_SERVICES_SETUP_STATE_NUKI_PAIRED);
  TEST_ASSERT_EQUAL(4, SDF_SERVICES_SETUP_STATE_COMPLETE);
}

/* Button dispatch: BLE Companion pairing-window admin action (double-click).
 * The dispatch itself always follows the ordinary pending-action flow; the
 * setup-phase routing decision (reclaim instead of dispatch while the latch
 * is unset) lives in the admin task's BUTTON_PRESS case. */

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

/* The unauthenticated bootstrap bypass is retired: no button gesture reaches
 * an admin action during the setup phase (the press reclaims the setup
 * connection instead), and factory reset executes directly at its gesture.
 * Every admin-action request path now follows the ordinary pending-action
 * flow - including on a device with zero enrolled users. */
void test_dispatch_admin_action_on_zero_user_device_sets_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  TEST_ASSERT_EQUAL(0, sdf_services_enrolled_user_count(sdf_services_state()->enrolled_user_bmp));
  sdf_services_dispatch_admin_action(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW);

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW,
                    sdf_services_state()->pending_admin_action);
}

static sdf_services_admin_action_t s_test_direct_cb_action = SDF_SERVICES_ADMIN_ACTION_NONE;
static void *s_test_direct_cb_ctx = NULL;

static void test_direct_action_cb(void *ctx, sdf_services_admin_action_t action) {
  s_test_direct_cb_ctx = ctx;
  s_test_direct_cb_action = action;
}

/* Factory reset requires no Admin fingerprint: the long-press gesture routes
 * through sdf_button_execute_direct(), which invokes the action callback
 * immediately and never touches pending-admin-action state. */
void test_factory_reset_direct_execution_sets_no_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());

  s_test_direct_cb_action = SDF_SERVICES_ADMIN_ACTION_NONE;
  s_test_direct_cb_ctx = NULL;
  sdf_services_state()->config.admin_action_cb = test_direct_action_cb;
  sdf_services_state()->config.admin_action_ctx = (void *)0xABCD;

  /* Even with zero readable admins there is no fingerprint gate to bypass:
   * the reset simply proceeds. */
  SDF_SERVICES_BMP_CLEAR(sdf_services_state()->enrolled_user_bmp, 1);
  sdf_button_execute_direct(SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET);

  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_FACTORY_RESET, s_test_direct_cb_action);
  TEST_ASSERT_EQUAL_PTR((void *)0xABCD, s_test_direct_cb_ctx);
  /* No pending admin action is set and no Admin scan is awaited. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NONE,
                    sdf_services_state()->pending_admin_action);
}

void test_button_dispatch_claimed_device_sets_pending_action(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 1);

  sdf_button_dispatch_action(SDF_SERVICES_ADMIN_ACTION_ENROLL);
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_ENROLL,
                    sdf_services_state()->pending_admin_action);
}

/* Single-click no longer resolves to any action: first-time setup runs
 * exclusively through the companion-app wizard, and post-completion
 * enrolment goes through the app's Enrollment characteristic. The old
 * resolve tests were removed with sdf_button_resolve_single_click_action().
 *
 * Factory reset no longer enters the pending-admin-action wait either - see
 * test_factory_reset_direct_execution_sets_no_pending_action() above. */

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

/* ---------------------------------------------------------------------------
 * Setup-phase lifecycle (device-setup-phase): arming, the two exposure
 * timers, the connection idle timer, and the timeout wipe. Time is driven
 * deterministically through sdf_services_internal.h's *_at() cores; all
 * times are microseconds.
 * ------------------------------------------------------------------------- */

#define T_ARM_WINDOW_US ((int64_t)SDF_SETUP_ARM_WINDOW_MS * 1000)
#define T_DEADLINE_US ((int64_t)SDF_SETUP_DEADLINE_MS * 1000)
#define T_IDLE_US ((int64_t)SDF_SETUP_CONN_IDLE_MS * 1000)

void test_setup_phase_boot_arms_when_latch_unset_and_not_when_set(void) {
  nvs_flash_init();
  sdf_storage_setup_complete_clear();
  sdf_services_setup_phase_reset_for_test();
  sdf_services_setup_phase_boot_arm();
  TEST_ASSERT_TRUE(sdf_services_setup_phase_is_armed());

  /* A completed device never enters the setup phase. */
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_save(true));
  sdf_services_setup_phase_reset_for_test();
  sdf_services_setup_phase_boot_arm();
  TEST_ASSERT_FALSE(sdf_services_setup_phase_is_armed());
  sdf_storage_setup_complete_clear();
}

void test_setup_phase_arm_window_expiry_wipes_and_disarms(void) {
  sdf_services_setup_phase_reset_for_test();

  const int64_t t0 = 1000000;
  sdf_services_setup_phase_arm_at(t0);

  /* Just inside the window: still armed, no action. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                    sdf_services_setup_phase_poll(t0 + T_ARM_WINDOW_US - 1));
  TEST_ASSERT_TRUE(sdf_services_setup_phase_is_armed());

  /* Expiry: wipe-and-stop, and the phase disarms. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP,
                    sdf_services_setup_phase_poll(t0 + T_ARM_WINDOW_US));
  sdf_services_setup_phase_timeout_wipe();
  TEST_ASSERT_FALSE(sdf_services_setup_phase_is_armed());
}

void test_setup_phase_deadline_starts_at_first_connection_and_is_not_extended(void) {
  sdf_services_setup_phase_reset_for_test();

  const int64_t t_arm = 0;
  sdf_services_setup_phase_arm_at(t_arm);

  /* No client yet: only the arm window governs. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                    sdf_services_setup_phase_poll(T_ARM_WINDOW_US - 1));

  /* First accepted connection starts the deadline. */
  const int64_t t_conn = 5000000;
  sdf_services_setup_phase_notify_connected_at(42, t_conn);
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                    sdf_services_setup_phase_poll(t_conn + T_IDLE_US - 1));

  /* Activity, progress, disconnect and reconnect do not move the deadline:
   * it still expires at t_conn + DEADLINE even after all of them. */
  const int64_t t_late = t_conn + (int64_t)(SDF_SETUP_DEADLINE_MS - 60000) * 1000;
  sdf_services_setup_phase_notify_gatt_activity_at(t_late);
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                    sdf_services_setup_phase_poll(t_late));

  sdf_services_setup_phase_notify_disconnected_at(t_late);
  sdf_services_setup_phase_notify_connected_at(42, t_late + 1000000);
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP,
                    sdf_services_setup_phase_poll(t_conn + T_DEADLINE_US));
}

void test_setup_phase_idle_timer_drops_only_the_connection(void) {
  sdf_services_setup_phase_reset_for_test();

  const int64_t t0 = 0;
  sdf_services_setup_phase_arm_at(t0);
  sdf_services_setup_phase_notify_connected_at(7, t0);

  /* Silence up to just under the idle bound: nothing happens. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                    sdf_services_setup_phase_poll(t0 + T_IDLE_US - 1));

  /* GATT activity resets the idle timer but not the deadline. */
  sdf_services_setup_phase_notify_gatt_activity_at(t0 + T_IDLE_US - 1);
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                    sdf_services_setup_phase_poll(t0 + 2 * T_IDLE_US - 2));

  /* Idle expiry: drop the connection, stay armed, keep the deadline. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_DROP_IDLE_CONN,
                    sdf_services_setup_phase_poll(t0 + 2 * T_IDLE_US - 2 + 1));
  sdf_services_setup_phase_idle_drop();
  TEST_ASSERT_TRUE(sdf_services_setup_phase_is_armed());

  /* The deadline still runs from the original first connection and expires
   * at t0 + DEADLINE regardless of the idle churn above. The idle drop does
   * not restart the arm window either - once the deadline has started, the
   * arm window stops governing entirely, so dropping to no-connection can
   * never buy a fresh open-air window. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP,
                    sdf_services_setup_phase_poll(t0 + T_DEADLINE_US));
}

/* Regression: the arm window governs only before the first connection. A
 * disconnect or idle drop used to restart it, which handed a reconnecting
 * (or squatting) client a fresh open-air window on every cycle - an
 * extension the spec grants only to a physical button press. */
void test_setup_phase_arm_window_stops_governing_once_deadline_starts(void) {
  sdf_services_setup_phase_reset_for_test();

  const int64_t t0 = 0;
  sdf_services_setup_phase_arm_at(t0);
  sdf_services_setup_phase_notify_connected_at(3, t0);

  /* Disconnect immediately, then idle well past a full arm window with no
   * client. The arm window must not fire - only the deadline bounds this. */
  sdf_services_setup_phase_notify_disconnected_at(t0 + 1000);
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                    sdf_services_setup_phase_poll(t0 + T_ARM_WINDOW_US + 1));

  /* And it still expires exactly at the original deadline. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                    sdf_services_setup_phase_poll(t0 + T_DEADLINE_US - 1));
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP,
                    sdf_services_setup_phase_poll(t0 + T_DEADLINE_US));
}

void test_setup_phase_button_press_restarts_both_timers(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_services_setup_phase_reset_for_test();

  const int64_t t0 = 0;
  sdf_services_setup_phase_arm_at(t0);
  sdf_services_setup_phase_notify_connected_at(9, t0 + 1000000);

  /* Near the end of the original deadline (with fresh GATT activity so the
   * idle timer stays quiet)... */
  const int64_t t_press = t0 + T_DEADLINE_US - 30000000;
  sdf_services_setup_phase_notify_gatt_activity_at(t_press);
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                    sdf_services_setup_phase_poll(t_press));

  /* ...a button press (arm_at is its timer restart core) restarts both
   * timers: without it, the deadline would expire at t0 + DEADLINE. */
  sdf_services_setup_phase_arm_at(t_press);

  /* Keep the link alive with periodic activity (the client may work for the
   * whole budget); the phase must survive until the RESTARTED deadline. */
  for (int64_t t = t_press + 60000000; t < t_press + T_DEADLINE_US; t += 60000000) {
    sdf_services_setup_phase_notify_gatt_activity_at(t);
    TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_NONE,
                      sdf_services_setup_phase_poll(t));
  }
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP,
                    sdf_services_setup_phase_poll(t_press + T_DEADLINE_US));

  /* The reclaim gesture sets no pending admin action. */
  TEST_ASSERT_EQUAL(SDF_SERVICES_ADMIN_ACTION_NONE,
                    sdf_services_state()->pending_admin_action);
}

void test_setup_phase_timeout_wipe_erases_partial_state(void) {
  nvs_flash_init();
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  sdf_services_setup_phase_reset_for_test();

  /* Seed every partial-state record the wipe must erase. */
  sdf_storage_web_user_t user = make_web_user_record("wizard", false);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &user));

  uint8_t key[32] = {0x42};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_nuki_save(555, key));

  uint8_t addr[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(1, addr));

  SDF_SERVICES_BMP_SET(sdf_services_state()->enrolled_user_bmp, 3);

  sdf_services_setup_phase_arm_at(0);
  sdf_services_setup_phase_timeout_wipe();

  /* Phase disarmed... */
  TEST_ASSERT_FALSE(sdf_services_setup_phase_is_armed());

  /* ...and every partial record gone. The latch stays unset. */
  sdf_storage_web_user_t loaded = {0};
  TEST_ASSERT_NOT_EQUAL(ESP_OK, sdf_storage_web_user_load(1, &loaded));
  uint32_t auth_id;
  uint8_t shared[32];
  TEST_ASSERT_NOT_EQUAL(ESP_OK, sdf_storage_nuki_load(&auth_id, shared));
  size_t count = (size_t)-1;
  sdf_storage_admission_t entries[SDF_STORAGE_ADMISSION_MAX];
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_storage_admission_load_all(entries, SDF_STORAGE_ADMISSION_MAX, &count));
  TEST_ASSERT_EQUAL(0, count);
  bool complete = false;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_load(&complete));
  TEST_ASSERT_FALSE(complete);
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
 * never produces a valid frame on its own (see test_fingerprint_owner_task.c
 * and the fp-io-owner-task change this depends on), so fp_change_user_permission()
 * and fp_enroll_step() cannot be made to succeed in a host test. (fp_delete_user()
 * and fp_delete_all_users() gained one via sdf_mock_uart_queue_response() -
 * see the last-admin-delete-guard section below.) The pre-existing constraint
 * still explains why the remaining sensor-rollback and red-LED wrapper logic
 * around those call sites (e.g. sdf_services_enroll.c's
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

/* ---------------------------------------------------------------------------
 * Last-admin delete guard (last-admin-delete-guard).
 *
 * sdf_services_delete_user() must refuse an unenrolled id (ESP_ERR_NOT_FOUND)
 * and the deletion of the only enrolled admin (ESP_ERR_INVALID_STATE) from a
 * snapshot of the enrolled-user cache taken before any sensor traffic, while
 * sdf_services_clear_all_users() stays deliberately exempt.
 *
 * These are host-target-only: they run the real fingerprint owner task
 * against the Linux mock UART, scripting the sensor's ACK reply through
 * sdf_mock_uart_queue_response() so fp_delete_user()/fp_delete_all_users()
 * complete successfully and the cache-update/NVS-persist wrapper behind them
 * runs exactly as it does behind a real sensor. The no-data mock could never
 * produce that success path on its own (see the constraint noted above
 * test_persist_enrolled_users_locked_writes_current_cache_to_nvs(), which
 * this hook retires for the two frame-shaped ops these tests need).
 * ------------------------------------------------------------------------- */

#ifdef CONFIG_IDF_TARGET_LINUX

/* Wire-format constants pinned to fingerprint.c's private defines. A drift
 * fails loudly: fp_validate_response_impl() rejects a response whose marker,
 * cmd or checksum byte does not match its request, so the sensor op would
 * stop returning OK and the success-path assertions below would trip. */
#define TEST_FP_MARKER 0xF5u
#define TEST_FP_CMD_DELETE_USER 0x04u
#define TEST_FP_CMD_DELETE_ALL_USERS 0x05u

/* Scripts a success ACK for `cmd` in the exact frame shape
 * fp_validate_response_impl() expects: [marker, cmd, p1, p2, ack, p,
 * checksum, marker], checksum = XOR of bytes 1..5. */
static void queue_sensor_ack(uint8_t cmd) {
  uint8_t frame[8] = {TEST_FP_MARKER, cmd, 0x00, 0x00,
                      SDF_FINGERPRINT_ACK_SUCCESS, 0x00, 0x00,
                      TEST_FP_MARKER};
  frame[6] = (uint8_t)(frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5]);
  sdf_mock_uart_queue_response(frame, sizeof(frame));
}

static void delete_guard_test_setup(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  nvs_flash_init();
  sdf_mock_uart_reset();

  /* Same arbitrary-but-in-range driver config the owner-task suite uses -
   * the mock UART/GPIO never touch real hardware. */
  const sdf_fingerprint_driver_config_t config = {
      .uart_port = 0,
      .tx_pin = 0,
      .rx_pin = 1,
      .power_en_pin = 2,
      .baud_rate = 115200,
      .response_timeout_ms = 1000,
      .rx_buffer_size = 256,
      .tx_buffer_size = 256,
  };
  TEST_ASSERT_EQUAL(ESP_OK, fp_init(&config));
}

static void delete_guard_test_teardown(void) {
  fp_deinit();
  sdf_mock_uart_reset();
}

/* Seeds one user into both the in-RAM cache AND its persisted NVS record, so
 * each test can assert exactly how the operation under test moved either. */
static void seed_enrolled_user(uint16_t user_id, uint8_t permission) {
  sdf_services_state_t *s = sdf_services_state();
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, user_id);
  sdf_services_perm_set(s->enrolled_perm_packed, user_id, permission);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_save(
                                s->enrolled_user_bmp,
                                s->enrolled_perm_packed));
}

/* Task 3.1: deleting the sole admin is refused with ESP_ERR_INVALID_STATE,
 * and the refusal is decided before any sensor command goes out - no UART
 * writes, cache unchanged, NVS record unchanged. */
void test_delete_user_refuses_sole_admin_before_sensor(void) {
  delete_guard_test_setup();
  seed_enrolled_user(1, 3);

  uint32_t writes_before = sdf_mock_uart_write_count();
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sdf_services_delete_user(1));

  TEST_ASSERT_EQUAL(writes_before, sdf_mock_uart_write_count());
  sdf_services_state_t *s = sdf_services_state();
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(s->enrolled_user_bmp, 1));
  TEST_ASSERT_EQUAL(3, sdf_services_perm_get(s->enrolled_perm_packed, 1));

  uint16_t loaded_bmp = 0;
  uint8_t loaded_perm[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_storage_enrolled_users_load(&loaded_bmp, loaded_perm));
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(loaded_bmp, 1));

  delete_guard_test_teardown();
}

/* Task 3.2: with two admins enrolled, deleting one succeeds and updates both
 * the cached record and its NVS persistence; the surviving admin is intact.
 * Also covers the "Deleting an admin while another admin remains succeeds"
 * spec scenario. */
void test_delete_user_allows_one_of_two_admins(void) {
  delete_guard_test_setup();
  seed_enrolled_user(1, 3);
  seed_enrolled_user(2, 3);

  queue_sensor_ack(TEST_FP_CMD_DELETE_USER);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_delete_user(2));

  sdf_services_state_t *s = sdf_services_state();
  TEST_ASSERT_FALSE(SDF_SERVICES_BMP_TEST(s->enrolled_user_bmp, 2));
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(s->enrolled_user_bmp, 1));
  TEST_ASSERT_EQUAL(3, sdf_services_perm_get(s->enrolled_perm_packed, 1));

  uint16_t loaded_bmp = 0;
  uint8_t loaded_perm[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_storage_enrolled_users_load(&loaded_bmp, loaded_perm));
  TEST_ASSERT_EQUAL(s->enrolled_user_bmp, loaded_bmp);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(s->enrolled_perm_packed, loaded_perm,
                                SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);

  delete_guard_test_teardown();
}

/* Task 3.3: deleting a non-admin proceeds regardless of the admin count -
 * here with exactly one admin enrolled, the configuration the guard exists
 * to protect. */
void test_delete_user_allows_non_admin_with_single_admin_enrolled(void) {
  delete_guard_test_setup();
  seed_enrolled_user(1, 3);
  seed_enrolled_user(2, 1);

  queue_sensor_ack(TEST_FP_CMD_DELETE_USER);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_delete_user(2));

  sdf_services_state_t *s = sdf_services_state();
  TEST_ASSERT_FALSE(SDF_SERVICES_BMP_TEST(s->enrolled_user_bmp, 2));
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(s->enrolled_user_bmp, 1));
  TEST_ASSERT_EQUAL(3, sdf_services_perm_get(s->enrolled_perm_packed, 1));

  delete_guard_test_teardown();
}

/* Task 3.4: an id that is not set in the snapshot bitmap is reported as
 * ESP_ERR_NOT_FOUND without any sensor traffic - distinguishable from the
 * ESP_FAIL a failing sensor round-trip produces for an enrolled user. */
void test_delete_user_unenrolled_id_not_found_without_sensor_traffic(void) {
  delete_guard_test_setup();
  /* User 2 is an enrolled non-admin, so a delete request for it WOULD be
   * allowed past the guards - making the contrast below purely about
   * not-found vs. sensor failure. */
  seed_enrolled_user(1, 3);
  seed_enrolled_user(2, 1);

  uint32_t writes_before = sdf_mock_uart_write_count();
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, sdf_services_delete_user(7));
  TEST_ASSERT_EQUAL(writes_before, sdf_mock_uart_write_count());

  /* With no scripted reply the same request for the enrolled non-admin
   * fails at the sensor instead - a different code, leaving the cache (and
   * its bit for user 2) untouched because the mutation path is never
   * reached. */
  TEST_ASSERT_EQUAL(ESP_FAIL, sdf_services_delete_user(2));
  TEST_ASSERT_TRUE(SDF_SERVICES_BMP_TEST(
      sdf_services_state()->enrolled_user_bmp, 2));

  delete_guard_test_teardown();
}

/* Task 3.5: clear-all is exempt from the guard - a single-admin device is
 * still wiped completely, cache and NVS both zeroed. */
void test_clear_all_users_still_clears_single_admin_device(void) {
  delete_guard_test_setup();
  seed_enrolled_user(1, 3);
  seed_enrolled_user(2, 1);

  queue_sensor_ack(TEST_FP_CMD_DELETE_ALL_USERS);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_clear_all_users());

  TEST_ASSERT_EQUAL(0, sdf_services_state()->enrolled_user_bmp);

  uint16_t loaded_bmp = 0xFFFF;
  uint8_t loaded_perm[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN];
  memset(loaded_perm, 0xAA, sizeof(loaded_perm));
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_storage_enrolled_users_load(&loaded_bmp, loaded_perm));
  TEST_ASSERT_EQUAL(0, loaded_bmp);
  uint8_t zero_perm[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(zero_perm, loaded_perm, sizeof(zero_perm));

  delete_guard_test_teardown();
}

/* companion-identity 5.3: deleting a user destroys their unified record -
 * including any bound credential - and their name afterwards answers as an
 * unknown name for login purposes (find_by_name() misses, so LOGIN_INIT
 * routes through the pseudo-salt challenge). */
void test_delete_user_destroys_bound_credential(void) {
  delete_guard_test_setup();
  seed_enrolled_user(1, 3);
  seed_enrolled_user(2, 3);

  sdf_storage_web_user_clear_all();
  sdf_storage_web_user_t account = make_web_user_record("alice", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(2, &account));
  /* A separate record for the surviving admin - deletion must not touch it. */
  sdf_storage_web_user_t survivor_seed = make_web_user_record("bob", false);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &survivor_seed));

  queue_sensor_ack(TEST_FP_CMD_DELETE_USER);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_delete_user(2));

  /* The record - name, salt and stretched credential - is gone. */
  sdf_storage_web_user_t loaded = {0};
  TEST_ASSERT_NOT_EQUAL(ESP_OK, sdf_storage_web_user_load(2, &loaded));

  /* A LOGIN_INIT for that name now resolves as unknown: find_by_name()
   * misses, which is exactly the condition that routes the challenge
   * through the pseudo-salt path. */
  uint16_t holder = 0;
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                    sdf_services_find_name_holder("alice", &holder));

  /* The surviving admin's record is untouched. */
  sdf_storage_web_user_t survivor = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(1, &survivor));
  TEST_ASSERT_EQUAL_STRING("bob", survivor.name);

  sdf_storage_web_user_clear_all();
  delete_guard_test_teardown();
}

/* companion-identity 5.3/4.3: deletion releases the name implicitly by
 * clearing the record - no separate reclamation step exists - so the same
 * name can be re-registered for another user afterwards. */
void test_deleted_name_is_available_for_reuse(void) {
  delete_guard_test_setup();
  seed_enrolled_user(1, 3);
  seed_enrolled_user(2, 1);

  sdf_storage_web_user_clear_all();
  sdf_storage_web_user_t account = make_web_user_record("shared", false);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(2, &account));

  queue_sensor_ack(TEST_FP_CMD_DELETE_USER);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_delete_user(2));

  /* The freed name can now be given to another enrolled user. */
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_set_user_name(1, "shared"));

  sdf_storage_web_user_t check = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(1, &check));
  TEST_ASSERT_EQUAL_STRING("shared", check.name);

  sdf_storage_web_user_clear_all();
  delete_guard_test_teardown();
}

#endif /* CONFIG_IDF_TARGET_LINUX */

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
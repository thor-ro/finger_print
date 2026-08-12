#include "unity.h"

#include <string.h>

#include "sdf_services.h"
#include "sdf_services_internal.h"

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

/* Web auth: login verification */

static sdf_storage_web_user_t make_web_user(const uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN]) {
  sdf_storage_web_user_t user = {0};
  strncpy(user.username, "alice", sizeof(user.username) - 1);
  user.permission = 1;
  user.valid = true;
  memcpy(user.password_hash, hash, SDF_STORAGE_WEB_USER_HASH_LEN);
  return user;
}

void test_web_auth_verify_login_matching_hash_is_valid(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0xAB, sizeof(hash));
  sdf_storage_web_user_t user = make_web_user(hash);

  TEST_ASSERT_TRUE(sdf_services_web_auth_verify_login(&user, hash, sizeof(hash)));
}

void test_web_auth_verify_login_mismatched_hash_is_invalid(void) {
  uint8_t stored_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(stored_hash, 0xAB, sizeof(stored_hash));
  sdf_storage_web_user_t user = make_web_user(stored_hash);

  uint8_t submitted_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(submitted_hash, 0xCD, sizeof(submitted_hash));

  TEST_ASSERT_FALSE(sdf_services_web_auth_verify_login(&user, submitted_hash, sizeof(submitted_hash)));
}

void test_web_auth_verify_login_wrong_hash_len_is_invalid(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0xAB, sizeof(hash));
  sdf_storage_web_user_t user = make_web_user(hash);

  TEST_ASSERT_FALSE(sdf_services_web_auth_verify_login(&user, hash, sizeof(hash) - 1));
}

void test_web_auth_verify_login_all_zero_hash_is_invalid(void) {
  uint8_t stored_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(stored_hash, 0xAB, sizeof(stored_hash));
  sdf_storage_web_user_t user = make_web_user(stored_hash);

  uint8_t zero_hash[SDF_STORAGE_WEB_USER_HASH_LEN] = {0};

  TEST_ASSERT_FALSE(sdf_services_web_auth_verify_login(&user, zero_hash, sizeof(zero_hash)));
}

/* Web auth: registration decision */

void test_web_auth_decide_registration_authorized_persists_user(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0x11, sizeof(hash));

  sdf_services_web_auth_registration_decision_t decision =
      sdf_services_web_auth_decide_registration("bob", hash, sizeof(hash), 2, true);

  TEST_ASSERT_TRUE(decision.should_persist);
  TEST_ASSERT_TRUE(decision.reply_authorized);
  TEST_ASSERT_EQUAL_STRING("bob", decision.user.username);
  TEST_ASSERT_EQUAL(2, decision.user.permission);
  TEST_ASSERT_TRUE(decision.user.valid);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(hash, decision.user.password_hash, sizeof(hash));
}

void test_web_auth_decide_registration_denied_does_not_persist(void) {
  uint8_t hash[SDF_STORAGE_WEB_USER_HASH_LEN];
  memset(hash, 0x11, sizeof(hash));

  sdf_services_web_auth_registration_decision_t decision =
      sdf_services_web_auth_decide_registration("bob", hash, sizeof(hash), 2, false);

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
}

/* Setup-state helper */

void test_setup_state_unclaimed_when_no_enrolled_users(void) {
  ensure_services_initialized();
  TEST_ASSERT_EQUAL(ESP_OK, sdf_services_reset_state());
  TEST_ASSERT_EQUAL(SDF_SERVICES_SETUP_STATE_UNCLAIMED, sdf_services_get_setup_state());
}
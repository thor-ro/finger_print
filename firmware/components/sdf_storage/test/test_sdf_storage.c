#include "unity.h"
#include <string.h>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdf_storage.h"

// -----------------------------------------------------------------------------
// Test Setup & Teardown
// -----------------------------------------------------------------------------

static void nvs_setup(void) {
  // Initialize NVS for the tests (writes to host filesystem on Linux target)
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_erase());
    err = nvs_flash_init();
  }
  TEST_ASSERT_EQUAL(ESP_OK, err);
}

static void nvs_teardown(void) {
  // Clear everything so the next test runs clean and deinit
  nvs_flash_erase();
  nvs_flash_deinit();
}

// -----------------------------------------------------------------------------
// Unit Tests
// -----------------------------------------------------------------------------

void test_sdf_storage_nuki_save_and_load_success(void) {
  nvs_setup();

  uint8_t key_to_save[32];
  memset(key_to_save, 0xAA, 32);

  // Test Save
  esp_err_t err = sdf_storage_nuki_save(1234, key_to_save);
  TEST_ASSERT_EQUAL(ESP_OK, err);

  // Test Load
  uint32_t loaded_auth_id = 0;
  uint8_t loaded_key[32] = {0};
  err = sdf_storage_nuki_load(&loaded_auth_id, loaded_key);

  TEST_ASSERT_EQUAL(ESP_OK, err);
  TEST_ASSERT_EQUAL(1234, loaded_auth_id);
  TEST_ASSERT_EQUAL_MEMORY(key_to_save, loaded_key, 32);

  nvs_teardown();
}

void test_sdf_storage_nuki_load_not_found(void) {
  nvs_setup();

  uint32_t auth_id;
  uint8_t key[32] = {0};

  // Shouldn't find anything in an empty NVS
  esp_err_t err = sdf_storage_nuki_load(&auth_id, key);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_nuki_save_invalid_args(void) {
  nvs_setup();

  // Null pointer should be rejected
  esp_err_t err = sdf_storage_nuki_save(1234, NULL);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

  nvs_teardown();
}

void test_sdf_storage_nuki_load_invalid_args(void) {
  nvs_setup();

  uint32_t auth_id;
  uint8_t key[32];

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_nuki_load(NULL, key));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_nuki_load(&auth_id, NULL));

  nvs_teardown();
}

void test_sdf_storage_nuki_clear_success(void) {
  nvs_setup();

  uint8_t key_to_save[32];
  memset(key_to_save, 0xBB, 32);

  // Save first
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_nuki_save(5678, key_to_save));

  // Clear it
  esp_err_t err = sdf_storage_nuki_clear();
  TEST_ASSERT_EQUAL(ESP_OK, err);

  // Load should now fail
  uint32_t auth_id;
  uint8_t key[32];
  err = sdf_storage_nuki_load(&auth_id, key);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_nuki_clear_already_cleared(void) {
  nvs_setup();

  // Clear on empty NVS shouldn't return error
  esp_err_t err = sdf_storage_nuki_clear();
  TEST_ASSERT_EQUAL(ESP_OK, err);

  nvs_teardown();
}

void test_sdf_storage_ble_target_save_and_load_success(void) {
  nvs_setup();

  uint8_t mac_to_save[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

  // Save
  esp_err_t err = sdf_storage_ble_target_save(1, mac_to_save);
  TEST_ASSERT_EQUAL(ESP_OK, err);

  // Load
  uint8_t type_loaded = 0;
  uint8_t mac_loaded[6] = {0};
  err = sdf_storage_ble_target_load(&type_loaded, mac_loaded);

  TEST_ASSERT_EQUAL(ESP_OK, err);
  TEST_ASSERT_EQUAL(1, type_loaded);
  TEST_ASSERT_EQUAL_MEMORY(mac_to_save, mac_loaded, 6);

  nvs_teardown();
}

void test_sdf_storage_ble_target_load_not_found(void) {
  nvs_setup();

  uint8_t type_loaded;
  uint8_t mac_loaded[6];

  esp_err_t err = sdf_storage_ble_target_load(&type_loaded, mac_loaded);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_ble_target_clear_success(void) {
  nvs_setup();

  uint8_t mac_to_save[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_ble_target_save(1, mac_to_save));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_ble_target_clear());

  uint8_t type_loaded;
  uint8_t mac_loaded[6];
  esp_err_t err = sdf_storage_ble_target_load(&type_loaded, mac_loaded);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_ble_target_clear_already_cleared(void) {
  nvs_setup();

  esp_err_t err = sdf_storage_ble_target_clear();
  TEST_ASSERT_EQUAL(ESP_OK, err);

  nvs_teardown();
}

void test_sdf_storage_erase_all_success(void) {
  nvs_setup();

  uint8_t key_to_save[32];
  memset(key_to_save, 0xCC, 32);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_nuki_save(9999, key_to_save));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_erase_all());

  uint32_t auth_id;
  uint8_t key[32];
  esp_err_t err = sdf_storage_nuki_load(&auth_id, key);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_erase_all_idempotent(void) {
  nvs_setup();

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_erase_all());
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_erase_all());

  nvs_teardown();
}

// -----------------------------------------------------------------------------
// Web user storage
// -----------------------------------------------------------------------------

static sdf_storage_web_user_t make_web_user(const char *username, uint8_t permission) {
  sdf_storage_web_user_t user = {0};
  strncpy(user.username, username, sizeof(user.username) - 1);
  memset(user.salt, 0xCD, sizeof(user.salt));
  memset(user.stretched_credential, 0xAB, sizeof(user.stretched_credential));
  user.permission = permission;
  user.valid = true;
  return user;
}

void test_sdf_storage_web_user_save_and_load_success(void) {
  nvs_setup();

  sdf_storage_web_user_t saved = make_web_user("alice", 1);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(0, &saved));

  sdf_storage_web_user_t loaded = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(0, &loaded));
  TEST_ASSERT_EQUAL_STRING("alice", loaded.username);
  TEST_ASSERT_EQUAL_MEMORY(saved.salt, loaded.salt, SDF_STORAGE_WEB_USER_SALT_LEN);
  TEST_ASSERT_EQUAL_MEMORY(saved.stretched_credential, loaded.stretched_credential, SDF_STORAGE_WEB_USER_STRETCHED_LEN);
  TEST_ASSERT_EQUAL(1, loaded.permission);
  TEST_ASSERT_TRUE(loaded.valid);

  nvs_teardown();
}

void test_sdf_storage_web_user_load_not_found(void) {
  nvs_setup();

  sdf_storage_web_user_t loaded = {0};
  esp_err_t err = sdf_storage_web_user_load(0, &loaded);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_web_user_find_by_name_hit(void) {
  nvs_setup();

  sdf_storage_web_user_t saved = make_web_user("bob", 2);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &saved));

  sdf_storage_web_user_t found = {0};
  uint8_t index_out = 0xFF;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_find_by_name("bob", &found, &index_out));
  TEST_ASSERT_EQUAL(1, index_out);
  TEST_ASSERT_EQUAL_STRING("bob", found.username);

  nvs_teardown();
}

void test_sdf_storage_web_user_find_by_name_miss(void) {
  nvs_setup();

  // find_by_name()'s "not found" translation to ESP_ERR_NOT_FOUND only
  // happens once its scan loop actually runs, which requires
  // nvs_open(..., NVS_READONLY, ...) to succeed first. On a namespace
  // that's never been written to at all, that nvs_open() call itself fails
  // with ESP_ERR_NVS_NOT_FOUND before the loop is ever reached - so save an
  // (unrelated) entry first to create the namespace and exercise the real
  // "present namespace, no matching user" miss path.
  sdf_storage_web_user_t saved = make_web_user("someone", 1);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(0, &saved));

  sdf_storage_web_user_t found = {0};
  uint8_t index_out = 0xFF;
  esp_err_t err = sdf_storage_web_user_find_by_name("nobody", &found, &index_out);
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_web_user_clear_success(void) {
  nvs_setup();

  sdf_storage_web_user_t saved = make_web_user("carol", 1);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(2, &saved));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_clear(2));

  sdf_storage_web_user_t loaded = {0};
  esp_err_t err = sdf_storage_web_user_load(2, &loaded);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_web_user_clear_all(void) {
  nvs_setup();

  for (uint8_t i = 0; i < SDF_STORAGE_WEB_USER_MAX; i++) {
    sdf_storage_web_user_t saved = make_web_user("user", 1);
    TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(i, &saved));
  }

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_clear_all());

  size_t count = (size_t)-1;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_count(&count));
  TEST_ASSERT_EQUAL(0, count);

  nvs_teardown();
}

void test_sdf_storage_web_user_count(void) {
  nvs_setup();

  // Like find_by_name() above, count() opens the namespace NVS_READONLY and
  // returns that nvs_open() error directly if the namespace has never been
  // written to - so "count is 0" can only be observed once the namespace
  // exists. Save-then-clear first to exercise that starting-from-zero case
  // for real, rather than asserting on the very first open of a totally
  // virgin partition.
  sdf_storage_web_user_t saved1 = make_web_user("dave", 1);
  sdf_storage_web_user_t saved2 = make_web_user("erin", 3);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(0, &saved1));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &saved2));

  size_t count = (size_t)-1;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_count(&count));
  TEST_ASSERT_EQUAL(2, count);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_clear(0));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_clear(1));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_count(&count));
  TEST_ASSERT_EQUAL(0, count);

  nvs_teardown();
}

void test_sdf_storage_web_user_save_index_at_max_rejected(void) {
  nvs_setup();

  // Index must be < SDF_STORAGE_WEB_USER_MAX - there's no auto-allocated
  // slot, so the max index itself is out of range.
  sdf_storage_web_user_t saved = make_web_user("overflow", 1);
  esp_err_t err = sdf_storage_web_user_save(SDF_STORAGE_WEB_USER_MAX, &saved);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

  nvs_teardown();
}

// -----------------------------------------------------------------------------
// Web pseudo-salt HMAC key
// -----------------------------------------------------------------------------

void test_sdf_storage_web_pseudo_salt_key_generates_on_first_use(void) {
  nvs_setup();

  uint8_t key[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_pseudo_salt_key_load_or_generate(key));

  // A freshly generated key should not be all-zero (esp_fill_random on the
  // linux host target still produces non-trivial output).
  uint8_t all_zero[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN] = {0};
  TEST_ASSERT_NOT_EQUAL(0, memcmp(all_zero, key, sizeof(key)));

  nvs_teardown();
}

void test_sdf_storage_web_pseudo_salt_key_persists_across_loads(void) {
  nvs_setup();

  uint8_t first[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_pseudo_salt_key_load_or_generate(first));

  uint8_t second[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_pseudo_salt_key_load_or_generate(second));

  TEST_ASSERT_EQUAL_MEMORY(first, second, SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN);

  nvs_teardown();
}

void test_sdf_storage_web_pseudo_salt_key_null_arg_rejected(void) {
  nvs_setup();

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_web_pseudo_salt_key_load_or_generate(NULL));

  nvs_teardown();
}

void test_sdf_storage_erase_all_clears_web_users_and_pseudo_salt_key(void) {
  nvs_setup();

  sdf_storage_web_user_t saved = make_web_user("frank", 1);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(0, &saved));

  uint8_t key_before[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_pseudo_salt_key_load_or_generate(key_before));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_erase_all());

  sdf_storage_web_user_t loaded = {0};
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, sdf_storage_web_user_load(0, &loaded));

  // The pseudo-salt key must be gone too - erase_all wipes the whole
  // namespace, so load_or_generate() regenerates a fresh (different) key
  // rather than returning the pre-erase one, keeping the two in lockstep.
  uint8_t key_after[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_pseudo_salt_key_load_or_generate(key_after));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(key_before, key_after, sizeof(key_before)));

  nvs_teardown();
}

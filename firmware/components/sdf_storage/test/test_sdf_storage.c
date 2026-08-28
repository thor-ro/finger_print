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
// Unified per-user records (companion-identity)
// -----------------------------------------------------------------------------

static sdf_storage_web_user_t make_web_user(const char *name, bool has_credential) {
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

void test_sdf_storage_web_user_save_and_load_success(void) {
  nvs_setup();

  sdf_storage_web_user_t saved = make_web_user("alice", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &saved));

  sdf_storage_web_user_t loaded = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_load(1, &loaded));
  TEST_ASSERT_EQUAL_STRING("alice", loaded.name);
  TEST_ASSERT_EQUAL_MEMORY(saved.salt, loaded.salt, SDF_STORAGE_WEB_USER_SALT_LEN);
  TEST_ASSERT_EQUAL_MEMORY(saved.stretched_credential, loaded.stretched_credential, SDF_STORAGE_WEB_USER_STRETCHED_LEN);
  TEST_ASSERT_TRUE(loaded.has_credential);
  TEST_ASSERT_TRUE(loaded.valid);

  nvs_teardown();
}

/* Task 1.3: the record deliberately carries no permission field. Every
 * member is byte-aligned, so the struct has no padding to hide a new field
 * in: pinning its exact size makes any added member - a returned
 * permission byte above all - fail here. The same assertion guards the
 * persisted NVS blob layout, which is this struct written verbatim. */
void test_sdf_storage_web_user_record_has_no_permission_field(void) {
  TEST_ASSERT_EQUAL(SDF_STORAGE_WEB_USER_NAME_MAX + SDF_STORAGE_WEB_USER_SALT_LEN +
                        SDF_STORAGE_WEB_USER_STRETCHED_LEN +
                        2 /* has_credential, valid */,
                    sizeof(sdf_storage_web_user_t));
}

void test_sdf_storage_web_user_load_absent_key_is_not_found(void) {
  nvs_setup();

  /* Namespace exists (unrelated write creates it) but this user id was
   * never written - an absent-key read must report NVS_NOT_FOUND, which is
   * what callers use to distinguish "no record" from "empty record". */
  uint8_t key_to_save[32];
  memset(key_to_save, 0xEE, 32);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_nuki_save(42, key_to_save));

  sdf_storage_web_user_t loaded = {0};
  esp_err_t err = sdf_storage_web_user_load(4, &loaded);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_web_user_find_by_name_hit(void) {
  nvs_setup();

  sdf_storage_web_user_t saved = make_web_user("bob", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(2, &saved));

  sdf_storage_web_user_t found = {0};
  uint16_t id_out = 0xFFFF;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_find_by_name("bob", &found, &id_out));
  TEST_ASSERT_EQUAL(2, id_out);
  TEST_ASSERT_EQUAL_STRING("bob", found.name);

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
  sdf_storage_web_user_t saved = make_web_user("someone", false);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &saved));

  sdf_storage_web_user_t found = {0};
  uint16_t id_out = 0xFFFF;
  esp_err_t err = sdf_storage_web_user_find_by_name("nobody", &found, &id_out);
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);

  nvs_teardown();
}

/* Task 1.6: a record with a name but no credential is a person, not an
 * account - find_by_name() (the LOGIN_INIT lookup) must report it as no
 * match so a non-admin's name falls on the unknown-name side of the
 * indistinguishability line. */
void test_sdf_storage_web_user_find_by_name_ignores_credentialless_record(void) {
  nvs_setup();

  sdf_storage_web_user_t name_only = make_web_user("standard", false);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(3, &name_only));
  sdf_storage_web_user_t holder = make_web_user("admin", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &holder));

  sdf_storage_web_user_t found = {0};
  uint16_t id_out = 0xFFFF;
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,
                    sdf_storage_web_user_find_by_name("standard", &found, &id_out));
  /* The credential-holding name still resolves. */
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_storage_web_user_find_by_name("admin", &found, &id_out));
  TEST_ASSERT_EQUAL(1, id_out);

  nvs_teardown();
}

void test_sdf_storage_web_user_clear_success(void) {
  nvs_setup();

  sdf_storage_web_user_t saved = make_web_user("carol", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(2, &saved));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_clear(2));

  sdf_storage_web_user_t loaded = {0};
  esp_err_t err = sdf_storage_web_user_load(2, &loaded);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_teardown();
}

void test_sdf_storage_web_user_clear_all(void) {
  nvs_setup();

  for (uint16_t id = SDF_STORAGE_FP_USER_ID_MIN; id <= SDF_STORAGE_FP_USER_ID_MAX; id++) {
    sdf_storage_web_user_t saved = make_web_user("user", true);
    TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(id, &saved));
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
  sdf_storage_web_user_t saved1 = make_web_user("dave", true);
  sdf_storage_web_user_t saved2 = make_web_user("erin", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &saved1));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(2, &saved2));

  size_t count = (size_t)-1;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_count(&count));
  TEST_ASSERT_EQUAL(2, count);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_clear(1));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_clear(2));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_count(&count));
  TEST_ASSERT_EQUAL(0, count);

  nvs_teardown();
}

/* Task 1.7: count() counts accounts (records holding a credential); a
 * name-only record is not counted. */
void test_sdf_storage_web_user_counts_only_credentials(void) {
  nvs_setup();

  sdf_storage_web_user_t account = make_web_user("withcred", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &account));
  sdf_storage_web_user_t name_only = make_web_user("nameonly", false);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(2, &name_only));

  size_t count = 0;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_count(&count));
  TEST_ASSERT_EQUAL(1, count);

  nvs_teardown();
}

void test_sdf_storage_web_user_id_out_of_range_rejected(void) {
  nvs_setup();

  // Ids are fingerprint user ids 1-10 inclusive: 0 and 11 are both outside
  // the range - there is no auto-allocated slot and no zero-based index
  // space anymore.
  sdf_storage_web_user_t saved = make_web_user("overflow", true);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_web_user_save(0, &saved));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_storage_web_user_save(SDF_STORAGE_FP_USER_ID_MAX + 1u, &saved));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_web_user_load(0, &saved));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_storage_web_user_load(SDF_STORAGE_FP_USER_ID_MAX + 1u, &saved));

  nvs_teardown();
}

/* Task 1.2: account capacity matches user capacity - all ten ids hold a
 * record simultaneously, so no admin can be refused an account by a limit
 * unrelated to their permission. */
void test_sdf_storage_web_user_capacity_at_all_ten_ids(void) {
  nvs_setup();

  for (uint16_t id = SDF_STORAGE_FP_USER_ID_MIN; id <= SDF_STORAGE_FP_USER_ID_MAX; id++) {
    char name[SDF_STORAGE_WEB_USER_NAME_MAX];
    snprintf(name, sizeof(name), "user%u", (unsigned)id);
    sdf_storage_web_user_t saved = make_web_user(name, true);
    TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(id, &saved));
  }

  size_t count = 0;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_count(&count));
  TEST_ASSERT_EQUAL(SDF_STORAGE_FP_USER_ID_MAX, count);

  /* Every one of the ten resolves back by name. */
  for (uint16_t id = SDF_STORAGE_FP_USER_ID_MIN; id <= SDF_STORAGE_FP_USER_ID_MAX; id++) {
    char name[SDF_STORAGE_WEB_USER_NAME_MAX];
    snprintf(name, sizeof(name), "user%u", (unsigned)id);
    sdf_storage_web_user_t found = {0};
    uint16_t id_out = 0;
    TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_find_by_name(name, &found, &id_out));
    TEST_ASSERT_EQUAL(id, id_out);
  }

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

  /* Task 1.8: erase_all() wipes the whole flash namespace, which is what
   * clears the unified records - no separate web-user/name clear step
   * exists or is needed. */
  sdf_storage_web_user_t saved = make_web_user("frank", true);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_user_save(1, &saved));

  uint8_t key_before[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_pseudo_salt_key_load_or_generate(key_before));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_erase_all());

  sdf_storage_web_user_t loaded = {0};
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, sdf_storage_web_user_load(1, &loaded));

  // The pseudo-salt key must be gone too - erase_all wipes the whole
  // namespace, so load_or_generate() regenerates a fresh (different) key
  // rather than returning the pre-erase one, keeping the two in lockstep.
  uint8_t key_after[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_web_pseudo_salt_key_load_or_generate(key_after));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(key_before, key_after, sizeof(key_before)));

  nvs_teardown();
}

// -----------------------------------------------------------------------------
// Enrolled-user cache (bitmap + packed permissions)
// -----------------------------------------------------------------------------

void test_sdf_storage_enrolled_users_save_and_load_success(void) {
  nvs_setup();

  uint16_t bmp_to_save = 0x0025; // users 1, 3, 6 enrolled
  uint8_t perm_to_save[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0x11, 0x22, 0x33};

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_save(bmp_to_save, perm_to_save));

  uint16_t bmp_loaded = 0;
  uint8_t perm_loaded[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_load(&bmp_loaded, perm_loaded));

  TEST_ASSERT_EQUAL(bmp_to_save, bmp_loaded);
  TEST_ASSERT_EQUAL_MEMORY(perm_to_save, perm_loaded, SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);

  nvs_teardown();
}

void test_sdf_storage_enrolled_users_load_not_found_reads_as_zero(void) {
  nvs_setup();

  // No prior save at all - namespace itself doesn't exist yet. Per
  // design.md's Migration Plan, this SHALL read as "zero users", not an
  // error, so a device upgrading to this firmware boots as unclaimed rather
  // than failing to initialize.
  uint16_t bmp_loaded = 0xFFFF;
  uint8_t perm_loaded[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0xFF, 0xFF, 0xFF};
  esp_err_t err = sdf_storage_enrolled_users_load(&bmp_loaded, perm_loaded);

  TEST_ASSERT_EQUAL(ESP_OK, err);
  TEST_ASSERT_EQUAL(0, bmp_loaded);
  uint8_t all_zero[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};
  TEST_ASSERT_EQUAL_MEMORY(all_zero, perm_loaded, SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);

  nvs_teardown();
}

void test_sdf_storage_enrolled_users_load_not_found_within_existing_namespace(void) {
  nvs_setup();

  // Namespace exists (from an unrelated write) but the enrolled-users key
  // has never been written - still reads as zero users, not an error.
  uint8_t key_to_save[32];
  memset(key_to_save, 0xEE, 32);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_nuki_save(42, key_to_save));

  uint16_t bmp_loaded = 0xFFFF;
  uint8_t perm_loaded[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0xFF, 0xFF, 0xFF};
  esp_err_t err = sdf_storage_enrolled_users_load(&bmp_loaded, perm_loaded);

  TEST_ASSERT_EQUAL(ESP_OK, err);
  TEST_ASSERT_EQUAL(0, bmp_loaded);
  uint8_t all_zero[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};
  TEST_ASSERT_EQUAL_MEMORY(all_zero, perm_loaded, SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);

  nvs_teardown();
}

void test_sdf_storage_enrolled_users_save_invalid_args(void) {
  nvs_setup();

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_enrolled_users_save(1, NULL));

  nvs_teardown();
}

void test_sdf_storage_enrolled_users_load_invalid_args(void) {
  nvs_setup();

  uint16_t bmp = 0;
  uint8_t perm[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_enrolled_users_load(NULL, perm));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_enrolled_users_load(&bmp, NULL));

  nvs_teardown();
}

void test_sdf_storage_enrolled_users_save_overwrites_previous(void) {
  nvs_setup();

  uint8_t perm_v1[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0x01, 0x02, 0x03};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_save(0x0001, perm_v1));

  uint8_t perm_v2[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0x04, 0x05, 0x06};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_save(0x0003, perm_v2));

  uint16_t bmp_loaded = 0;
  uint8_t perm_loaded[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_load(&bmp_loaded, perm_loaded));

  TEST_ASSERT_EQUAL(0x0003, bmp_loaded);
  TEST_ASSERT_EQUAL_MEMORY(perm_v2, perm_loaded, SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN);

  nvs_teardown();
}

void test_sdf_storage_erase_all_clears_enrolled_users(void) {
  nvs_setup();

  uint8_t perm[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0x01, 0x02, 0x03};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_save(0x0007, perm));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_erase_all());

  uint16_t bmp_loaded = 0xFFFF;
  uint8_t perm_loaded[SDF_STORAGE_ENROLLED_USERS_PERM_PACKED_LEN] = {0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_enrolled_users_load(&bmp_loaded, perm_loaded));
  TEST_ASSERT_EQUAL(0, bmp_loaded);

  nvs_teardown();
}

// -----------------------------------------------------------------------------
// Setup-completion latch
// -----------------------------------------------------------------------------

void test_sdf_storage_setup_complete_save_and_load(void) {
  nvs_setup();

  bool complete = true;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_load(&complete));
  TEST_ASSERT_FALSE(complete);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_save(true));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_load(&complete));
  TEST_ASSERT_TRUE(complete);

  nvs_teardown();
}

void test_sdf_storage_setup_complete_absent_key_reads_as_false_not_error(void) {
  nvs_setup();

  // Namespace exists but key never written - still reads as unset.
  uint8_t key_to_save[32];
  memset(key_to_save, 0xEE, 32);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_nuki_save(42, key_to_save));

  bool complete = true;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_load(&complete));
  TEST_ASSERT_FALSE(complete);

  // And on a completely virgin partition too.
  nvs_flash_erase();
  nvs_flash_deinit();
  nvs_setup();

  complete = true;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_load(&complete));
  TEST_ASSERT_FALSE(complete);

  nvs_teardown();
}

void test_sdf_storage_setup_complete_clear(void) {
  nvs_setup();

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_save(true));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_clear());

  bool complete = true;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_load(&complete));
  TEST_ASSERT_FALSE(complete);

  // Clearing an already-cleared latch is not an error.
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_clear());

  nvs_teardown();
}

void test_sdf_storage_setup_complete_invalid_args(void) {
  nvs_setup();

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_setup_complete_load(NULL));

  nvs_teardown();
}

void test_sdf_storage_erase_all_clears_setup_latch_and_admissions(void) {
  nvs_setup();

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_save(true));
  uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(1, mac));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_erase_all());

  bool complete = false;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_load(&complete));
  TEST_ASSERT_FALSE(complete);

  size_t count = (size_t)-1;
  sdf_storage_admission_t entries[SDF_STORAGE_ADMISSION_MAX];
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_storage_admission_load_all(entries, SDF_STORAGE_ADMISSION_MAX, &count));
  TEST_ASSERT_EQUAL(0, count);

  nvs_teardown();
}

// -----------------------------------------------------------------------------
// Admission records
// -----------------------------------------------------------------------------

void test_sdf_storage_admission_add_and_load_all(void) {
  nvs_setup();

  uint8_t a1[6] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
  uint8_t a2[6] = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(0, a1));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(1, a2));

  sdf_storage_admission_t entries[SDF_STORAGE_ADMISSION_MAX];
  size_t count = 0;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_load_all(entries, SDF_STORAGE_ADMISSION_MAX, &count));
  TEST_ASSERT_EQUAL(2, count);
  TEST_ASSERT_EQUAL(0, entries[0].addr_type);
  TEST_ASSERT_EQUAL_MEMORY(a1, entries[0].addr, 6);
  TEST_ASSERT_EQUAL(1, entries[1].addr_type);
  TEST_ASSERT_EQUAL_MEMORY(a2, entries[1].addr, 6);

  nvs_teardown();
}

void test_sdf_storage_admission_add_duplicate_is_idempotent(void) {
  nvs_setup();

  uint8_t a1[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(1, a1));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(1, a1));

  sdf_storage_admission_t entries[SDF_STORAGE_ADMISSION_MAX];
  size_t count = 0;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_load_all(entries, SDF_STORAGE_ADMISSION_MAX, &count));
  TEST_ASSERT_EQUAL(1, count);

  nvs_teardown();
}

void test_sdf_storage_admission_remove_compacts(void) {
  nvs_setup();

  uint8_t a1[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t a2[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t a3[6] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(0, a1));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(0, a2));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(0, a3));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_remove(0, a2));

  sdf_storage_admission_t entries[SDF_STORAGE_ADMISSION_MAX];
  size_t count = 0;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_load_all(entries, SDF_STORAGE_ADMISSION_MAX, &count));
  TEST_ASSERT_EQUAL(2, count);
  TEST_ASSERT_EQUAL_MEMORY(a1, entries[0].addr, 6);
  TEST_ASSERT_EQUAL_MEMORY(a3, entries[1].addr, 6);

  // Removing an address that was never admitted is not an error.
  uint8_t missing[6] = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_remove(1, missing));

  nvs_teardown();
}

void test_sdf_storage_admission_capacity_exhaustion_rejected(void) {
  nvs_setup();

  for (size_t i = 0; i < SDF_STORAGE_ADMISSION_MAX; i++) {
    uint8_t addr[6];
    memset(addr, (int)(i + 1), sizeof(addr));
    TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add((uint8_t)i, addr));
  }

  uint8_t overflow[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, sdf_storage_admission_add(1, overflow));

  // Capacity is at least CONFIG_BT_NIMBLE_MAX_BONDS (3).
  TEST_ASSERT_TRUE(SDF_STORAGE_ADMISSION_MAX >= 3);

  nvs_teardown();
}

void test_sdf_storage_admission_load_all_buffer_too_small_reports_no_mem(void) {
  nvs_setup();

  uint8_t a1[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t a2[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(0, a1));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(1, a2));

  sdf_storage_admission_t one;
  size_t count = 0;
  TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, sdf_storage_admission_load_all(&one, 1, &count));
  TEST_ASSERT_EQUAL(2, count);

  nvs_teardown();
}

void test_sdf_storage_admission_empty_store_reads_as_zero_count(void) {
  nvs_setup();

  sdf_storage_admission_t entries[SDF_STORAGE_ADMISSION_MAX];
  size_t count = (size_t)-1;

  // Virgin namespace: no records, not an error.
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_load_all(entries, SDF_STORAGE_ADMISSION_MAX, &count));
  TEST_ASSERT_EQUAL(0, count);

  // Namespace created by an unrelated write, still no records.
  uint8_t key_to_save[32];
  memset(key_to_save, 0xEE, 32);
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_nuki_save(7, key_to_save));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_load_all(entries, SDF_STORAGE_ADMISSION_MAX, &count));
  TEST_ASSERT_EQUAL(0, count);

  nvs_teardown();
}

void test_sdf_storage_admission_clear_all(void) {
  nvs_setup();

  uint8_t a1[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t a2[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(0, a1));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(1, a2));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_clear_all());

  sdf_storage_admission_t entries[SDF_STORAGE_ADMISSION_MAX];
  size_t count = (size_t)-1;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_load_all(entries, SDF_STORAGE_ADMISSION_MAX, &count));
  TEST_ASSERT_EQUAL(0, count);

  // Clearing an empty store is not an error.
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_clear_all());

  nvs_teardown();
}

void test_sdf_storage_admission_invalid_args(void) {
  nvs_setup();

  uint8_t addr[6] = {0};
  sdf_storage_admission_t entry;
  size_t count;

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_admission_add(0, NULL));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_admission_remove(0, NULL));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_admission_load_all(NULL, 1, &count));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_admission_load_all(&entry, 1, NULL));

  nvs_teardown();
}

/* Crash-safety ordering (device-setup-phase): an interruption after the
 * admission record is written but before the latch leaves the device in the
 * setup phase - the latch read stays false with only an admission present,
 * so a reboot lands back in the wizard and completion can be retried. */
void test_sdf_storage_admission_without_latch_leaves_device_in_setup_phase(void) {
  nvs_setup();

  uint8_t addr[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_admission_add(1, addr));

  bool complete = true;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_setup_complete_load(&complete));
  TEST_ASSERT_FALSE(complete);

  nvs_teardown();
}

// -----------------------------------------------------------------------------
// Biometric-lockout latch (persist-biometric-lockout)
// -----------------------------------------------------------------------------

/* The record is a flag, not a deadline, and its three states have to stay
 * distinguishable: armed, explicitly cleared, and never written. The first two
 * are what the match task writes at a lockout episode's two transitions; the
 * third is a fresh device. */
void test_sdf_storage_lockout_save_load_roundtrip(void) {
  nvs_setup();

  bool armed = false;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_lockout_save(true));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_lockout_load(&armed));
  TEST_ASSERT_TRUE(armed);

  /* Overwriting with false is a distinct state from clearing: the key still
   * exists, so the load reports ESP_OK rather than NOT_FOUND. */
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_lockout_save(false));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_lockout_load(&armed));
  TEST_ASSERT_FALSE(armed);

  nvs_teardown();
}

void test_sdf_storage_lockout_clear_makes_record_absent(void) {
  nvs_setup();

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_lockout_save(true));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_lockout_clear());

  bool armed = true;
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, sdf_storage_lockout_load(&armed));
  // The out-param is defined even on the error path - callers fail open on it.
  TEST_ASSERT_FALSE(armed);

  // Clearing an absent record is not an error.
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_lockout_clear());

  nvs_teardown();
}

/* A device that has never locked out has no key at all, and that must read as
 * NOT_FOUND rather than as a read failure - sdf_services_restore_lockout_locked()
 * logs a warning for the latter and stays silent for the former. */
void test_sdf_storage_lockout_load_absent_on_fresh_device(void) {
  nvs_setup();

  bool armed = true;
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, sdf_storage_lockout_load(&armed));
  TEST_ASSERT_FALSE(armed);

  nvs_teardown();
}

void test_sdf_storage_lockout_load_rejects_null(void) {
  nvs_setup();
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_storage_lockout_load(NULL));
  nvs_teardown();
}

/* Factory reset must not leave a device locked out: erase_all() wipes the whole
 * NVS partition, so the latch goes with it. Asserted rather than assumed,
 * because a future key moved to a second namespace would silently survive. */
void test_sdf_storage_lockout_cleared_by_erase_all(void) {
  nvs_setup();

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_lockout_save(true));
  bool armed = false;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_lockout_load(&armed));
  TEST_ASSERT_TRUE(armed);

  TEST_ASSERT_EQUAL(ESP_OK, sdf_storage_erase_all());

  armed = true;
  TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, sdf_storage_lockout_load(&armed));
  TEST_ASSERT_FALSE(armed);

  nvs_teardown();
}

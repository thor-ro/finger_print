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

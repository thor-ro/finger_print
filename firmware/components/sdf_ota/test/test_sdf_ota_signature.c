#include "unity.h"

#include "sdf_ota.h"

// -----------------------------------------------------------------------------
// Unit Tests
// -----------------------------------------------------------------------------

/* test_runner (and this project's default Kconfig) builds with
 * CONFIG_SDF_OTA_SIGNATURE_VERIFY=n, which selects sdf_ota_signature.c's
 * no-op #else branch: sdf_ota_verify_signature() returns ESP_OK
 * unconditionally, without touching `partition` or `image_size`. Pin that
 * contract here so it doesn't silently change; the real Ed25519 branch
 * (CONFIG_SDF_OTA_SIGNATURE_VERIFY=y) is out of scope - see this change's
 * design.md Open Questions for the pre-existing include gap that blocks it. */
void test_sdf_ota_verify_signature_default_config_returns_ok(void) {
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_verify_signature(NULL, 0));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_verify_signature(NULL, 12345));
}

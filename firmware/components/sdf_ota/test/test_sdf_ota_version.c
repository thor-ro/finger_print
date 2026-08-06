#include "unity.h"

#include "sdf_ota.h"

// -----------------------------------------------------------------------------
// Unit Tests
// -----------------------------------------------------------------------------

/* sdf_ota_version_compare(current, incoming) reports how `incoming` relates
 * to `current`: SDF_OTA_VERSION_NEWER means incoming > current, OLDER means
 * incoming < current. */

void test_sdf_ota_version_compare_equal(void) {
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_EQUAL, sdf_ota_version_compare("1.2.3", "1.2.3"));
}

void test_sdf_ota_version_compare_leading_v_ignored(void) {
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_EQUAL, sdf_ota_version_compare("v1.2.3", "1.2.3"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_EQUAL, sdf_ota_version_compare("1.2.3", "V1.2.3"));
}

void test_sdf_ota_version_compare_major_newer_and_older(void) {
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_NEWER, sdf_ota_version_compare("1.0.0", "2.0.0"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_OLDER, sdf_ota_version_compare("2.0.0", "1.0.0"));
}

void test_sdf_ota_version_compare_minor_newer_and_older(void) {
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_NEWER, sdf_ota_version_compare("1.1.0", "1.2.0"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_OLDER, sdf_ota_version_compare("1.2.0", "1.1.0"));
}

void test_sdf_ota_version_compare_patch_newer_and_older(void) {
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_NEWER, sdf_ota_version_compare("1.2.3", "1.2.4"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_OLDER, sdf_ota_version_compare("1.2.4", "1.2.3"));
}

void test_sdf_ota_version_compare_release_newer_than_pre_release(void) {
  /* Same major.minor.patch: a release outranks a pre-release of itself. */
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_NEWER, sdf_ota_version_compare("1.0.0-beta", "1.0.0"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_OLDER, sdf_ota_version_compare("1.0.0", "1.0.0-beta"));
}

void test_sdf_ota_version_compare_pre_release_alphanumeric_order(void) {
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_NEWER, sdf_ota_version_compare("1.0.0-alpha", "1.0.0-beta"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_OLDER, sdf_ota_version_compare("1.0.0-beta", "1.0.0-alpha"));
}

void test_sdf_ota_version_compare_build_metadata_ignored(void) {
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_EQUAL, sdf_ota_version_compare("1.2.3+build1", "1.2.3+build2"));
}

void test_sdf_ota_version_compare_malformed_input_returns_equal(void) {
  /* parse_semver() rejects both, so sdf_ota_version_compare() falls back to
   * its "can't tell" default rather than treating one side as more/less than
   * the other. */
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_EQUAL, sdf_ota_version_compare("not-a-version", "1.2.3"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_EQUAL, sdf_ota_version_compare("1.2.3", "not-a-version"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_EQUAL, sdf_ota_version_compare("1.2", "1.2.3"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_EQUAL, sdf_ota_version_compare(NULL, "1.2.3"));
  TEST_ASSERT_EQUAL(SDF_OTA_VERSION_EQUAL, sdf_ota_version_compare("1.2.3", NULL));
}

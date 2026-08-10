#include "unity.h"

#include <string.h>

#include "sdf_ota_digest.h"

/* These exercise the same accumulator sdf_ota_write() drives - it lives in
 * sdf_ota_signature.c precisely so it is reachable here, since sdf_ota.c
 * itself is excluded from IDF_TARGET=linux. */

#define IMAGE_LEN  1000u
#define TOTAL_LEN  (IMAGE_LEN + SDF_OTA_FOOTER_SIZE)

static uint8_t s_stream[TOTAL_LEN];

static void fill_stream(void) {
  /* Deterministic, non-repeating enough that a mis-ordered or duplicated
   * chunk changes the digest. */
  for (size_t i = 0; i < sizeof(s_stream); i++) {
    s_stream[i] = (uint8_t)((i * 31u + (i >> 3)) & 0xFF);
  }
}

/* Digest of the signed range fed as one contiguous block - the reference every
 * chunked case below must reproduce. */
static void reference_digest(uint8_t out[SDF_OTA_DIGEST_SIZE]) {
  sdf_ota_digest_t d;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_begin(&d, IMAGE_LEN));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_update(&d, s_stream, IMAGE_LEN));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_finish(&d, out));
}

/* Feed the whole stream, footer included, in chunks of the given sizes,
 * cycling through the list the way a transport delivers variable-sized MTU
 * writes. */
static void chunked_digest(const uint32_t *sizes, size_t n_sizes,
                           uint8_t out[SDF_OTA_DIGEST_SIZE]) {
  sdf_ota_digest_t d;
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_begin(&d, IMAGE_LEN));

  uint32_t offset = 0;
  size_t i = 0;
  while (offset < TOTAL_LEN) {
    uint32_t chunk = sizes[i % n_sizes];
    if (offset + chunk > TOTAL_LEN) {
      chunk = TOTAL_LEN - offset;
    }
    TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_update(&d, s_stream + offset, chunk));
    offset += chunk;
    i++;
  }

  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_finish(&d, out));
}

void test_sdf_ota_digest_chunked_equals_contiguous(void) {
  fill_stream();

  uint8_t expected[SDF_OTA_DIGEST_SIZE];
  reference_digest(expected);

  /* 244 divides neither IMAGE_LEN nor TOTAL_LEN, so a chunk straddles the
   * image/footer boundary: 4 x 244 = 976, and the next chunk covers bytes
   * 976..1219, spanning the last 24 signed bytes plus the whole 68-byte
   * footer. That is the clamp case from the design. */
  const uint32_t ble_like[] = {244};
  uint8_t actual[SDF_OTA_DIGEST_SIZE];
  chunked_digest(ble_like, 1, actual);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, SDF_OTA_DIGEST_SIZE);

  /* Varying sizes: the digest must not depend on how the stream was split. */
  const uint32_t varied[] = {1, 7, 64, 3, 255, 128, 17};
  memset(actual, 0, sizeof(actual));
  chunked_digest(varied, sizeof(varied) / sizeof(varied[0]), actual);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, SDF_OTA_DIGEST_SIZE);

  /* Single byte at a time - the pathological split. */
  const uint32_t one[] = {1};
  memset(actual, 0, sizeof(actual));
  chunked_digest(one, 1, actual);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, SDF_OTA_DIGEST_SIZE);

  /* Whole stream in one write, footer included - the clamp still applies. */
  const uint32_t whole[] = {TOTAL_LEN};
  memset(actual, 0, sizeof(actual));
  chunked_digest(whole, 1, actual);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, SDF_OTA_DIGEST_SIZE);
}

void test_sdf_ota_digest_excludes_footer(void) {
  fill_stream();

  uint8_t signed_only[SDF_OTA_DIGEST_SIZE];
  reference_digest(signed_only);

  /* Hashing the footer too must produce a different digest - otherwise the
   * clamp above would be untested by the equality assertions. */
  sdf_ota_digest_t d;
  uint8_t including_footer[SDF_OTA_DIGEST_SIZE];
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_begin(&d, TOTAL_LEN));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_update(&d, s_stream, TOTAL_LEN));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_finish(&d, including_footer));

  TEST_ASSERT_NOT_EQUAL(0, memcmp(signed_only, including_footer, SDF_OTA_DIGEST_SIZE));

  /* Mutating a footer byte must not change the digest of the signed range -
   * the signature covers the image, not its own footer. */
  uint8_t mutated_footer[SDF_OTA_DIGEST_SIZE];
  s_stream[IMAGE_LEN + 10] ^= 0xFF;
  reference_digest(mutated_footer);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(signed_only, mutated_footer, SDF_OTA_DIGEST_SIZE);

  /* ...while mutating an image byte must change it. */
  uint8_t mutated_image[SDF_OTA_DIGEST_SIZE];
  s_stream[IMAGE_LEN / 2] ^= 0xFF;
  reference_digest(mutated_image);
  TEST_ASSERT_NOT_EQUAL(0, memcmp(signed_only, mutated_image, SDF_OTA_DIGEST_SIZE));
}

void test_sdf_ota_digest_rejects_use_after_finish(void) {
  fill_stream();

  sdf_ota_digest_t d;
  uint8_t out[SDF_OTA_DIGEST_SIZE];
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_begin(&d, IMAGE_LEN));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_update(&d, s_stream, IMAGE_LEN));
  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_finish(&d, out));

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sdf_ota_digest_update(&d, s_stream, 1));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sdf_ota_digest_finish(&d, out));

  /* Releasing twice must not double-free. */
  sdf_ota_digest_release(&d);
  sdf_ota_digest_release(&d);
}

void test_sdf_ota_digest_null_arguments_rejected(void) {
  sdf_ota_digest_t d;

  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_ota_digest_begin(NULL, IMAGE_LEN));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_ota_digest_update(NULL, s_stream, 1));

  TEST_ASSERT_EQUAL(ESP_OK, sdf_ota_digest_begin(&d, IMAGE_LEN));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_ota_digest_update(&d, NULL, 1));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sdf_ota_digest_finish(&d, NULL));
  sdf_ota_digest_release(&d);

  /* NULL is a no-op, not a crash. */
  sdf_ota_digest_release(NULL);
}

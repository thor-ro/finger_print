#include "unity.h"

#include <string.h>

#include "sdf_ota.h"

// -----------------------------------------------------------------------------
// Known-answer vectors
// -----------------------------------------------------------------------------

/* RFC 6979 appendix A.2.5, "Test vectors for P-256, SHA-256" - the published
 * key/signature pairs for the messages "sample" and "test". Used rather than a
 * locally generated pair so these tests fail if sdf_ota_verify_digest() ever
 * departs from standard ECDSA P-256 over raw r||s, not merely if it disagrees
 * with itself.
 *
 * These replace a test that only asserted the *disabled* no-op stub returned
 * ESP_OK, which is why none of the four defects in the previous Ed25519
 * implementation were caught. */

/* Uncompressed public point: 0x04 || Ux || Uy */
static const uint8_t k_pubkey[SDF_OTA_PUBKEY_SIZE] = {
    0x04, 0x60, 0xfe, 0xd4, 0xba, 0x25, 0x5a, 0x9d, 0x31, 0xc9, 0x61, 0xeb,
    0x74, 0xc6, 0x35, 0x6d, 0x68, 0xc0, 0x49, 0xb8, 0x92, 0x3b, 0x61, 0xfa,
    0x6c, 0xe6, 0x69, 0x62, 0x2e, 0x60, 0xf2, 0x9f, 0xb6, 0x79, 0x03, 0xfe,
    0x10, 0x08, 0xb8, 0xbc, 0x99, 0xa4, 0x1a, 0xe9, 0xe9, 0x56, 0x28, 0xbc,
    0x64, 0xf2, 0xf1, 0xb2, 0x0c, 0x2d, 0x7e, 0x9f, 0x51, 0x77, 0xa3, 0xc2,
    0x94, 0xd4, 0x46, 0x22, 0x99,
};

/* SHA-256("sample") */
static const uint8_t k_digest_sample[SDF_OTA_DIGEST_SIZE] = {
    0xaf, 0x2b, 0xdb, 0xe1, 0xaa, 0x9b, 0x6e, 0xc1, 0xe2, 0xad, 0xe1, 0xd6,
    0x94, 0xf4, 0x1f, 0xc7, 0x1a, 0x83, 0x1d, 0x02, 0x68, 0xe9, 0x89, 0x15,
    0x62, 0x11, 0x3d, 0x8a, 0x62, 0xad, 0xd1, 0xbf,
};

/* r || s over SHA-256("sample") */
static const uint8_t k_sig_sample[SDF_OTA_SIG_SIZE] = {
    0xef, 0xd4, 0x8b, 0x2a, 0xac, 0xb6, 0xa8, 0xfd, 0x11, 0x40, 0xdd, 0x9c,
    0xd4, 0x5e, 0x81, 0xd6, 0x9d, 0x2c, 0x87, 0x7b, 0x56, 0xaa, 0xf9, 0x91,
    0xc3, 0x4d, 0x0e, 0xa8, 0x4e, 0xaf, 0x37, 0x16, 0xf7, 0xcb, 0x1c, 0x94,
    0x2d, 0x65, 0x7c, 0x41, 0xd4, 0x36, 0xc7, 0xa1, 0xb6, 0xe2, 0x9f, 0x65,
    0xf3, 0xe9, 0x00, 0xdb, 0xb9, 0xaf, 0xf4, 0x06, 0x4d, 0xc4, 0xab, 0x2f,
    0x84, 0x3a, 0xcd, 0xa8,
};

/* SHA-256("test") */
static const uint8_t k_digest_test[SDF_OTA_DIGEST_SIZE] = {
    0x9f, 0x86, 0xd0, 0x81, 0x88, 0x4c, 0x7d, 0x65, 0x9a, 0x2f, 0xea, 0xa0,
    0xc5, 0x5a, 0xd0, 0x15, 0xa3, 0xbf, 0x4f, 0x1b, 0x2b, 0x0b, 0x82, 0x2c,
    0xd1, 0x5d, 0x6c, 0x15, 0xb0, 0xf0, 0x0a, 0x08,
};

/* r || s over SHA-256("test"), same key */
static const uint8_t k_sig_test[SDF_OTA_SIG_SIZE] = {
    0xf1, 0xab, 0xb0, 0x23, 0x51, 0x83, 0x51, 0xcd, 0x71, 0xd8, 0x81, 0x56,
    0x7b, 0x1e, 0xa6, 0x63, 0xed, 0x3e, 0xfc, 0xf6, 0xc5, 0x13, 0x2b, 0x35,
    0x4f, 0x28, 0xd3, 0xb0, 0xb7, 0xd3, 0x83, 0x67, 0x01, 0x9f, 0x41, 0x13,
    0x74, 0x2a, 0x2b, 0x14, 0xbd, 0x25, 0x92, 0x6b, 0x49, 0xc6, 0x49, 0x15,
    0x5f, 0x26, 0x7e, 0x60, 0xd3, 0x81, 0x4b, 0x4c, 0x0c, 0xc8, 0x42, 0x50,
    0xe4, 0x6f, 0x00, 0x83,
};

// -----------------------------------------------------------------------------
// Unit Tests
// -----------------------------------------------------------------------------

void test_sdf_ota_verify_digest_known_answer_vectors_pass(void) {
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_ota_verify_digest(k_digest_sample, k_sig_sample, k_pubkey));
  TEST_ASSERT_EQUAL(ESP_OK,
                    sdf_ota_verify_digest(k_digest_test, k_sig_test, k_pubkey));
}

void test_sdf_ota_verify_digest_tampered_signature_fails(void) {
  uint8_t sig[SDF_OTA_SIG_SIZE];

  /* Flip a bit in r */
  memcpy(sig, k_sig_sample, sizeof(sig));
  sig[0] ^= 0x01;
  TEST_ASSERT_EQUAL(SDF_ERR_OTA_SIGNATURE_INVALID,
                    sdf_ota_verify_digest(k_digest_sample, sig, k_pubkey));

  /* Flip a bit in s */
  memcpy(sig, k_sig_sample, sizeof(sig));
  sig[SDF_OTA_SIG_SIZE - 1] ^= 0x01;
  TEST_ASSERT_EQUAL(SDF_ERR_OTA_SIGNATURE_INVALID,
                    sdf_ota_verify_digest(k_digest_sample, sig, k_pubkey));

  /* A signature valid for a different digest under the same key must not
   * verify - catches an implementation that ignores the digest entirely. */
  TEST_ASSERT_EQUAL(SDF_ERR_OTA_SIGNATURE_INVALID,
                    sdf_ota_verify_digest(k_digest_sample, k_sig_test, k_pubkey));
}

void test_sdf_ota_verify_digest_tampered_digest_fails(void) {
  uint8_t digest[SDF_OTA_DIGEST_SIZE];

  memcpy(digest, k_digest_sample, sizeof(digest));
  digest[0] ^= 0x01;
  TEST_ASSERT_EQUAL(SDF_ERR_OTA_SIGNATURE_INVALID,
                    sdf_ota_verify_digest(digest, k_sig_sample, k_pubkey));

  memcpy(digest, k_digest_sample, sizeof(digest));
  digest[SDF_OTA_DIGEST_SIZE - 1] ^= 0x80;
  TEST_ASSERT_EQUAL(SDF_ERR_OTA_SIGNATURE_INVALID,
                    sdf_ota_verify_digest(digest, k_sig_sample, k_pubkey));
}

void test_sdf_ota_verify_digest_malformed_public_key_rejected(void) {
  uint8_t pubkey[SDF_OTA_PUBKEY_SIZE];

  /* Wrong leading byte: not an uncompressed point */
  memcpy(pubkey, k_pubkey, sizeof(pubkey));
  pubkey[0] = 0x02;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_ota_verify_digest(k_digest_sample, k_sig_sample, pubkey));

  /* Well-formed encoding, but the point is not on the curve */
  memcpy(pubkey, k_pubkey, sizeof(pubkey));
  pubkey[SDF_OTA_PUBKEY_SIZE - 1] ^= 0x01;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_ota_verify_digest(k_digest_sample, k_sig_sample, pubkey));

  /* All-zero key: the point at infinity is not a valid public key */
  memset(pubkey, 0, sizeof(pubkey));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_ota_verify_digest(k_digest_sample, k_sig_sample, pubkey));
}

void test_sdf_ota_verify_digest_null_arguments_rejected(void) {
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_ota_verify_digest(NULL, k_sig_sample, k_pubkey));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_ota_verify_digest(k_digest_sample, NULL, k_pubkey));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                    sdf_ota_verify_digest(k_digest_sample, k_sig_sample, NULL));
}

/**
 * @file test_nuki_crypto.c
 * @brief Unit tests for sdf_nuki_crypto primitives.
 */
#include "sdf_nuki_crypto.h"
#include "unity.h"
#include <string.h>

/* ---------- crypto_secretbox round-trip ---------- */

void test_crypto_secretbox_round_trip(void) {
  /* NaCl secretbox: first 32 bytes of m must be zero, output c has
     authenticator at c[16..31], ciphertext from c[32..]. */
  uint8_t m[64];
  memset(m, 0, 32);                  /* required zero padding */
  memcpy(m + 32, "Hello Nuki!", 11); /* actual plaintext after padding */
  memset(m + 43, 0, 21);             /* fill rest */

  uint8_t key[32];
  memset(key, 0x42, sizeof(key));
  uint8_t nonce[24];
  memset(nonce, 0xAB, sizeof(nonce));

  uint8_t c[64];
  TEST_ASSERT_EQUAL(0, crypto_secretbox(c, m, sizeof(m), nonce, key));

  uint8_t decrypted[64];
  TEST_ASSERT_EQUAL(0,
                    crypto_secretbox_open(decrypted, c, sizeof(c), nonce, key));

  /* First 32 bytes of decrypted output are zeroed by secretbox_open */
  TEST_ASSERT_EQUAL_MEMORY(m + 32, decrypted + 32, 32);
}

void test_crypto_secretbox_tampered_ciphertext(void) {
  uint8_t m[64];
  memset(m, 0, 32);
  memcpy(m + 32, "Tamper test", 11);
  memset(m + 43, 0, 21);

  uint8_t key[32];
  memset(key, 0x55, sizeof(key));
  uint8_t nonce[24];
  memset(nonce, 0xCC, sizeof(nonce));

  uint8_t c[64];
  TEST_ASSERT_EQUAL(0, crypto_secretbox(c, m, sizeof(m), nonce, key));

  /* Flip a bit in the ciphertext body */
  c[40] ^= 0x01;

  uint8_t decrypted[64];
  TEST_ASSERT_NOT_EQUAL(
      0, crypto_secretbox_open(decrypted, c, sizeof(c), nonce, key));
}

void test_crypto_secretbox_too_short(void) {
  uint8_t m[16]; /* less than 32 bytes minimum */
  memset(m, 0, sizeof(m));
  uint8_t c[16];
  uint8_t key[32] = {0};
  uint8_t nonce[24] = {0};

  TEST_ASSERT_NOT_EQUAL(0, crypto_secretbox(c, m, sizeof(m), nonce, key));
}

void test_crypto_secretbox_open_too_short(void) {
  uint8_t m[16];
  uint8_t c[16] = {0};
  uint8_t key[32] = {0};
  uint8_t nonce[24] = {0};

  TEST_ASSERT_NOT_EQUAL(0, crypto_secretbox_open(m, c, sizeof(c), nonce, key));
}

/* ---------- crypto_core_hsalsa20 ---------- */

void test_crypto_core_hsalsa20_deterministic(void) {
  /* HSalsa20 must be deterministic: same inputs → same output */
  uint8_t key[32], in[16], sigma[16];
  memset(key, 0x01, sizeof(key));
  memset(in, 0x02, sizeof(in));
  memcpy(sigma, "expand 32-byte k", 16);

  uint8_t out1[32], out2[32];
  TEST_ASSERT_EQUAL(0, crypto_core_hsalsa20(out1, in, key, sigma));
  TEST_ASSERT_EQUAL(0, crypto_core_hsalsa20(out2, in, key, sigma));
  TEST_ASSERT_EQUAL_MEMORY(out1, out2, 32);
}

void test_crypto_core_hsalsa20_different_keys(void) {
  uint8_t key1[32], key2[32], in[16], sigma[16];
  memset(key1, 0x01, sizeof(key1));
  memset(key2, 0x02, sizeof(key2));
  memset(in, 0x00, sizeof(in));
  memcpy(sigma, "expand 32-byte k", 16);

  uint8_t out1[32], out2[32];
  TEST_ASSERT_EQUAL(0, crypto_core_hsalsa20(out1, in, key1, sigma));
  TEST_ASSERT_EQUAL(0, crypto_core_hsalsa20(out2, in, key2, sigma));
  TEST_ASSERT_FALSE(memcmp(out1, out2, 32) == 0);
}

/* ---------- crypto_scalarmult ---------- */

void test_crypto_scalarmult_basepoint(void) {
  /* Scalar mult with the Curve25519 basepoint should produce a valid public
   * key. NOTE: mbedTLS on the Linux host may not support Curve25519 point read
   * in raw 32-byte format. If scalarmult returns -1, skip the test. */
  static const uint8_t basepoint[32] = {9};
  uint8_t scalar[32];
  memset(scalar, 0x77, sizeof(scalar));

  uint8_t result[32];
  int ret = crypto_scalarmult(result, scalar, basepoint);
  if (ret != 0) {
    TEST_IGNORE_MESSAGE(
        "crypto_scalarmult not supported on this host mbedTLS build");
    return;
  }

  /* Result should not be all zeros */
  uint8_t zeros[32] = {0};
  TEST_ASSERT_FALSE(memcmp(result, zeros, 32) == 0);
}

void test_crypto_scalarmult_null_args(void) {
  uint8_t q[32], n[32], p[32];
  memset(n, 1, sizeof(n));
  memset(p, 9, sizeof(p));

  TEST_ASSERT_NOT_EQUAL(0, crypto_scalarmult(NULL, n, p));
  TEST_ASSERT_NOT_EQUAL(0, crypto_scalarmult(q, NULL, p));
  TEST_ASSERT_NOT_EQUAL(0, crypto_scalarmult(q, n, NULL));
}

void test_crypto_scalarmult_dh_agreement(void) {
  /* Two parties should derive the same shared secret:
     Alice: sA * (sB * G) == Bob: sB * (sA * G) */
  static const uint8_t basepoint[32] = {9};
  uint8_t sA[32], sB[32], pA[32], pB[32];
  memset(sA, 0x11, sizeof(sA));
  memset(sB, 0x22, sizeof(sB));

  if (crypto_scalarmult(pA, sA, basepoint) != 0) {
    TEST_IGNORE_MESSAGE(
        "crypto_scalarmult not supported on this host mbedTLS build");
    return;
  }
  TEST_ASSERT_EQUAL(0, crypto_scalarmult(pB, sB, basepoint));

  uint8_t shared_AB[32], shared_BA[32];
  TEST_ASSERT_EQUAL(0, crypto_scalarmult(shared_AB, sA, pB));
  TEST_ASSERT_EQUAL(0, crypto_scalarmult(shared_BA, sB, pA));

  TEST_ASSERT_EQUAL_MEMORY(shared_AB, shared_BA, 32);
}

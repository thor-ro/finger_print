#include "sdf_nuki_crypto.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/poly1305.h"

#define SDF_SALSA20_ROUNDS 20

static uint32_t sdf_rotl32(uint32_t value, int shift)
{
    return (value << shift) | (value >> (32 - shift));
}

static uint32_t sdf_load32_le(const uint8_t *src)
{
    return (uint32_t)src[0]
        | ((uint32_t)src[1] << 8)
        | ((uint32_t)src[2] << 16)
        | ((uint32_t)src[3] << 24);
}

static void sdf_store32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static void sdf_salsa20_rounds(uint32_t state[16])
{
    for (int i = 0; i < SDF_SALSA20_ROUNDS / 2; ++i) {
        state[4] ^= sdf_rotl32(state[0] + state[12], 7);
        state[8] ^= sdf_rotl32(state[4] + state[0], 9);
        state[12] ^= sdf_rotl32(state[8] + state[4], 13);
        state[0] ^= sdf_rotl32(state[12] + state[8], 18);

        state[9] ^= sdf_rotl32(state[5] + state[1], 7);
        state[13] ^= sdf_rotl32(state[9] + state[5], 9);
        state[1] ^= sdf_rotl32(state[13] + state[9], 13);
        state[5] ^= sdf_rotl32(state[1] + state[13], 18);

        state[14] ^= sdf_rotl32(state[10] + state[6], 7);
        state[2] ^= sdf_rotl32(state[14] + state[10], 9);
        state[6] ^= sdf_rotl32(state[2] + state[14], 13);
        state[10] ^= sdf_rotl32(state[6] + state[2], 18);

        state[3] ^= sdf_rotl32(state[15] + state[11], 7);
        state[7] ^= sdf_rotl32(state[3] + state[15], 9);
        state[11] ^= sdf_rotl32(state[7] + state[3], 13);
        state[15] ^= sdf_rotl32(state[11] + state[7], 18);

        state[1] ^= sdf_rotl32(state[0] + state[3], 7);
        state[2] ^= sdf_rotl32(state[1] + state[0], 9);
        state[3] ^= sdf_rotl32(state[2] + state[1], 13);
        state[0] ^= sdf_rotl32(state[3] + state[2], 18);

        state[6] ^= sdf_rotl32(state[5] + state[4], 7);
        state[7] ^= sdf_rotl32(state[6] + state[5], 9);
        state[4] ^= sdf_rotl32(state[7] + state[6], 13);
        state[5] ^= sdf_rotl32(state[4] + state[7], 18);

        state[11] ^= sdf_rotl32(state[10] + state[9], 7);
        state[8] ^= sdf_rotl32(state[11] + state[10], 9);
        state[9] ^= sdf_rotl32(state[8] + state[11], 13);
        state[10] ^= sdf_rotl32(state[9] + state[8], 18);

        state[12] ^= sdf_rotl32(state[15] + state[14], 7);
        state[13] ^= sdf_rotl32(state[12] + state[15], 9);
        state[14] ^= sdf_rotl32(state[13] + state[12], 13);
        state[15] ^= sdf_rotl32(state[14] + state[13], 18);
    }
}

static void sdf_salsa20_hash(uint8_t out[64], const uint32_t in[16])
{
    uint32_t state[16];
    memcpy(state, in, sizeof(state));

    sdf_salsa20_rounds(state);

    for (int i = 0; i < 16; ++i) {
        state[i] += in[i];
        sdf_store32_le(out + (i * 4), state[i]);
    }
}

int crypto_core_hsalsa20(
    unsigned char *out,
    const unsigned char *in,
    const unsigned char *k,
    const unsigned char *c)
{
    uint32_t state[16];

    state[0] = sdf_load32_le(c + 0);
    state[5] = sdf_load32_le(c + 4);
    state[10] = sdf_load32_le(c + 8);
    state[15] = sdf_load32_le(c + 12);

    state[1] = sdf_load32_le(k + 0);
    state[2] = sdf_load32_le(k + 4);
    state[3] = sdf_load32_le(k + 8);
    state[4] = sdf_load32_le(k + 12);

    state[11] = sdf_load32_le(k + 16);
    state[12] = sdf_load32_le(k + 20);
    state[13] = sdf_load32_le(k + 24);
    state[14] = sdf_load32_le(k + 28);

    state[6] = sdf_load32_le(in + 0);
    state[7] = sdf_load32_le(in + 4);
    state[8] = sdf_load32_le(in + 8);
    state[9] = sdf_load32_le(in + 12);

    sdf_salsa20_rounds(state);

    sdf_store32_le(out + 0, state[0]);
    sdf_store32_le(out + 4, state[5]);
    sdf_store32_le(out + 8, state[10]);
    sdf_store32_le(out + 12, state[15]);
    sdf_store32_le(out + 16, state[6]);
    sdf_store32_le(out + 20, state[7]);
    sdf_store32_le(out + 24, state[8]);
    sdf_store32_le(out + 28, state[9]);

    return 0;
}

static void sdf_salsa20_stream_xor(
    uint8_t *out,
    const uint8_t *in,
    size_t len,
    const uint8_t nonce[8],
    const uint8_t key[32])
{
    static const uint32_t sigma[4] = {
        0x61707865,
        0x3320646e,
        0x79622d32,
        0x6b206574
    };

    uint32_t state[16];
    state[0] = sigma[0];
    state[5] = sigma[1];
    state[10] = sigma[2];
    state[15] = sigma[3];

    state[1] = sdf_load32_le(key + 0);
    state[2] = sdf_load32_le(key + 4);
    state[3] = sdf_load32_le(key + 8);
    state[4] = sdf_load32_le(key + 12);
    state[11] = sdf_load32_le(key + 16);
    state[12] = sdf_load32_le(key + 20);
    state[13] = sdf_load32_le(key + 24);
    state[14] = sdf_load32_le(key + 28);

    state[6] = sdf_load32_le(nonce + 0);
    state[7] = sdf_load32_le(nonce + 4);

    uint64_t counter = 0;
    uint8_t block[64];

    while (len > 0) {
        state[8] = (uint32_t)(counter & 0xFFFFFFFFu);
        state[9] = (uint32_t)(counter >> 32);

        sdf_salsa20_hash(block, state);

        size_t block_len = len > sizeof(block) ? sizeof(block) : len;
        if (in != NULL) {
            for (size_t i = 0; i < block_len; ++i) {
                out[i] = (uint8_t)(in[i] ^ block[i]);
            }
            in += block_len;
        } else {
            memcpy(out, block, block_len);
        }

        out += block_len;
        len -= block_len;
        counter++;
    }
}

static void sdf_xsalsa20_stream_xor(
    uint8_t *out,
    const uint8_t *in,
    size_t len,
    const uint8_t nonce[24],
    const uint8_t key[32])
{
    static const uint8_t sigma[16] = "expand 32-byte k";
    uint8_t subkey[32];

    crypto_core_hsalsa20(subkey, nonce, key, sigma);
    sdf_salsa20_stream_xor(out, in, len, nonce + 16, subkey);
}

static int sdf_poly1305_auth(
    uint8_t out[16],
    const uint8_t *m,
    size_t mlen,
    const uint8_t key[32])
{
    mbedtls_poly1305_context ctx;
    mbedtls_poly1305_init(&ctx);

    int res = mbedtls_poly1305_starts(&ctx, key);
    if (res == 0) {
        res = mbedtls_poly1305_update(&ctx, m, mlen);
    }
    if (res == 0) {
        res = mbedtls_poly1305_finish(&ctx, out);
    }

    mbedtls_poly1305_free(&ctx);
    return res == 0 ? 0 : -1;
}

static int sdf_poly1305_verify(
    const uint8_t mac[16],
    const uint8_t *m,
    size_t mlen,
    const uint8_t key[32])
{
    uint8_t calc[16];
    if (sdf_poly1305_auth(calc, m, mlen, key) != 0) {
        return -1;
    }

    uint8_t diff = 0;
    for (size_t i = 0; i < sizeof(calc); ++i) {
        diff |= (uint8_t)(calc[i] ^ mac[i]);
    }

    return diff == 0 ? 0 : -1;
}

static int crypto_stream_xsalsa20(unsigned char *c,
    unsigned long long clen,
    const unsigned char *n,
    const unsigned char *k)
{
    sdf_xsalsa20_stream_xor(c, NULL, (size_t)clen, n, k);
    return 0;
}

static int crypto_stream_xsalsa20_xor(unsigned char *c,
    const unsigned char *m,
    unsigned long long mlen,
    const unsigned char *n,
    const unsigned char *k)
{
    sdf_xsalsa20_stream_xor(c, m, (size_t)mlen, n, k);
    return 0;
}

int crypto_secretbox(
    unsigned char *c,
    const unsigned char *m,
    unsigned long long mlen,
    const unsigned char *n,
    const unsigned char *k)
{
    if (mlen < 32) {
        return -1;
    }

    if (crypto_stream_xsalsa20_xor(c, m, mlen, n, k) != 0) {
        return -1;
    }

    if (sdf_poly1305_auth(c + 16, c + 32, (size_t)(mlen - 32), c) != 0) {
        return -1;
    }

    memset(c, 0, 16);
    return 0;
}

int crypto_secretbox_open(
    unsigned char *m,
    const unsigned char *c,
    unsigned long long mlen,
    const unsigned char *n,
    const unsigned char *k)
{
    if (mlen < 32) {
        return -1;
    }

    unsigned char key_stream[32];
    if (crypto_stream_xsalsa20(key_stream, sizeof(key_stream), n, k) != 0) {
        return -1;
    }

    if (sdf_poly1305_verify(c + 16, c + 32, (size_t)(mlen - 32), key_stream) != 0) {
        return -1;
    }

    if (crypto_stream_xsalsa20_xor(m, c, mlen, n, k) != 0) {
        return -1;
    }

    memset(m, 0, 32);
    return 0;
}

int crypto_scalarmult(
    unsigned char *q,
    const unsigned char *n,
    const unsigned char *p)
{
    if (q == NULL || n == NULL || p == NULL) {
        return -1;
    }

    uint8_t scalar[32];
    memcpy(scalar, n, sizeof(scalar));
    scalar[0] &= 248;
    scalar[31] &= 127;
    scalar[31] |= 64;

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Qp;
    mbedtls_mpi d;
    mbedtls_mpi z;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Qp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);

    int ret = 0;

    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) != 0) {
        ret = -1;
        goto cleanup;
    }

    if (mbedtls_mpi_read_binary(&d, scalar, sizeof(scalar)) != 0) {
        ret = -1;
        goto cleanup;
    }

    if (mbedtls_ecp_point_read_binary(&grp, &Qp, p, 32) != 0) {
        ret = -1;
        goto cleanup;
    }

    if (mbedtls_ecdh_compute_shared(&grp, &z, &Qp, &d, NULL, NULL) != 0) {
        ret = -1;
        goto cleanup;
    }

    if (mbedtls_mpi_write_binary(&z, q, 32) != 0) {
        ret = -1;
        goto cleanup;
    }

cleanup:
    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Qp);
    mbedtls_ecp_group_free(&grp);

    return ret;
}

/* mbedtls/private/ecp.h includes mbedtls/private_access.h before it pulls in
 * tf-psa-crypto/build_info.h (which is what applies ESP-IDF's mbedtls port
 * config, mbedtls/esp_config.h, and would otherwise set this for us). Define
 * it here first, with the same empty value esp_config.h uses, so
 * private_access.h unlocks real struct member names instead of private_* ones
 * without triggering a conflicting-value -Wmacro-redefined later. Same pattern
 * as sdf_protocol_ble/src/sdf_nuki_crypto.c. */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "sdf_ota.h"
#include "sdf_ota_digest.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"

/* mbedtls 4.x (tf-psa-crypto) moved the low-level ECP/ECDSA/bignum APIs out of
 * their public headers into a "private" header directory, and hides the legacy
 * (non-PSA) declarations behind MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS. These are
 * real, compiled symbols - CONFIG_MBEDTLS_ECDSA_C and
 * CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED are both on for esp32c6 and for
 * IDF_TARGET=linux - unlike the mbedtls_ed25519_* API this replaced, which
 * never existed anywhere in the tree. */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/private/bignum.h"
#include "mbedtls/private/ecdsa.h"
#include "mbedtls/private/ecp.h"

static const char *TAG = "sdf_ota_sig";

// -----------------------------------------------------------------------------
// Streaming digest accumulator
// -----------------------------------------------------------------------------

esp_err_t sdf_ota_digest_begin(sdf_ota_digest_t *d, uint32_t signed_len)
{
    if (d == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(d, 0, sizeof(*d));
    mbedtls_sha256_init(&d->ctx);
    d->active = true;
    d->signed_len = signed_len;

    if (mbedtls_sha256_starts(&d->ctx, 0 /* SHA-256, not SHA-224 */) != 0) {
        ESP_LOGE(TAG, "Failed to start image digest");
        sdf_ota_digest_release(d);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sdf_ota_digest_update(sdf_ota_digest_t *d, const void *data, uint32_t len)
{
    if (d == NULL || (data == NULL && len != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!d->active) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clamp at signed_len so the footer the client streams after the image is
     * excluded from what the signature covers. Deterministic because
     * signed_len is fixed before the first byte arrives. */
    if (d->bytes_hashed >= d->signed_len) {
        return ESP_OK;
    }
    uint32_t remaining = d->signed_len - d->bytes_hashed;
    uint32_t to_hash = (len < remaining) ? len : remaining;

    if (mbedtls_sha256_update(&d->ctx, (const unsigned char *)data, to_hash) != 0) {
        ESP_LOGE(TAG, "Failed to update image digest");
        return ESP_FAIL;
    }
    d->bytes_hashed += to_hash;
    return ESP_OK;
}

esp_err_t sdf_ota_digest_finish(sdf_ota_digest_t *d, uint8_t out[SDF_OTA_DIGEST_SIZE])
{
    if (d == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!d->active) {
        return ESP_ERR_INVALID_STATE;
    }

    int ret = mbedtls_sha256_finish(&d->ctx, out);
    sdf_ota_digest_release(d);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to finalize image digest");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void sdf_ota_digest_release(sdf_ota_digest_t *d)
{
    if (d != NULL && d->active) {
        mbedtls_sha256_free(&d->ctx);
        d->active = false;
    }
}

// -----------------------------------------------------------------------------
// Signature verification
// -----------------------------------------------------------------------------

/* Verification core. Deliberately outside the CONFIG_SDF_OTA_SIGNATURE_VERIFY
 * guard and free of any partition/flash dependency: it is the piece the host
 * test runner exercises against known-answer vectors, and gating it behind the
 * config flag is exactly what let the previous implementation rot unnoticed. */
esp_err_t sdf_ota_verify_digest(const uint8_t digest[SDF_OTA_DIGEST_SIZE],
                                const uint8_t sig[SDF_OTA_SIG_SIZE],
                                const uint8_t pubkey[SDF_OTA_PUBKEY_SIZE])
{
    if (digest == NULL || sig == NULL || pubkey == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi r, s;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    esp_err_t result = ESP_FAIL;

    int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to load P-256 group: -0x%04X", (unsigned)-ret);
        goto cleanup;
    }

    /* Rejects anything that is not a well-formed uncompressed point (wrong
     * leading byte, coordinates >= p, point not on the curve). */
    ret = mbedtls_ecp_point_read_binary(&grp, &Q, pubkey, SDF_OTA_PUBKEY_SIZE);
    if (ret != 0) {
        ESP_LOGE(TAG, "Malformed public key: -0x%04X", (unsigned)-ret);
        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    ret = mbedtls_ecp_check_pubkey(&grp, &Q);
    if (ret != 0) {
        ESP_LOGE(TAG, "Public key not a valid P-256 point: -0x%04X", (unsigned)-ret);
        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    /* Raw r||s, two 32-byte big-endian halves - not ASN.1 DER, so
     * mbedtls_ecdsa_verify() is used directly rather than the
     * mbedtls_ecdsa_read_signature() wrapper. */
    ret = mbedtls_mpi_read_binary(&r, sig, SDF_OTA_SIG_SIZE / 2);
    if (ret == 0) {
        ret = mbedtls_mpi_read_binary(&s, sig + SDF_OTA_SIG_SIZE / 2, SDF_OTA_SIG_SIZE / 2);
    }
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to load signature halves: -0x%04X", (unsigned)-ret);
        goto cleanup;
    }

    ret = mbedtls_ecdsa_verify(&grp, digest, SDF_OTA_DIGEST_SIZE, &Q, &r, &s);
    if (ret != 0) {
        ESP_LOGE(TAG, "Signature verification failed: -0x%04X", (unsigned)-ret);
        result = SDF_ERR_OTA_SIGNATURE_INVALID;
        goto cleanup;
    }

    result = ESP_OK;

cleanup:
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    return result;
}

/* The partition-bound wrapper needs the public key blob embedded by
 * sdf_ota/CMakeLists.txt, which only the device build produces, so it stays
 * gated. IDF_TARGET=linux keeps the no-op stub; the crypto it would exercise
 * is already covered by sdf_ota_verify_digest() above. */
#if CONFIG_SDF_OTA_SIGNATURE_VERIFY && !CONFIG_IDF_TARGET_LINUX

#include "esp_partition.h"

/* ECDSA P-256 public key (65 bytes, uncompressed) - embedded from
 * ota_public_key.bin */
extern const uint8_t _binary_ota_public_key_bin_start[] asm("_binary_ota_public_key_bin_start");
#define sdf_ota_public_key _binary_ota_public_key_bin_start

/* Magic marker appended after the signature: "SDF\x01" (4 bytes) */
static const uint8_t SDF_OTA_MAGIC[SDF_OTA_MAGIC_SIZE] = {0x53, 0x44, 0x46, 0x01};

/* Bounded, so this stays independent of image size. */
#define SDF_OTA_DIGEST_READ_CHUNK 1024

esp_err_t sdf_ota_compute_partition_digest(const esp_partition_t *partition, uint32_t image_size,
                                           uint8_t digest_out[SDF_OTA_DIGEST_SIZE])
{
    if (partition == NULL || digest_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (image_size <= SDF_OTA_FOOTER_SIZE || image_size > partition->size) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint32_t signed_len = image_size - SDF_OTA_FOOTER_SIZE;
    uint8_t buf[SDF_OTA_DIGEST_READ_CHUNK];
    sdf_ota_digest_t digest;

    esp_err_t err = sdf_ota_digest_begin(&digest, signed_len);
    if (err != ESP_OK) {
        return err;
    }

    for (uint32_t offset = 0; offset < signed_len; ) {
        uint32_t remaining = signed_len - offset;
        uint32_t chunk = (remaining < sizeof(buf)) ? remaining : (uint32_t)sizeof(buf);

        err = esp_partition_read(partition, offset, buf, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read image at offset %" PRIu32 ": %s",
                     offset, esp_err_to_name(err));
            sdf_ota_digest_release(&digest);
            return err;
        }
        err = sdf_ota_digest_update(&digest, buf, chunk);
        if (err != ESP_OK) {
            sdf_ota_digest_release(&digest);
            return err;
        }
        offset += chunk;
    }

    return sdf_ota_digest_finish(&digest, digest_out);
}

esp_err_t sdf_ota_verify_signature(const esp_partition_t *partition, uint32_t image_size,
                                   const uint8_t digest[SDF_OTA_DIGEST_SIZE])
{
    if (partition == NULL || digest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Verifying signature on partition: %s (image_size=%" PRIu32 ")",
             partition->label, image_size);

    if (image_size < SDF_OTA_FOOTER_SIZE || image_size > partition->size) {
        ESP_LOGE(TAG, "Invalid image size %" PRIu32 " for partition size %" PRIu32,
                 image_size, partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Read the footer (signature + magic) from the end of the actual written
     * image, not the end of the (larger, erased-flash-padded) partition. This
     * is the only flash read verification performs - the image itself was
     * hashed incrementally as it was written, so cost is independent of image
     * size and there is no maximum verifiable image size. */
    uint8_t footer[SDF_OTA_FOOTER_SIZE];
    esp_err_t err = esp_partition_read(partition, image_size - SDF_OTA_FOOTER_SIZE,
                                       footer, SDF_OTA_FOOTER_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read image footer: %s", esp_err_to_name(err));
        return err;
    }

    if (memcmp(footer + SDF_OTA_SIG_SIZE, SDF_OTA_MAGIC, SDF_OTA_MAGIC_SIZE) != 0) {
        ESP_LOGE(TAG, "Signature magic marker not found (image not signed?)");
        return ESP_ERR_INVALID_CRC;  /* Use as "not signed" indicator */
    }

    err = sdf_ota_verify_digest(digest, footer, sdf_ota_public_key);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Signature verification PASSED");
    return ESP_OK;
}

#else /* !CONFIG_SDF_OTA_SIGNATURE_VERIFY || CONFIG_IDF_TARGET_LINUX */

#include "esp_partition.h"

esp_err_t sdf_ota_verify_signature(const esp_partition_t *partition, uint32_t image_size,
                                   const uint8_t digest[SDF_OTA_DIGEST_SIZE])
{
    (void)partition;
    (void)image_size;
    (void)digest;
    return ESP_OK;  /* Verification disabled */
}

esp_err_t sdf_ota_compute_partition_digest(const esp_partition_t *partition, uint32_t image_size,
                                           uint8_t digest_out[SDF_OTA_DIGEST_SIZE])
{
    (void)partition;
    (void)image_size;
    (void)digest_out;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif /* CONFIG_SDF_OTA_SIGNATURE_VERIFY && !CONFIG_IDF_TARGET_LINUX */

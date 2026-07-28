#if CONFIG_SDF_OTA_SIGNATURE_VERIFY

#include "sdf_ota.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "mbedtls/ed25519.h"
#include "esp_partition.h"
#include "sdf_app.h"

static const char *TAG = "sdf_ota_sig";

/* Ed25519 public key (32 bytes) - Generated from ota_private.key
 * Run tools/sdf_sign_ota.py extract-pubkey --key ota_private.key --output ota_pubkey.bin
 */
static const uint8_t sdf_ota_public_key[32] = {
    0x40, 0xd1, 0x58, 0x3a, 0x00, 0xad, 0x56, 0xb5,
    0x74, 0x5d, 0xeb, 0x23, 0x3e, 0x91, 0x25, 0xce,
    0x2d, 0x50, 0x15, 0xc0, 0x92, 0x78, 0x33, 0x01,
    0xdf, 0x17, 0xaa, 0xb9, 0x64, 0x9d, 0x4a, 0x5d
};

/* Magic marker appended after signature: "SDF\x01" (4 bytes) */
static const uint8_t SDF_OTA_MAGIC[4] = {0x53, 0x44, 0x46, 0x01};
static const size_t SDF_OTA_SIG_SIZE = 64;      /* Ed25519 signature */
static const size_t SDF_OTA_FOOTER_SIZE = 68;   /* signature + magic */

esp_err_t sdf_ota_verify_signature(const esp_partition_t *partition)
{
    if (partition == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Verifying signature on partition: %s (size=%" PRIu32 ")",
             partition->label, partition->size);

    if (partition->size < SDF_OTA_FOOTER_SIZE) {
        ESP_LOGE(TAG, "Partition too small for signature footer");
        return ESP_ERR_INVALID_SIZE;
    }

    /* Read the footer (signature + magic) from the end of partition */
    uint8_t footer[SDF_OTA_FOOTER_SIZE];
    esp_err_t err = esp_partition_read(partition, partition->size - SDF_OTA_FOOTER_SIZE,
                                       footer, SDF_OTA_FOOTER_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read partition footer: %s", esp_err_to_name(err));
        return err;
    }

    /* Verify magic marker */
    if (memcmp(footer + SDF_OTA_SIG_SIZE, SDF_OTA_MAGIC, 4) != 0) {
        ESP_LOGE(TAG, "Signature magic marker not found (image not signed?)");
        return ESP_ERR_INVALID_CRC;  /* Use as "not signed" indicator */
    }

    /* Verify Ed25519 signature */
    mbedtls_ed25519_context ctx;
    mbedtls_ed25519_init(&ctx);

    err = mbedtls_ed25519_import_public_key(&ctx, sdf_ota_public_key, 32);
    if (err != 0) {
        ESP_LOGE(TAG, "Failed to import public key: -0x%04X", -err);
        mbedtls_ed25519_free(&ctx);
        return ESP_FAIL;
    }

    /* Signature covers the image data (partition size - footer) */
    size_t image_size = partition->size - SDF_OTA_FOOTER_SIZE;

    /* We need to read the image in chunks to verify */
    const size_t CHUNK_SIZE = 4096;
    uint8_t *buffer = malloc(CHUNK_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate verification buffer");
        mbedtls_ed25519_free(&ctx);
        return ESP_ERR_NO_MEM;
    }

    /* For Ed25519, we need to hash the entire message first.
     * mbedTLS ed25519 API expects pre-hashed message or uses internal SHA-512.
     * We'll use the low-level API that hashes internally.
     *
     * Note: mbedtls_ed25519_verify takes (pubkey, message, msg_len, signature)
     * where message is the raw data to be signed.
     */

    /* Read and verify in chunks - we need the full image for Ed25519.
     * Since the image can be large, we read it into a buffer or use a streaming approach.
     * For simplicity and memory constraints, we'll read the whole image if small enough,
     * otherwise use a temporary buffer approach.
     */

    if (image_size <= 64 * 1024) {
        /* Small enough to read in one go */
        uint8_t *image_data = malloc(image_size);
        if (image_data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate image buffer (%zu bytes)", image_size);
            free(buffer);
            mbedtls_ed25519_free(&ctx);
            return ESP_ERR_NO_MEM;
        }

        err = esp_partition_read(partition, 0, image_data, image_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read image data: %s", esp_err_to_name(err));
            free(image_data);
            free(buffer);
            mbedtls_ed25519_free(&ctx);
            return err;
        }

        err = mbedtls_ed25519_verify(&ctx, image_data, image_size, footer);
        free(image_data);

    } else {
        /* Large image: we need a different approach.
         * Ed25519 doesn't support streaming verification in mbedTLS.
         * We'll read the entire image into PSRAM if available, or fail.
         */
        ESP_LOGE(TAG, "Image too large for in-memory verification (%zu bytes). PSRAM required.", image_size);
        free(buffer);
        mbedtls_ed25519_free(&ctx);
        return ESP_ERR_NOT_SUPPORTED;
    }

    free(buffer);
    mbedtls_ed25519_free(&ctx);

    if (err != 0) {
        ESP_LOGE(TAG, "Signature verification failed: -0x%04X", -err);
        return ESP_ERR_INVALID_SIGNATURE;
    }

    ESP_LOGI(TAG, "Signature verification PASSED");
    return ESP_OK;
}

#else /* !CONFIG_SDF_OTA_SIGNATURE_VERIFY */

#include "sdf_ota.h"
#include "esp_err.h"

esp_err_t sdf_ota_verify_signature(const esp_partition_t *partition)
{
    (void)partition;
    return ESP_OK;  /* Verification disabled */
}

#endif /* CONFIG_SDF_OTA_SIGNATURE_VERIFY */
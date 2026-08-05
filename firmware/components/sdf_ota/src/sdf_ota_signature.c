#if CONFIG_SDF_OTA_SIGNATURE_VERIFY

#include "sdf_ota.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_system.h"
#include "mbedtls/ed25519.h"
#include "esp_partition.h"
#include "sdf_app.h"

static const char *TAG = "sdf_ota_sig";

/* Ed25519 public key (32 bytes) - Embedded from ota_public_key.bin */
extern const uint8_t _binary_ota_public_key_bin_start[] asm("_binary_ota_public_key_bin_start");
#define sdf_ota_public_key _binary_ota_public_key_bin_start

/* Magic marker appended after signature: "SDF\x01" (4 bytes) */
static const uint8_t SDF_OTA_MAGIC[4] = {0x53, 0x44, 0x46, 0x01};
static const size_t SDF_OTA_SIG_SIZE = 64;      /* Ed25519 signature */
static const size_t SDF_OTA_FOOTER_SIZE = 68;   /* signature + magic */

/* This board has no PSRAM (~512KB total SRAM), so whole-image Ed25519
 * verification has to fit the image in internal RAM alongside every other
 * task's stack/heap use. This cap is a safety ceiling, not a target -
 * realistic firmware images (partition_table.csv: ota_0/ota_1 are ~1.9MB
 * each) will exceed it and fall through to the "not supported" path below.
 * The real fix is streaming verification (hash the image incrementally as
 * it's written, verify the digest against the signature) or switching to
 * ESP-IDF's built-in secure boot + signed app image support, which verifies
 * the image via bootloader-side checks instead of holding it all in RAM. */
#define SDF_OTA_MAX_INMEM_VERIFY_SIZE (192u * 1024u)
/* Heap headroom to keep free after the verification buffer, so we don't
 * starve other tasks (or the mbedTLS temporaries below) even when the
 * image happens to fit under the cap above. */
#define SDF_OTA_VERIFY_HEAP_HEADROOM (32u * 1024u)

esp_err_t sdf_ota_verify_signature(const esp_partition_t *partition, uint32_t image_size)
{
    if (partition == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Verifying signature on partition: %s (image_size=%" PRIu32 ")",
             partition->label, image_size);

    if (image_size < SDF_OTA_FOOTER_SIZE || image_size > partition->size) {
        ESP_LOGE(TAG, "Invalid image size %" PRIu32 " for partition size %" PRIu32,
                 image_size, partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Read the footer (signature + magic) from the end of the actual
     * written image, not the end of the (larger, erased-flash-padded)
     * partition. */
    uint8_t footer[SDF_OTA_FOOTER_SIZE];
    esp_err_t err = esp_partition_read(partition, image_size - SDF_OTA_FOOTER_SIZE,
                                       footer, SDF_OTA_FOOTER_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read image footer: %s", esp_err_to_name(err));
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

    /* Signature covers the image data (image_size - footer), i.e. the
     * bytes actually written by sdf_ota_write(), not the whole partition. */
    size_t signed_data_size = image_size - SDF_OTA_FOOTER_SIZE;

    /* mbedtls_ed25519_verify() takes the raw message and hashes it
     * internally - there's no streaming/incremental variant in mbedTLS, so
     * the full image has to be resident in RAM at once. Fail closed rather
     * than attempt a partial/best-effort verification. */
    if (signed_data_size > SDF_OTA_MAX_INMEM_VERIFY_SIZE) {
        ESP_LOGE(TAG, "Image too large for in-memory verification (%zu > %u bytes). "
                 "No PSRAM on this board; see comment above SDF_OTA_MAX_INMEM_VERIFY_SIZE.",
                 signed_data_size, (unsigned)SDF_OTA_MAX_INMEM_VERIFY_SIZE);
        mbedtls_ed25519_free(&ctx);
        return ESP_ERR_NOT_SUPPORTED;
    }

    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < signed_data_size + SDF_OTA_VERIFY_HEAP_HEADROOM) {
        ESP_LOGE(TAG, "Not enough free heap to verify image in place (%zu bytes, need %zu, have %zu)",
                 signed_data_size, signed_data_size + SDF_OTA_VERIFY_HEAP_HEADROOM, free_heap);
        mbedtls_ed25519_free(&ctx);
        return ESP_ERR_NO_MEM;
    }

    uint8_t *image_data = malloc(signed_data_size);
    if (image_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate image buffer (%zu bytes)", signed_data_size);
        mbedtls_ed25519_free(&ctx);
        return ESP_ERR_NO_MEM;
    }

    err = esp_partition_read(partition, 0, image_data, signed_data_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read image data: %s", esp_err_to_name(err));
        free(image_data);
        mbedtls_ed25519_free(&ctx);
        return err;
    }

    err = mbedtls_ed25519_verify(&ctx, image_data, signed_data_size, footer);
    free(image_data);
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
#include "esp_partition.h"

esp_err_t sdf_ota_verify_signature(const esp_partition_t *partition, uint32_t image_size)
{
    (void)partition;
    (void)image_size;
    return ESP_OK;  /* Verification disabled */
}

#endif /* CONFIG_SDF_OTA_SIGNATURE_VERIFY */
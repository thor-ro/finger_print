#ifndef SDF_OTA_H
#define SDF_OTA_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Signed-image footer layout: the image bytes are followed by a 64-byte raw
 * ECDSA P-256 signature (r||s, two 32-byte big-endian halves) and the 4-byte
 * magic marker "SDF\x01". Raw rather than ASN.1 DER so the footer length is a
 * compile-time constant - sdf_ota_begin() has to know the signed range before
 * the first byte arrives, which a length-variable DER footer would prevent. */
#define SDF_OTA_SIG_SIZE     64u
#define SDF_OTA_MAGIC_SIZE   4u
#define SDF_OTA_FOOTER_SIZE  (SDF_OTA_SIG_SIZE + SDF_OTA_MAGIC_SIZE)
#define SDF_OTA_DIGEST_SIZE  32u
/* Uncompressed EC point: 0x04 || X || Y. Compressed encoding is not used
 * because point decompression is not reliably compiled into ESP-IDF's
 * mbedTLS. */
#define SDF_OTA_PUBKEY_SIZE  65u

/* ESP-IDF defines no ESP_ERR_INVALID_SIGNATURE - the previous implementation
 * returned one anyway, which is one of the ways it gave itself away as never
 * having been compiled. Allocate a component-local code in a range ESP-IDF
 * leaves free (0x9000-0xBFFF sits between ESP_ERR_ESP_TLS_BASE and
 * ESP_ERR_HW_CRYPTO_BASE). */
#define SDF_ERR_OTA_BASE               0xA000
#define SDF_ERR_OTA_SIGNATURE_INVALID  (SDF_ERR_OTA_BASE + 1)

typedef enum {
    SDF_OTA_SOURCE_ZIGBEE = 0,
    SDF_OTA_SOURCE_CLI    = 1,
    SDF_OTA_SOURCE_BLE    = 2,
} sdf_ota_source_t;

typedef enum {
    SDF_OTA_STATE_IDLE        = 0,
    SDF_OTA_STATE_DOWNLOADING = 1,
    SDF_OTA_STATE_VERIFYING   = 2,
    SDF_OTA_STATE_COMMITTING  = 3,
    SDF_OTA_STATE_COMPLETE    = 4,
    SDF_OTA_STATE_FAILED      = 5,
} sdf_ota_state_t;

typedef enum {
    SDF_OTA_VERSION_OLDER  = -1,
    SDF_OTA_VERSION_EQUAL  =  0,
    SDF_OTA_VERSION_NEWER  =  1,
} sdf_ota_version_cmp_t;

typedef void *sdf_ota_handle_t;

esp_err_t sdf_ota_init(void);
esp_err_t sdf_ota_begin(sdf_ota_source_t source, uint32_t image_size, sdf_ota_handle_t *handle_out);
esp_err_t sdf_ota_write(sdf_ota_handle_t handle, const void *data, uint32_t len);
esp_err_t sdf_ota_abort(sdf_ota_handle_t handle);
esp_err_t sdf_ota_verify_integrity(sdf_ota_handle_t handle);
esp_err_t sdf_ota_verify_and_commit(sdf_ota_handle_t handle);
esp_err_t sdf_ota_rollback(void);
const char *sdf_ota_get_version(void);
sdf_ota_state_t sdf_ota_get_state(void);

/* Read back the number of bytes accepted so far by sdf_ota_write() for the
 * active session identified by handle. Used by transports (e.g. BLE) that
 * need to report/resume from a confirmed offset. Returns ESP_ERR_INVALID_ARG
 * for a NULL handle_out, ESP_ERR_INVALID_STATE if handle does not match the
 * currently active session. */
esp_err_t sdf_ota_get_bytes_written(sdf_ota_handle_t handle, uint32_t *bytes_written_out);
sdf_ota_version_cmp_t sdf_ota_version_compare(const char *current, const char *incoming);

/* Verify a raw ECDSA P-256 signature over an already-computed SHA-256 digest.
 * Pure: touches no partition, flash or config state, so it compiles and runs
 * unchanged on IDF_TARGET=linux and can be exercised against known-answer
 * vectors by the host test runner.
 *
 *   digest  32 bytes, SHA-256 of the signed range
 *   sig     64 bytes, raw r||s (two 32-byte big-endian integers)
 *   pubkey  65 bytes, uncompressed EC point 0x04 || X || Y
 *
 * Returns ESP_OK when the signature is valid, ESP_ERR_INVALID_ARG for a NULL
 * argument or a malformed public key, and SDF_ERR_OTA_SIGNATURE_INVALID when
 * the inputs are well-formed but the signature does not verify. */
esp_err_t sdf_ota_verify_digest(const uint8_t digest[SDF_OTA_DIGEST_SIZE],
                                const uint8_t sig[SDF_OTA_SIG_SIZE],
                                const uint8_t pubkey[SDF_OTA_PUBKEY_SIZE]);

/* Verify the ECDSA P-256 signature footer appended after the first image_size
 * bytes of partition, against digest - the SHA-256 accumulated over the
 * signed range during sdf_ota_write(). image_size must be the actual size of
 * the written app image (e.g. sdf_ota_begin()'s image_size, or a size
 * independently derived from the image itself) - NOT partition->size, since
 * partitions are erased-flash-padded and almost always larger than the image
 * they hold. Passing partition->size here will read the footer from stale or
 * erased flash for any image smaller than the full partition. */
esp_err_t sdf_ota_verify_signature(const esp_partition_t *partition, uint32_t image_size,
                                   const uint8_t digest[SDF_OTA_DIGEST_SIZE]);

/* Compute the digest of an already-written image by reading the partition
 * back, for callers that have no live session to have accumulated one -
 * today only "ota verify", which inspects a partition flashed out of band.
 * The read-back hazard that rules this approach out inside a live session
 * (esp_ota_write() defers the trailing bytes until esp_ota_end()) does not
 * apply here, because the image is already fully committed to flash.
 * Reads in bounded chunks; no allocation proportional to image_size. */
esp_err_t sdf_ota_compute_partition_digest(const esp_partition_t *partition, uint32_t image_size,
                                           uint8_t digest_out[SDF_OTA_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* SDF_OTA_H */

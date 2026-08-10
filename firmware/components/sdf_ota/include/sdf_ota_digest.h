#ifndef SDF_OTA_DIGEST_H
#define SDF_OTA_DIGEST_H

/* Streaming SHA-256 accumulator for the OTA signed range.
 *
 * Split out of sdf_ota.c so it lives in the linux-safe part of the component
 * and can be exercised directly by the host test runner: sdf_ota.c itself is
 * excluded from IDF_TARGET=linux (it needs sdf_app_emit_audit() and
 * app_update), so clamp logic left there would be untestable, and untestable
 * is how the previous signature implementation stayed broken.
 *
 * mbedtls 4.x (tf-psa-crypto) hides the legacy SHA-256 declarations behind
 * MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS; define it here so every includer gets
 * a usable mbedtls_sha256_context without repeating the incantation. */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/private/sha256.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdf_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    mbedtls_sha256_context ctx;
    /* Length of the prefix the signature covers, fixed at begin(). */
    uint32_t signed_len;
    uint32_t bytes_hashed;
    bool active;
} sdf_ota_digest_t;

/* Start accumulating over the first signed_len bytes of the transfer. */
esp_err_t sdf_ota_digest_begin(sdf_ota_digest_t *d, uint32_t signed_len);

/* Feed the next len transferred bytes. Bytes past signed_len are ignored, so
 * the caller can hand over the stream verbatim - including the 68-byte footer
 * the client sends after the image - without splitting chunks itself. Calls
 * must be in transfer order and must not re-send bytes already fed. */
esp_err_t sdf_ota_digest_update(sdf_ota_digest_t *d, const void *data, uint32_t len);

/* Finalize into out[SDF_OTA_DIGEST_SIZE]. Releases the context; the
 * accumulator must not be updated afterwards. */
esp_err_t sdf_ota_digest_finish(sdf_ota_digest_t *d, uint8_t out[SDF_OTA_DIGEST_SIZE]);

/* Release without finalizing. Idempotent, so it is safe on every path that
 * ends a session, including paths that may run twice. */
void sdf_ota_digest_release(sdf_ota_digest_t *d);

#ifdef __cplusplus
}
#endif

#endif /* SDF_OTA_DIGEST_H */

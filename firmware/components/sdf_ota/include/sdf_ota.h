#ifndef SDF_OTA_H
#define SDF_OTA_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_assert.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the incoming image carries its esp_app_desc_t. The offset is
 * sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) = 24 + 8,
 * which is exactly what esp_ota_get_partition_description() reads from
 * (esp_ota_ops.c:985); both it and the 256-byte size are load-bearing parts of
 * the image format the bootloader itself depends on. Named here so the session
 * can capture the window off the transfer stream instead of reading the target
 * partition back while an esp_ota_handle_t is still open. */
#define SDF_OTA_APP_DESC_OFFSET  32u
#define SDF_OTA_APP_DESC_SIZE    256u
ESP_STATIC_ASSERT(SDF_OTA_APP_DESC_SIZE == sizeof(esp_app_desc_t),
                  "SDF_OTA_APP_DESC_SIZE must track sizeof(esp_app_desc_t)");

/* Smallest image a session will accept: header plus a complete app
 * descriptor. Anything shorter cannot carry the window the commit path
 * captures, and is not a bootable app image either. There is no
 * project-defined signed range to add on top of this any more - IDF's own
 * signed-app verification (esp_ota_end() -> esp_image_verify()) validates
 * the signature sector's presence and position itself. */
#define SDF_OTA_MIN_IMAGE_SIZE  (SDF_OTA_APP_DESC_OFFSET + SDF_OTA_APP_DESC_SIZE)

/* ESP-IDF defines no ESP_ERR_INVALID_SIGNATURE - the previous implementation
 * returned one anyway, which is one of the ways it gave itself away as never
 * having been compiled. Allocate a component-local code in a range ESP-IDF
 * leaves free (0x9000-0xBFFF sits between ESP_ERR_ESP_TLS_BASE and
 * ESP_ERR_HW_CRYPTO_BASE). Retained across the move to IDF signed-app
 * verification so audit-log consumers keep working: esp_ota_end()'s
 * ESP_ERR_OTA_VALIDATE_FAILED is mapped onto this code. */
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

/* Copy whatever part of the transfer chunk [stream_offset, stream_offset+len)
 * falls inside the absolute window [win_start, win_start+win_len) into dst, at
 * the offset within the window it belongs at.
 *
 * The incoming esp_app_desc_t is needed before esp_ota_end(), and reading it
 * back off the target partition at that point is only correct while
 * esp_ota_write() happens not to be buffering (flash encryption) and the
 * staging partition happens to be the final one. The window is absolute
 * rather than sliding because expected_size is fixed at sdf_ota_begin(), so
 * no ring-buffer state is needed and a chunk never has to be split by the
 * caller.
 *
 * Takes no partition, flash or session argument, so the chunk-boundary
 * arithmetic - where an off-by-one would hide - is exercised directly by the
 * host test runner on IDF_TARGET=linux. Stateless and idempotent per byte: a
 * chunk lying wholly outside the window leaves dst untouched, and feeding the
 * same chunk twice is harmless. Callers must still feed the stream in order and
 * exactly once for the window to end up complete. */
void sdf_ota_window_capture(uint8_t *dst, uint32_t win_start, uint32_t win_len,
                            uint32_t stream_offset, const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* SDF_OTA_H */

#ifndef SDF_OTA_H
#define SDF_OTA_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

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
sdf_ota_version_cmp_t sdf_ota_version_compare(const char *current, const char *incoming);

/* Verify the Ed25519 signature footer appended after the first image_size
 * bytes of partition. image_size must be the actual size of the written
 * app image (e.g. sdf_ota_begin()'s image_size, or a size independently
 * derived from the image itself) - NOT partition->size, since partitions
 * are erased-flash-padded and almost always larger than the image they
 * hold. Passing partition->size here will read the footer from stale or
 * erased flash for any image smaller than the full partition. */
esp_err_t sdf_ota_verify_signature(const esp_partition_t *partition, uint32_t image_size);

#ifdef __cplusplus
}
#endif

#endif /* SDF_OTA_H */

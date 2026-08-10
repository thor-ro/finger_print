#include "sdf_ota.h"
#include "sdf_ota_digest.h"
#include "sdf_common.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "sdf_config.h"

/* Forward declarations for external functions */
extern void sdf_app_emit_audit(sdf_audit_event_type_t type, uint16_t user_id, int32_t status, uint16_t detail);

static const char *TAG = "sdf_ota";

/* Internal session structure */
typedef struct {
    sdf_ota_source_t source;
    esp_ota_handle_t ota_handle;
    const esp_partition_t *target_partition;
    uint32_t expected_size;
    uint32_t bytes_written;
    /* Everything sdf_ota_verify_and_commit() needs before esp_ota_end() is
     * captured here from the write stream, so the commit path performs no read
     * of target_partition while the OTA handle is open.
     *
     * That is not a micro-optimisation. esp_ota_write() defers the trailing
     * (size % 16) bytes into an internal buffer and only flushes them in
     * esp_ota_end() when flash encryption is enabled (esp_ota_ops.c:347-377,
     * :583-592), so a read-back would see erased flash for the last <=15 bytes
     * the day encryption is turned on - and since app images are 16-byte
     * aligned and the footer is 68 bytes, that slice is exactly the "SDF\x01"
     * magic. ESP-IDF v6 also allows writes to land in a *staging* partition
     * that is only copied to the final one inside esp_ota_end()
     * (esp_ota_ops.c:262, :598-601), which would make any earlier read return
     * the previous image wholesale. Capturing on the way in is immune to both,
     * and to whatever the write layer does next.
     *
     *   digest    SHA-256 over the signed range [0, expected_size - 68)
     *   app_desc  the incoming esp_app_desc_t, window [32, 288), for the
     *             version and downgrade check
     *   footer    the 64-byte signature plus magic, window
     *             [expected_size - 68, expected_size)
     *
     * All three windows are fixed at sdf_ota_begin() and depend only on writes
     * being a pure sequential append, which they are. */
    sdf_ota_digest_t digest;
    uint8_t app_desc[SDF_OTA_APP_DESC_SIZE];
    uint8_t footer[SDF_OTA_FOOTER_SIZE];
    sdf_ota_state_t state;
    bool active;
    /* True from a successful esp_ota_begin() until whichever comes first:
     * esp_ota_abort(), or esp_ota_end() returning *any* value - it frees its
     * entry through the cleanup: label on every path, success and failure
     * alike (esp_ota_ops.c:604-610). Aborting after that would be a lookup of
     * a freed handle, so the flag rather than a comment is what keeps the
     * failure paths after esp_ota_end() from calling esp_ota_abort(). */
    bool ota_handle_open;
} sdf_ota_session_t;

static sdf_ota_session_t s_session = {0};
static SemaphoreHandle_t s_session_mutex = NULL;

/* Version string embedded at build time */
extern const char sdf_ota_version_string[];



/* Idempotent, so it is safe on every path that ends a session - including
 * paths that may run twice - without leaking or double-freeing the mbedTLS
 * context. Callers must hold s_session_mutex. */
static void sdf_ota_session_digest_release(void)
{
    sdf_ota_digest_release(&s_session.digest);
}

static esp_err_t sdf_ota_emit_audit(sdf_audit_event_type_t type, int32_t status, uint16_t detail)
{
    extern void sdf_app_emit_audit(sdf_audit_event_type_t type, uint16_t user_id, int32_t status, uint16_t detail);
    sdf_app_emit_audit(type, 0, status, detail);
    return ESP_OK;
}

static esp_err_t sdf_ota_state_transition(sdf_ota_state_t new_state)
{
    static const struct {
        sdf_ota_state_t from;
        sdf_ota_state_t to;
    } valid_transitions[] = {
        {SDF_OTA_STATE_IDLE, SDF_OTA_STATE_DOWNLOADING},
        {SDF_OTA_STATE_DOWNLOADING, SDF_OTA_STATE_VERIFYING},
        {SDF_OTA_STATE_VERIFYING, SDF_OTA_STATE_COMMITTING},
        {SDF_OTA_STATE_COMMITTING, SDF_OTA_STATE_COMPLETE},
        {SDF_OTA_STATE_DOWNLOADING, SDF_OTA_STATE_FAILED},
        {SDF_OTA_STATE_VERIFYING, SDF_OTA_STATE_FAILED},
        {SDF_OTA_STATE_COMMITTING, SDF_OTA_STATE_FAILED},
    };

    bool valid = false;
    for (size_t i = 0; i < sizeof(valid_transitions) / sizeof(valid_transitions[0]); i++) {
        if (valid_transitions[i].from == s_session.state && valid_transitions[i].to == new_state) {
            valid = true;
            break;
        }
    }

    if (!valid && s_session.state != new_state) {
        ESP_LOGE(TAG, "Invalid state transition: %d -> %d", s_session.state, new_state);
        return ESP_ERR_INVALID_STATE;
    }

    /* Emit audit event for state transitions */
    switch (new_state) {
    case SDF_OTA_STATE_DOWNLOADING:
        sdf_ota_emit_audit(SDF_AUDIT_OTA_STARTED, 0, s_session.source);
        break;
    case SDF_OTA_STATE_VERIFYING:
        sdf_ota_emit_audit(SDF_AUDIT_OTA_VERIFYING, 0, 0);
        break;
    case SDF_OTA_STATE_COMMITTING:
        break; // Will emit COMMITTED or FAILED after
    case SDF_OTA_STATE_COMPLETE:
        sdf_ota_emit_audit(SDF_AUDIT_OTA_COMMITTED, 0, 0);
        break;
    case SDF_OTA_STATE_FAILED:
        sdf_ota_emit_audit(SDF_AUDIT_OTA_FAILED, 0, 0);
        break;
    default:
        break;
    }

    s_session.state = new_state;
    ESP_LOGI(TAG, "OTA state: %d", new_state);
    return ESP_OK;
}

/* The single way a session ends badly. Releases the digest context, hands the
 * OTA handle back to ESP-IDF if it is still ours to hand back, marks the
 * session FAILED and - the part every hand-rolled failure block used to miss -
 * clears active, so the next sdf_ota_begin() from any transport is accepted
 * without a reboot. Returns err unchanged so callers can `return
 * sdf_ota_session_fail(err);` and lose nothing.
 *
 * Callers must hold s_session_mutex and must give it back themselves; the
 * mutex is deliberately not touched here so this stays usable from paths that
 * have more to do before unlocking. */
static esp_err_t sdf_ota_session_fail(esp_err_t err)
{
    sdf_ota_session_digest_release();

    if (s_session.ota_handle_open) {
        esp_ota_abort(s_session.ota_handle);
        s_session.ota_handle_open = false;
    }

    sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
    s_session.active = false;
    return err;
}

esp_err_t sdf_ota_init(void)
{
    if (s_session_mutex != NULL) {
        return ESP_OK;
    }

    s_session_mutex = xSemaphoreCreateMutex();
    if (s_session_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create OTA mutex");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OTA subsystem initialized");
    ESP_LOGI(TAG, "Current firmware version: %s", sdf_ota_version_string);

    return ESP_OK;
}

esp_err_t sdf_ota_begin(sdf_ota_source_t source, uint32_t image_size, sdf_ota_handle_t *handle_out)
{
    if (handle_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_session.active) {
        xSemaphoreGive(s_session_mutex);
        ESP_LOGE(TAG, "OTA session already active");
        return ESP_ERR_INVALID_STATE;
    }

    /* Validate image size against partition */
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        xSemaphoreGive(s_session_mutex);
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_ERR_NOT_FOUND;
    }

    /* The smallest thing that can be a signed app image is a header plus a
     * complete esp_app_desc_t plus the footer. Below that the two capture
     * windows would overlap or run past the end of the stream and the commit
     * path would be verifying whatever the memset left behind, so reject here
     * rather than in the middle of a transfer. Also keeps signed_len below
     * from underflowing. */
    if (image_size < SDF_OTA_MIN_IMAGE_SIZE) {
        xSemaphoreGive(s_session_mutex);
        ESP_LOGE(TAG, "Image size %" PRIu32 " is below the %u-byte minimum for a signed image "
                      "(%u-byte header + %u-byte app descriptor + %u-byte signature footer)",
                 image_size, (unsigned)SDF_OTA_MIN_IMAGE_SIZE, (unsigned)SDF_OTA_APP_DESC_OFFSET,
                 (unsigned)SDF_OTA_APP_DESC_SIZE, (unsigned)SDF_OTA_FOOTER_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (image_size > partition->size) {
        xSemaphoreGive(s_session_mutex);
        ESP_LOGE(TAG, "Image size %" PRIu32 " exceeds partition size %" PRIu32,
                 image_size, partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Begin OTA write */
    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(partition, image_size, &ota_handle);
    if (err != ESP_OK) {
        xSemaphoreGive(s_session_mutex);
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Initialize session */
    memset(&s_session, 0, sizeof(s_session));
    s_session.source = source;
    s_session.ota_handle = ota_handle;
    s_session.target_partition = partition;
    s_session.expected_size = image_size;
    s_session.bytes_written = 0;
    s_session.active = true;
    s_session.ota_handle_open = true;

    /* The two paths below release the handle by hand rather than through
     * sdf_ota_session_fail(): the session is still IDLE here, and IDLE ->
     * FAILED is not a legal transition. They must still clear the flag, or a
     * later abort would look up a handle esp_ota_abort() has already freed. */
    err = sdf_ota_digest_begin(&s_session.digest, image_size - SDF_OTA_FOOTER_SIZE);
    if (err != ESP_OK) {
        esp_ota_abort(ota_handle);
        s_session.ota_handle_open = false;
        s_session.active = false;
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    err = sdf_ota_state_transition(SDF_OTA_STATE_DOWNLOADING);
    if (err != ESP_OK) {
        sdf_ota_session_digest_release();
        esp_ota_abort(ota_handle);
        s_session.ota_handle_open = false;
        s_session.active = false;
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    sdf_ota_emit_audit(SDF_AUDIT_OTA_TRIGGERED, 0, source);

    *handle_out = &s_session;
    ESP_LOGI(TAG, "OTA session started: source=%d, partition=%s, size=%" PRIu32,
             source, partition->label, image_size);
    xSemaphoreGive(s_session_mutex);
    return ESP_OK;
}

esp_err_t sdf_ota_write(sdf_ota_handle_t handle, const void *data, uint32_t len)
{
    if (handle == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_session.active || handle != &s_session) {
        xSemaphoreGive(s_session_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_ota_write(s_session.ota_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        err = sdf_ota_session_fail(err);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    /* Writes are a pure sequential append - there is no offset addressing here
     * and transports resume from the device's own bytes_written - so every
     * byte reaches the digest exactly once, in order. The accumulator clamps
     * at the signed range itself, so the 68-byte footer the client streams
     * along with the image is excluded without splitting chunks here. */
    err = sdf_ota_digest_update(&s_session.digest, data, len);
    if (err != ESP_OK) {
        err = sdf_ota_session_fail(err);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    /* Same append property, so the same reasoning: each window fills in
     * whatever order the chunks happen to be sliced, and is complete once the
     * stream has passed its end. Captured before bytes_written advances, since
     * that is this chunk's own offset in the stream. Neither call can fail and
     * neither depends on chunk alignment. */
    sdf_ota_window_capture(s_session.app_desc, SDF_OTA_APP_DESC_OFFSET, SDF_OTA_APP_DESC_SIZE,
                           s_session.bytes_written, data, len);
    sdf_ota_window_capture(s_session.footer, s_session.expected_size - SDF_OTA_FOOTER_SIZE,
                           SDF_OTA_FOOTER_SIZE, s_session.bytes_written, data, len);

    s_session.bytes_written += len;

    xSemaphoreGive(s_session_mutex);
    return ESP_OK;
}

esp_err_t sdf_ota_abort(sdf_ota_handle_t handle)
{
    if (handle == NULL || s_session_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_session.active || handle != &s_session) {
        xSemaphoreGive(s_session_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    if (s_session.ota_handle_open) {
        err = esp_ota_abort(s_session.ota_handle);
        s_session.ota_handle_open = false;
    }
    sdf_ota_session_digest_release();
    s_session.active = false;
    (void)sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
    xSemaphoreGive(s_session_mutex);
    return err;
}

esp_err_t sdf_ota_verify_integrity(sdf_ota_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_session.active || handle != &s_session) {
        xSemaphoreGive(s_session_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_session.bytes_written != s_session.expected_size) {
        ESP_LOGE(TAG, "Size mismatch: wrote %" PRIu32 ", expected %" PRIu32,
                 s_session.bytes_written, s_session.expected_size);
        esp_err_t fail_err = sdf_ota_session_fail(ESP_FAIL);
        xSemaphoreGive(s_session_mutex);
        return fail_err;
    }

    esp_err_t err = sdf_ota_state_transition(SDF_OTA_STATE_VERIFYING);
    xSemaphoreGive(s_session_mutex);
    return err;
}

esp_err_t sdf_ota_verify_and_commit(sdf_ota_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_session.active || handle != &s_session) {
        xSemaphoreGive(s_session_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sdf_ota_state_transition(SDF_OTA_STATE_COMMITTING);
    if (err != ESP_OK) {
        err = sdf_ota_session_fail(err);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    /* The running partition is fully committed with no handle open against it,
     * so reading its description back from flash is sound. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_desc;
    err = esp_ota_get_partition_description(running, &running_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get running partition description: %s", esp_err_to_name(err));
        err = sdf_ota_session_fail(err);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    /* The incoming one is *not* - esp_ota_end() has not run yet - so it comes
     * from the window captured off the write stream rather than from
     * esp_ota_get_partition_description(s_session.target_partition, ...),
     * which would read the target while the handle is still open. The magic
     * check that call performs is repeated here, since nothing else has
     * established that these 256 bytes are a descriptor at all. */
    esp_app_desc_t target_desc;
    memcpy(&target_desc, s_session.app_desc, sizeof(target_desc));
    if (target_desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        ESP_LOGE(TAG, "Incoming image carries no app descriptor (magic 0x%08" PRIx32 ")",
                 target_desc.magic_word);
        err = sdf_ota_session_fail(ESP_ERR_NOT_FOUND);
        xSemaphoreGive(s_session_mutex);
        return err;
    }
    /* esp_app_desc_t's strings are fixed-size arrays that a hostile image can
     * fill completely; terminate them before anything prints or compares. */
    target_desc.version[sizeof(target_desc.version) - 1] = '\0';
    target_desc.project_name[sizeof(target_desc.project_name) - 1] = '\0';

    ESP_LOGI(TAG, "Version check: current=%s, incoming=%s",
             running_desc.version, target_desc.version);

    /* Compare versions. sdf_ota_version_compare(current, incoming) returns
     * NEWER when *incoming* is newer than *current* (i.e. a genuine
     * upgrade) and OLDER when incoming is older (a downgrade) - the
     * downgrade branch below must therefore check for OLDER, not NEWER. */
    sdf_ota_version_cmp_t cmp = sdf_ota_version_compare(running_desc.version, target_desc.version);
    if (cmp == SDF_OTA_VERSION_OLDER) {
        ESP_LOGW(TAG, "Downgrade detected: %s -> %s",
                 running_desc.version, target_desc.version);
        sdf_ota_emit_audit(SDF_AUDIT_OTA_VERSION_DOWNGRADE, 0, 0);
#if !CONFIG_SDF_OTA_ALLOW_DOWNGRADE
        ESP_LOGE(TAG, "Downgrades not allowed (CONFIG_SDF_OTA_ALLOW_DOWNGRADE=n)");
        err = sdf_ota_session_fail(ESP_ERR_INVALID_VERSION);
        xSemaphoreGive(s_session_mutex);
        return err;
#endif
    } else if (cmp == SDF_OTA_VERSION_EQUAL) {
        ESP_LOGI(TAG, "Same version reinstall: %s", target_desc.version);
    } else {
        ESP_LOGI(TAG, "Upgrade: %s -> %s", running_desc.version, target_desc.version);
        sdf_ota_emit_audit(SDF_AUDIT_OTA_VERSION_UPGRADE, 0, 0);
    }

    /* Finalize the streaming digest before anything can consume it. Done
     * unconditionally so the context is released on exactly one path whether
     * or not verification is compiled in. */
    uint8_t digest[SDF_OTA_DIGEST_SIZE] = {0};
    err = sdf_ota_digest_finish(&s_session.digest, digest);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to finalize image digest: %s", esp_err_to_name(err));
        err = sdf_ota_session_fail(err);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    /* Verify signature if enabled. Against the footer captured from the
     * stream, not one read back from the target - see the session struct. */
#if CONFIG_SDF_OTA_SIGNATURE_VERIFY
    err = sdf_ota_verify_footer(s_session.footer, digest);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Signature verification failed: %s", esp_err_to_name(err));
        sdf_ota_emit_audit(SDF_AUDIT_OTA_SIGNATURE_INVALID, err, 0);
        err = sdf_ota_session_fail(err);
        xSemaphoreGive(s_session_mutex);
        return err;
    }
    ESP_LOGI(TAG, "Signature verification passed");
#endif

    /* Finalize OTA. esp_ota_end() frees its entry on every path it can return
     * from, success and failure alike (esp_ota_ops.c:604-610), so the handle
     * stops being ours the moment it returns - hence the flag is cleared
     * before the error branch, which would otherwise abort a freed handle. */
    err = esp_ota_end(s_session.ota_handle);
    s_session.ota_handle_open = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        err = sdf_ota_session_fail(err);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    err = esp_ota_set_boot_partition(s_session.target_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        err = sdf_ota_session_fail(err);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    sdf_ota_state_transition(SDF_OTA_STATE_COMPLETE);
    ESP_LOGI(TAG, "OTA commit successful, rebooting...");
    xSemaphoreGive(s_session_mutex);

    /* Mark app as valid for rollback */
#if CONFIG_SDF_OTA_BOOTLOADER_ROLLBACK
    esp_ota_mark_app_valid_cancel_rollback();
#endif

    esp_restart();
    return ESP_OK; /* Never reached */
}

esp_err_t sdf_ota_rollback(void)
{
    ESP_LOGW(TAG, "Manual rollback requested");
    sdf_app_emit_audit(SDF_AUDIT_OTA_ROLLED_BACK, 0, 0, 0);
#if CONFIG_SDF_OTA_BOOTLOADER_ROLLBACK
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(err));
    }
    return err;
#else
    ESP_LOGE(TAG, "Bootloader rollback not enabled (CONFIG_SDF_OTA_BOOTLOADER_ROLLBACK=n)");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

const char *sdf_ota_get_version(void)
{
    return sdf_ota_version_string;
}

sdf_ota_state_t sdf_ota_get_state(void)
{
    return s_session.state;
}

esp_err_t sdf_ota_get_bytes_written(sdf_ota_handle_t handle, uint32_t *bytes_written_out)
{
    if (handle == NULL || bytes_written_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_session.active || handle != &s_session) {
        xSemaphoreGive(s_session_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    *bytes_written_out = s_session.bytes_written;
    xSemaphoreGive(s_session_mutex);
    return ESP_OK;
}

sdf_ota_version_cmp_t sdf_ota_version_compare(const char *current, const char *incoming)
{
    if (current == NULL || incoming == NULL) {
        return SDF_OTA_VERSION_EQUAL;
    }

    /* Skip leading 'v' if present */
    if (*current == 'v') current++;
    if (*incoming == 'v') incoming++;

    /* Parse major.minor.patch */
    int c_maj = 0, c_min = 0, c_pat = 0;
    int i_maj = 0, i_min = 0, i_pat = 0;

    sscanf(current, "%d.%d.%d", &c_maj, &c_min, &c_pat);
    sscanf(incoming, "%d.%d.%d", &i_maj, &i_min, &i_pat);

    if (i_maj != c_maj) return (i_maj > c_maj) ? SDF_OTA_VERSION_NEWER : SDF_OTA_VERSION_OLDER;
    if (i_min != c_min) return (i_min > c_min) ? SDF_OTA_VERSION_NEWER : SDF_OTA_VERSION_OLDER;
    if (i_pat != c_pat) return (i_pat > c_pat) ? SDF_OTA_VERSION_NEWER : SDF_OTA_VERSION_OLDER;

    /* Check for pre-release suffix (-N-g<hash>) */
    const char *c_pre = strchr(current, '-');
    const char *i_pre = strchr(incoming, '-');

    bool c_has_pre = (c_pre != NULL);
    bool i_has_pre = (i_pre != NULL);

    if (c_has_pre && !i_has_pre) {
        return SDF_OTA_VERSION_OLDER;  /* release > pre-release */
    }
    if (!c_has_pre && i_has_pre) {
        return SDF_OTA_VERSION_NEWER;  /* pre-release < release */
    }
    if (c_has_pre && i_has_pre) {
        /* Both have pre-release, compare commit count */
        int c_cnt = 0, i_cnt = 0;
        sscanf(c_pre, "-%d-g", &c_cnt);
        sscanf(i_pre, "-%d-g", &i_cnt);
        if (i_cnt != c_cnt) return (i_cnt > c_cnt) ? SDF_OTA_VERSION_NEWER : SDF_OTA_VERSION_OLDER;
    }

    return SDF_OTA_VERSION_EQUAL;
}

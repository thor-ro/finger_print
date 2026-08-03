#include "sdf_ota.h"
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
    sdf_ota_state_t state;
    bool active;
} sdf_ota_session_t;

static sdf_ota_session_t s_session = {0};
static SemaphoreHandle_t s_session_mutex = NULL;

/* Version string embedded at build time */
extern const char sdf_ota_version_string[];



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

    err = sdf_ota_state_transition(SDF_OTA_STATE_DOWNLOADING);
    if (err != ESP_OK) {
        esp_ota_abort(ota_handle);
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
        sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

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

    esp_err_t err = esp_ota_abort(s_session.ota_handle);
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
        sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
        xSemaphoreGive(s_session_mutex);
        return ESP_FAIL;
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
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    /* Read version from target partition */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_desc;
    err = esp_ota_get_partition_description(running, &running_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get running partition description: %s", esp_err_to_name(err));
        sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    esp_app_desc_t target_desc;
    err = esp_ota_get_partition_description(s_session.target_partition, &target_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get target partition description: %s", esp_err_to_name(err));
        sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    ESP_LOGI(TAG, "Version check: current=%s, incoming=%s",
             running_desc.version, target_desc.version);

    /* Compare versions */
    sdf_ota_version_cmp_t cmp = sdf_ota_version_compare(running_desc.version, target_desc.version);
    if (cmp == SDF_OTA_VERSION_NEWER) {
        ESP_LOGW(TAG, "Downgrade detected: %s -> %s",
                 running_desc.version, target_desc.version);
        sdf_ota_emit_audit(SDF_AUDIT_OTA_VERSION_DOWNGRADE, 0, 0);
#if !CONFIG_SDF_OTA_ALLOW_DOWNGRADE
        ESP_LOGE(TAG, "Downgrades not allowed (CONFIG_SDF_OTA_ALLOW_DOWNGRADE=n)");
        sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
        xSemaphoreGive(s_session_mutex);
        return ESP_ERR_INVALID_VERSION;
#endif
    } else if (cmp == SDF_OTA_VERSION_EQUAL) {
        ESP_LOGI(TAG, "Same version reinstall: %s", target_desc.version);
    } else {
        ESP_LOGI(TAG, "Upgrade: %s -> %s", running_desc.version, target_desc.version);
        sdf_ota_emit_audit(SDF_AUDIT_OTA_VERSION_UPGRADE, 0, 0);
    }

    /* Verify signature if enabled */
#if CONFIG_SDF_OTA_SIGNATURE_VERIFY
    err = sdf_ota_verify_signature(s_session.target_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Signature verification failed: %s", esp_err_to_name(err));
        sdf_ota_emit_audit(SDF_AUDIT_OTA_SIGNATURE_INVALID, err, 0);
        sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
        xSemaphoreGive(s_session_mutex);
        return err;
    }
    ESP_LOGI(TAG, "Signature verification passed");
#endif

    /* Finalize OTA */
    err = esp_ota_end(s_session.ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
        xSemaphoreGive(s_session_mutex);
        return err;
    }

    err = esp_ota_set_boot_partition(s_session.target_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        sdf_ota_state_transition(SDF_OTA_STATE_FAILED);
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

#include "sdf_ble_companion.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h" /* ble_att_mtu() */

#include "sdf_ble_ota_protocol.h"
#include "sdf_ota.h"

static const char *TAG = "sdf_ble_ota";

typedef struct {
    bool active;
    sdf_ota_handle_t ota_handle;
    uint16_t conn_handle;
    uint32_t declared_size;
} sdf_ble_ota_session_t;

static sdf_ble_ota_session_t s_session;
static SemaphoreHandle_t s_session_lock;
static esp_timer_handle_t s_idle_timer;

static void sdf_ble_ota_notify(uint16_t conn_handle, const char *json_str) {
    esp_err_t err = sdf_ble_companion_notify_ota(conn_handle, (const uint8_t *)json_str,
                                                  strlen(json_str));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to notify OTA status: %s", esp_err_to_name(err));
    }
}

static void sdf_ble_ota_rearm_idle_timer(void) {
    if (s_idle_timer == NULL) {
        return;
    }
    esp_timer_stop(s_idle_timer); /* no-op if not currently running */
    esp_timer_start_once(s_idle_timer, (uint64_t)SDF_BLE_OTA_IDLE_TIMEOUT_MS * 1000);
}

static void sdf_ble_ota_idle_timeout_cb(void *arg) {
    (void)arg;

    if (s_session_lock == NULL || xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    bool was_active = s_session.active;
    uint16_t conn_handle = s_session.conn_handle;
    sdf_ota_handle_t handle = s_session.ota_handle;
    if (was_active) {
        memset(&s_session, 0, sizeof(s_session));
    }
    xSemaphoreGive(s_session_lock);

    if (was_active) {
        ESP_LOGW(TAG, "BLE OTA idle timeout, aborting session");
        sdf_ota_abort(handle);
        sdf_ble_ota_notify(conn_handle, "{\"status\":\"failed\",\"error\":\"idle_timeout\"}");
    }
}

static esp_err_t sdf_ble_ota_ensure_init(void) {
    esp_err_t err = sdf_ota_init();
    if (err != ESP_OK) {
        return err;
    }

    if (s_session_lock == NULL) {
        s_session_lock = xSemaphoreCreateMutex();
        if (s_session_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_idle_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = sdf_ble_ota_idle_timeout_cb,
            .arg = NULL,
            .name = "sdf_ble_ota_idle",
        };
        err = esp_timer_create(&timer_args, &s_idle_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create idle timer: %s", esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

/* Reject malformed/zero-size BEGIN or a size-mismatched BEGIN while another
 * session is open without disturbing that session (returns false -> caller
 * rejects the GATT write with a non-zero ATT error, no notify). A BEGIN
 * matching an already-open session's declared size is treated as a resume:
 * no new sdf_ota_begin() call, the (possibly new) connection is adopted, and
 * the current confirmed offset is reported. */
static bool sdf_ble_ota_handle_begin(uint16_t conn_handle, const uint8_t *payload,
                                      size_t payload_len) {
    uint32_t image_size;
    if (!sdf_ble_ota_protocol_parse_begin(payload, payload_len, &image_size)) {
        ESP_LOGW(TAG, "BEGIN: malformed payload (len=%u)", (unsigned)payload_len);
        return false;
    }

    if (xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    sdf_ble_ota_begin_decision_t decision =
        sdf_ble_ota_protocol_decide_begin(s_session.active, s_session.declared_size, image_size);

    if (decision == SDF_BLE_OTA_BEGIN_REJECT) {
        xSemaphoreGive(s_session_lock);
        ESP_LOGW(TAG, "BEGIN: size mismatch for resume (open=%" PRIu32 ", requested=%" PRIu32 ")",
                 s_session.declared_size, image_size);
        return false;
    }

    if (decision == SDF_BLE_OTA_BEGIN_RESUME) {
        s_session.conn_handle = conn_handle;
        sdf_ota_handle_t handle = s_session.ota_handle;
        xSemaphoreGive(s_session_lock);

        uint32_t bytes_written = 0;
        sdf_ota_get_bytes_written(handle, &bytes_written);
        sdf_ble_ota_rearm_idle_timer();

        char notify_buf[64];
        snprintf(notify_buf, sizeof(notify_buf), "{\"status\":\"ready\",\"offset\":%" PRIu32 "}",
                 bytes_written);
        sdf_ble_ota_notify(conn_handle, notify_buf);
        return true;
    }
    xSemaphoreGive(s_session_lock);

    /* SDF_BLE_OTA_BEGIN_START_NEW */
    sdf_ota_handle_t handle = NULL;
    esp_err_t err = sdf_ota_begin(SDF_OTA_SOURCE_BLE, image_size, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BEGIN: sdf_ota_begin failed: %s", esp_err_to_name(err));
        return false;
    }

    if (xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        sdf_ota_abort(handle);
        return false;
    }
    s_session.active = true;
    s_session.ota_handle = handle;
    s_session.conn_handle = conn_handle;
    s_session.declared_size = image_size;
    xSemaphoreGive(s_session_lock);

    sdf_ble_ota_rearm_idle_timer();
    sdf_ble_ota_notify(conn_handle, "{\"status\":\"ready\",\"offset\":0}");
    return true;
}

/* Reject an empty/over-MTU chunk or one with no matching open session
 * (returns false -> ATT error, no notify, no write to the OTA session). An
 * accepted chunk that fails sdf_ota_write() aborts the session and reports
 * failure via notify (not an ATT error), since the write itself was
 * well-formed. */
static bool sdf_ble_ota_handle_chunk(uint16_t conn_handle, const uint8_t *payload,
                                      size_t payload_len) {
    uint16_t mtu = ble_att_mtu(conn_handle);
    if (!sdf_ble_ota_protocol_validate_chunk(mtu, payload_len)) {
        ESP_LOGW(TAG, "CHUNK: invalid payload (len=%u, mtu=%u, max=%u)", (unsigned)payload_len,
                 (unsigned)mtu, (unsigned)sdf_ble_ota_protocol_max_chunk_len(mtu));
        return false;
    }

    if (xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    if (!s_session.active || s_session.conn_handle != conn_handle) {
        xSemaphoreGive(s_session_lock);
        ESP_LOGW(TAG, "CHUNK: no matching open session");
        return false;
    }
    sdf_ota_handle_t handle = s_session.ota_handle;
    xSemaphoreGive(s_session_lock);

    esp_err_t err = sdf_ota_write(handle, payload, (uint32_t)payload_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CHUNK: sdf_ota_write failed: %s", esp_err_to_name(err));
        sdf_ota_abort(handle);
        esp_timer_stop(s_idle_timer);
        if (xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
            memset(&s_session, 0, sizeof(s_session));
            xSemaphoreGive(s_session_lock);
        }
        char notify_buf[96];
        snprintf(notify_buf, sizeof(notify_buf), "{\"status\":\"failed\",\"error\":\"%s\"}",
                 esp_err_to_name(err));
        sdf_ble_ota_notify(conn_handle, notify_buf);
        return true;
    }

    uint32_t bytes_written = 0;
    sdf_ota_get_bytes_written(handle, &bytes_written);
    sdf_ble_ota_rearm_idle_timer();

    char notify_buf[64];
    snprintf(notify_buf, sizeof(notify_buf), "{\"status\":\"chunk_ack\",\"offset\":%" PRIu32 "}",
             bytes_written);
    sdf_ble_ota_notify(conn_handle, notify_buf);
    return true;
}

/* Reject an END with a payload or with no matching open session (returns
 * false -> ATT error, no notify). A matching END always consumes the
 * session (success or failure) and reports pass/fail via notify - on
 * success sdf_ota_verify_and_commit() reboots the device and never returns,
 * so the success notify below is unreachable in practice; this mirrors the
 * pre-existing behavior of every other sdf_ota_verify_and_commit() caller
 * (Zigbee, CLI) and is not a regression introduced by this change. */
static bool sdf_ble_ota_handle_end(uint16_t conn_handle, size_t payload_len) {
    if (!sdf_ble_ota_protocol_validate_end(payload_len)) {
        ESP_LOGW(TAG, "END: unexpected payload (%u bytes)", (unsigned)payload_len);
        return false;
    }

    if (xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    if (!s_session.active || s_session.conn_handle != conn_handle) {
        xSemaphoreGive(s_session_lock);
        ESP_LOGW(TAG, "END: no matching open session");
        return false;
    }
    sdf_ota_handle_t handle = s_session.ota_handle;
    xSemaphoreGive(s_session_lock);

    esp_timer_stop(s_idle_timer);

    esp_err_t err = sdf_ota_verify_integrity(handle);
    if (err == ESP_OK) {
        err = sdf_ota_verify_and_commit(handle);
    }

    if (xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memset(&s_session, 0, sizeof(s_session));
        xSemaphoreGive(s_session_lock);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "END: verification/commit failed: %s", esp_err_to_name(err));
        sdf_ota_abort(handle);
        char notify_buf[96];
        snprintf(notify_buf, sizeof(notify_buf), "{\"status\":\"failed\",\"error\":\"%s\"}",
                 esp_err_to_name(err));
        sdf_ble_ota_notify(conn_handle, notify_buf);
        return true;
    }

    sdf_ble_ota_notify(conn_handle, "{\"status\":\"success\"}");
    return true;
}

bool sdf_ble_companion_handle_ota_write(void *ctx, uint16_t conn_handle, const uint8_t *data,
                                         size_t len) {
    (void)ctx;

    if (data == NULL || len == 0) {
        return false;
    }
    if (sdf_ble_ota_ensure_init() != ESP_OK) {
        return false;
    }

    uint8_t opcode = data[0];
    const uint8_t *payload = data + 1;
    size_t payload_len = len - 1;

    switch (opcode) {
    case SDF_BLE_OTA_OPCODE_BEGIN:
        return sdf_ble_ota_handle_begin(conn_handle, payload, payload_len);
    case SDF_BLE_OTA_OPCODE_CHUNK:
        return sdf_ble_ota_handle_chunk(conn_handle, payload, payload_len);
    case SDF_BLE_OTA_OPCODE_END:
        return sdf_ble_ota_handle_end(conn_handle, payload_len);
    default:
        ESP_LOGW(TAG, "Unknown OTA opcode 0x%02x", opcode);
        return false;
    }
}

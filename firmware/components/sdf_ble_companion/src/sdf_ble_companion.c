#include "sdf_ble_companion.h"
#include "sdf_storage.h"
#include "sdf_event_router.h"
#include "sdf_nuki_ble_transport.h"
#include "sdf_config.h"
#include "sdf_services.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_timer.h"

#define TAG "sdf_ble_companion"

#define SDF_BLE_COMPANION_MAX_CONNECTIONS 3
#define SDF_BLE_COMPANION_ATTR_MAX_LEN 512

#define SDF_BLE_COMPANION_AUTH_LOGIN 0x01
#define SDF_BLE_COMPANION_AUTH_REGISTER 0x02
#define SDF_BLE_COMPANION_AUTH_LOGOUT 0x00
#define SDF_BLE_COMPANION_AUTH_RESULT_PENDING 0x02
#define SDF_BLE_COMPANION_AUTH_RESULT_OK 0x01

/* Nuki re-pair request sentinel on the Config characteristic: a write whose
 * JSON body is `{"action":"nuki_repair"}` requests the admin-fingerprint-
 * gated re-pair flow instead of being treated as a config field update. No
 * new characteristic is added (NimBLE CCCD budget is already fully
 * committed by the 4 existing NOTIFY-capable characteristics), so this
 * reuses Config's existing authenticated write path and JSON convention. */
#define SDF_BLE_COMPANION_CONFIG_ACTION_KEY "action"
#define SDF_BLE_COMPANION_CONFIG_ACTION_NUKI_REPAIR "nuki_repair"

#define SDF_BLE_COMPANION_SVC_UUID128 \
    0x6f, 0x5e, 0x4d, 0x3c, 0x2b, 0x1a, 0x3d, 0x9e, \
    0x8a, 0x4f, 0x2b, 0x5c, 0x00, 0x00, 0x5a, 0x7d

#define SDF_BLE_COMPANION_AUTH_UUID128 \
    0x6f, 0x5e, 0x4d, 0x3c, 0x2b, 0x1a, 0x3d, 0x9e, \
    0x8a, 0x4f, 0x2b, 0x5c, 0x01, 0x00, 0x5a, 0x7d

#define SDF_BLE_COMPANION_CONFIG_UUID128 \
    0x6f, 0x5e, 0x4d, 0x3c, 0x2b, 0x1a, 0x3d, 0x9e, \
    0x8a, 0x4f, 0x2b, 0x5c, 0x02, 0x00, 0x5a, 0x7d

#define SDF_BLE_COMPANION_ENROLL_UUID128 \
    0x6f, 0x5e, 0x4d, 0x3c, 0x2b, 0x1a, 0x3d, 0x9e, \
    0x8a, 0x4f, 0x2b, 0x5c, 0x03, 0x00, 0x5a, 0x7d

#define SDF_BLE_COMPANION_OTA_UUID128 \
    0x6f, 0x5e, 0x4d, 0x3c, 0x2b, 0x1a, 0x3d, 0x9e, \
    0x8a, 0x4f, 0x2b, 0x5c, 0x04, 0x00, 0x5a, 0x7d

#define SDF_BLE_COMPANION_ADV_FAST_DURATION_MS 30000

// Slow advertising intervals (approx 1 second)
#define SDF_BLE_COMPANION_ADV_SLOW_INTERVAL_MIN BLE_GAP_ADV_ITVL_MS(1000)
#define SDF_BLE_COMPANION_ADV_SLOW_INTERVAL_MAX BLE_GAP_ADV_ITVL_MS(2000)

static sdf_ble_companion_connection_t s_connections[SDF_BLE_COMPANION_MAX_CONNECTIONS];
static sdf_ble_companion_callbacks_t s_callbacks = {0};
static bool s_initialized = false;
static SemaphoreHandle_t s_lock = NULL;

/* Scratch buffer used by the GATT access callbacks below (sdf_ble_companion_*
 * _access) to snapshot a characteristic's value under s_lock so it can be
 * handed to a user callback (on_config_write/on_enroll_write/on_ota_write/
 * auth parsing) after the lock is released - callbacks must never run while
 * s_lock is held, since they may themselves call back into this component.
 * A 512-byte array used to live as a stack-local in each of these functions;
 * that's fine size-wise on its own, but they all run on the NimBLE host
 * task's stack (a single shared, size-constrained task - only one GATT
 * access callback ever executes at a time on it), and stacking a 512B
 * buffer plus the rest of each callback's locals plus the NimBLE host's own
 * call depth was eating into an already tight budget. Since access to this
 * buffer is inherently serialized by the host task (no reentrancy across
 * these callbacks), a single shared static buffer is safe. */
static uint8_t s_gatt_scratch[SDF_BLE_COMPANION_ATTR_MAX_LEN];

static uint16_t s_auth_val_handle = 0;
static uint16_t s_config_val_handle = 0;
static uint16_t s_enroll_val_handle = 0;
static uint16_t s_ota_val_handle = 0;

static sdf_event_router_subscriber_t *s_enrollment_sub = NULL;
static sdf_event_router_subscriber_t *s_enrollment_failed_sub = NULL;

static esp_timer_handle_t s_adv_timer;

static uint8_t s_adv_data[31];
static uint8_t s_adv_data_len = 0;

// Forward declarations
static void sdf_ble_companion_adv_timer_cb(void *arg);

void sdf_ble_companion_start_advertising_fast(void);
void sdf_ble_companion_start_advertising_slow(void);
void sdf_ble_companion_start_advertising(void);

static sdf_ble_companion_connection_t *sdf_ble_companion_get_conn(uint16_t conn_handle);
static sdf_ble_companion_connection_t *sdf_ble_companion_get_free_conn(void);

static void sdf_ble_companion_adv_timer_cb(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Fast advertising duration expired, switching to slow advertising");
    sdf_ble_companion_start_advertising_slow();
}

static sdf_ble_companion_connection_t *sdf_ble_companion_get_conn(uint16_t conn_handle) {
    for (int i = 0; i < SDF_BLE_COMPANION_MAX_CONNECTIONS; i++) {
        if (s_connections[i].connected && s_connections[i].conn_handle == conn_handle) {
            return &s_connections[i];
        }
    }
    return NULL;
}

static sdf_ble_companion_connection_t *sdf_ble_companion_get_free_conn(void) {
    for (int i = 0; i < SDF_BLE_COMPANION_MAX_CONNECTIONS; i++) {
        if (!s_connections[i].connected) {
            return &s_connections[i];
        }
    }
    return NULL;
}

static void sdf_ble_companion_enrollment_complete_handler(void *ctx,
                                                           const sdf_event_router_event_t *event) {
    (void)ctx;
    if (!event) return;

    uint16_t user_id = event->payload.enrollment_complete.user_id;
    ESP_LOGI(TAG, "Enrollment complete for user_id=%u", (unsigned)user_id);

    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "status", "success");
    cJSON_AddNumberToObject(root, "user_id", user_id);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str) {
        // Broadcast to all authenticated connections
        for (int i = 0; i < SDF_BLE_COMPANION_MAX_CONNECTIONS; i++) {
            if (s_connections[i].connected &&
                s_connections[i].auth_state == SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
                sdf_ble_companion_notify_enroll(s_connections[i].conn_handle,
                                                 (const uint8_t *)json_str, strlen(json_str));
            }
        }
        free(json_str);
    }
}

static void sdf_ble_companion_enrollment_failed_handler(void *ctx,
                                                         const sdf_event_router_event_t *event) {
    (void)ctx;
    if (!event) return;

    uint8_t step = event->payload.enrollment_failed.step;
    int8_t error_code = event->payload.enrollment_failed.error_code;
    ESP_LOGW(TAG, "Enrollment failed at step=%u error=%d", (unsigned)step, (int)error_code);

    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "status", "failed");
    cJSON_AddNumberToObject(root, "step", step);
    cJSON_AddNumberToObject(root, "error_code", error_code);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str) {
        // Broadcast to all authenticated connections
        for (int i = 0; i < SDF_BLE_COMPANION_MAX_CONNECTIONS; i++) {
            if (s_connections[i].connected &&
                s_connections[i].auth_state == SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
                sdf_ble_companion_notify_enroll(s_connections[i].conn_handle,
                                                 (const uint8_t *)json_str, strlen(json_str));
            }
        }
        free(json_str);
    }
}

static int sdf_ble_companion_auth_access(uint16_t conn_handle, uint16_t attr_handle,
                                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;

    /* Can still be invoked by the NimBLE host after sdf_ble_companion_deinit()
     * has run (the GATT characteristic is never unregistered) - bail out
     * before touching s_connections/s_callbacks, which deinit may have
     * already reset. */
    if (!s_initialized) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "auth_access: lock contention");
        return BLE_ATT_ERR_UNLIKELY;
    }

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn) {
        xSemaphoreGive(s_lock);
        return BLE_ATT_ERR_INVALID_HANDLE;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *resp = (conn->auth_state == SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED)
                               ? "AUTH_OK" : "AUTH_REQUIRED";
        xSemaphoreGive(s_lock);
        int rc = os_mbuf_append(ctxt->om, resp, strlen(resp));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        if (len >= 2 && len < SDF_BLE_COMPANION_ATTR_MAX_LEN) {
            uint8_t *buf = s_gatt_scratch;
            os_mbuf_copydata(om, 0, len, buf);
            uint8_t cmd = buf[0];
            if (cmd == SDF_BLE_COMPANION_AUTH_LOGIN ||
                cmd == SDF_BLE_COMPANION_AUTH_REGISTER) {
                size_t username_len = buf[1];
                if (username_len == 0 ||
                    username_len >= SDF_STORAGE_WEB_USER_NAME_MAX ||
                    len != 2 + username_len + SDF_STORAGE_WEB_USER_HASH_LEN) {
                    xSemaphoreGive(s_lock);
                    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                }

                memcpy(conn->username, &buf[2], username_len);
                conn->username[username_len] = '\0';
                memcpy(conn->password_hash, &buf[2 + username_len],
                       SDF_STORAGE_WEB_USER_HASH_LEN);

                if (cmd == SDF_BLE_COMPANION_AUTH_LOGIN) {
                    sdf_storage_web_user_t user = {0};
                    uint8_t index = 0;
                    esp_err_t err = sdf_storage_web_user_find_by_name(
                        conn->username, &user, &index);
                    if (err != ESP_OK ||
                        !sdf_services_web_auth_verify_login(
                            &user, conn->password_hash,
                            SDF_STORAGE_WEB_USER_HASH_LEN)) {
                        conn->auth_state =
                            SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
                        conn->auth_pending = false;
                        memset(conn->password_hash, 0,
                               sizeof(conn->password_hash));
                        xSemaphoreGive(s_lock);
                        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
                    }

                    xSemaphoreGive(s_lock);
                    if (sdf_ble_companion_set_authenticated(conn_handle, true) !=
                        ESP_OK) {
                        return BLE_ATT_ERR_UNLIKELY;
                    }
                    return 0;
                }

                conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_PENDING;
                conn->auth_pending = true;
                /* Copy callback pointer before releasing lock */
                void (*on_auth_req)(void *, const char *, const uint8_t *, size_t) =
                    s_callbacks.on_auth_request;
                void *cb_ctx = s_callbacks.ctx;
                char username_copy[SDF_STORAGE_WEB_USER_NAME_MAX];
                uint8_t hash_copy[SDF_STORAGE_WEB_USER_HASH_LEN];
                strlcpy(username_copy, conn->username, sizeof(username_copy));
                memcpy(hash_copy, conn->password_hash, SDF_STORAGE_WEB_USER_HASH_LEN);

                conn->auth_value_len = 1;
                conn->auth_value[0] = SDF_BLE_COMPANION_AUTH_RESULT_PENDING;
                xSemaphoreGive(s_lock);

                if (on_auth_req) {
                    on_auth_req(cb_ctx, username_copy, hash_copy,
                                SDF_STORAGE_WEB_USER_HASH_LEN);
                }
                return 0;
            } else if (cmd == SDF_BLE_COMPANION_AUTH_LOGOUT) {
                conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
                conn->auth_pending = false;
                memset(conn->username, 0, sizeof(conn->username));
                memset(conn->password_hash, 0, sizeof(conn->password_hash));
                conn->auth_value_len = 1;
                conn->auth_value[0] = SDF_BLE_COMPANION_AUTH_LOGOUT;
                xSemaphoreGive(s_lock);
                return 0;
            }
            xSemaphoreGive(s_lock);
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }
        xSemaphoreGive(s_lock);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    xSemaphoreGive(s_lock);
    return BLE_ATT_ERR_UNLIKELY;
}

/* Returns true if `data` is a well-formed
 * `{"action":"nuki_repair"}` request, false for any other Config write
 * (including malformed JSON, which is left for the normal on_config_write
 * passthrough to reject/ignore as it already does). */
static bool sdf_ble_companion_is_nuki_repair_request(const uint8_t *data, size_t len) {
    cJSON *root = cJSON_ParseWithLength((const char *)data, len);
    if (!root) {
        return false;
    }
    cJSON *action = cJSON_GetObjectItemCaseSensitive(root, SDF_BLE_COMPANION_CONFIG_ACTION_KEY);
    bool is_repair_request = cJSON_IsString(action) && action->valuestring != NULL &&
                              strcmp(action->valuestring,
                                     SDF_BLE_COMPANION_CONFIG_ACTION_NUKI_REPAIR) == 0;
    cJSON_Delete(root);
    return is_repair_request;
}

static int sdf_ble_companion_config_access(uint16_t conn_handle, uint16_t attr_handle,
                                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;

    /* See the equivalent guard in sdf_ble_companion_auth_access(). */
    if (!s_initialized) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "config_access: lock contention");
        return BLE_ATT_ERR_UNLIKELY;
    }

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        xSemaphoreGive(s_lock);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Serialize current config subset to JSON
        const sdf_config_t *cfg = sdf_config_get();
        if (!cfg) {
            xSemaphoreGive(s_lock);
            return BLE_ATT_ERR_UNLIKELY;
        }

        cJSON *root = cJSON_CreateObject();
        if (!root) {
            xSemaphoreGive(s_lock);
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        cJSON_AddNumberToObject(root, "checkin_interval_ms", cfg->checkin_interval_ms);
        cJSON_AddNumberToObject(root, "idle_before_sleep_ms", cfg->idle_before_sleep_ms);
        cJSON_AddNumberToObject(root, "post_wake_guard_ms", cfg->post_wake_guard_ms);
        cJSON_AddNumberToObject(root, "battery_default_percent", cfg->battery_default_percent);
        cJSON_AddBoolToObject(root, "ble_connect_on_demand", cfg->ble_connect_on_demand);
        cJSON_AddNumberToObject(root, "match_poll_interval_ms", cfg->match_poll_interval_ms);
        cJSON_AddNumberToObject(root, "battery_report_interval_ms", cfg->battery_report_interval_ms);
        cJSON_AddNumberToObject(root, "power_loop_interval_ms", cfg->power_loop_interval_ms);
        cJSON_AddNumberToObject(root, "failed_attempt_threshold", cfg->failed_attempt_threshold);
        cJSON_AddNumberToObject(root, "failed_attempt_window_ms", cfg->failed_attempt_window_ms);
        cJSON_AddNumberToObject(root, "lockout_duration_ms", cfg->lockout_duration_ms);

        char *json_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (!json_str) {
            xSemaphoreGive(s_lock);
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        size_t json_len = strlen(json_str);
        xSemaphoreGive(s_lock);

        int rc = os_mbuf_append(ctxt->om, json_str, json_len);
        free(json_str);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        if (len < SDF_BLE_COMPANION_ATTR_MAX_LEN) {
            os_mbuf_copydata(om, 0, len, conn->config_value);
            conn->config_value_len = len;
            void (*on_config)(void *, const uint8_t *, size_t) = s_callbacks.on_config_write;
            sdf_ble_companion_nuki_repair_request_cb on_nuki_repair =
                s_callbacks.on_nuki_repair_request;
            void *cb_ctx = s_callbacks.ctx;
            uint8_t *tmp = s_gatt_scratch;
            memcpy(tmp, conn->config_value, len);
            xSemaphoreGive(s_lock);

            if (sdf_ble_companion_is_nuki_repair_request(tmp, len)) {
                /* Only reachable after setup is complete - initial pairing
                 * during setup happens via the physical button flow, not
                 * this trigger. Rejected synchronously (no pending state
                 * entered), matching how an unauthenticated write is
                 * already rejected synchronously above. */
                if (sdf_services_get_setup_state() !=
                    SDF_SERVICES_SETUP_STATE_CLAIMED_COMPLETE) {
                    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
                }
                if (on_nuki_repair) {
                    on_nuki_repair(cb_ctx, conn_handle);
                }
                return 0;
            }

            if (on_config) {
                on_config(cb_ctx, tmp, len);
            }
            return 0;
        }
        xSemaphoreGive(s_lock);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    xSemaphoreGive(s_lock);
    return BLE_ATT_ERR_UNLIKELY;
}

static int sdf_ble_companion_enroll_access(uint16_t conn_handle, uint16_t attr_handle,
                                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;

    /* See the equivalent guard in sdf_ble_companion_auth_access(). */
    if (!s_initialized) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "enroll_access: lock contention");
        return BLE_ATT_ERR_UNLIKELY;
    }

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        xSemaphoreGive(s_lock);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        /* os_mbuf_append() only copies into an already-allocated mbuf chain,
         * it doesn't block or call back into this component, so there's no
         * need to snapshot conn->enroll_value into a scratch buffer first -
         * just append directly while still holding the lock. */
        int rc = os_mbuf_append(ctxt->om, conn->enroll_value, conn->enroll_value_len);
        xSemaphoreGive(s_lock);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        if (len < SDF_BLE_COMPANION_ATTR_MAX_LEN) {
            os_mbuf_copydata(om, 0, len, conn->enroll_value);
            conn->enroll_value_len = len;
            void (*on_enroll)(void *, const uint8_t *, size_t) = s_callbacks.on_enroll_write;
            void *cb_ctx = s_callbacks.ctx;
            uint8_t *tmp = s_gatt_scratch;
            memcpy(tmp, conn->enroll_value, len);
            xSemaphoreGive(s_lock);
            if (on_enroll) {
                on_enroll(cb_ctx, tmp, len);
            }
            return 0;
        }
        xSemaphoreGive(s_lock);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    xSemaphoreGive(s_lock);
    return BLE_ATT_ERR_UNLIKELY;
}

static int sdf_ble_companion_ota_access(uint16_t conn_handle, uint16_t attr_handle,
                                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;

    /* See the equivalent guard in sdf_ble_companion_auth_access(). */
    if (!s_initialized) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "ota_access: lock contention");
        return BLE_ATT_ERR_UNLIKELY;
    }

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        xSemaphoreGive(s_lock);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        /* See the equivalent comment in sdf_ble_companion_enroll_access():
         * os_mbuf_append() doesn't block or call back into this component,
         * so append directly from conn->ota_value while still locked. */
        int rc = os_mbuf_append(ctxt->om, conn->ota_value, conn->ota_value_len);
        xSemaphoreGive(s_lock);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        if (len < SDF_BLE_COMPANION_ATTR_MAX_LEN) {
            os_mbuf_copydata(om, 0, len, conn->ota_value);
            conn->ota_value_len = len;
            sdf_ble_companion_ota_write_cb on_ota = s_callbacks.on_ota_write;
            void *cb_ctx = s_callbacks.ctx;
            uint8_t *tmp = s_gatt_scratch;
            memcpy(tmp, conn->ota_value, len);
            xSemaphoreGive(s_lock);
            /* Unlike the other characteristics, a malformed/out-of-protocol
             * OTA write must be rejected at the GATT layer (non-zero ATT
             * error, no notify) rather than silently accepted - the failed
             * write itself is the client's synchronous signal per the BLE
             * OTA chunked-transfer wire format. */
            if (on_ota && !on_ota(cb_ctx, conn_handle, tmp, len)) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            return 0;
        }
        xSemaphoreGive(s_lock);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    xSemaphoreGive(s_lock);
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def s_characteristics[] = {
    {
        .uuid = BLE_UUID128_DECLARE(SDF_BLE_COMPANION_AUTH_UUID128),
        .access_cb = sdf_ble_companion_auth_access,
        /* _ENC requires the link to be encrypted (paired/bonded) before a
         * read or write is allowed; the stack triggers pairing on the first
         * access if the link isn't already secured. These characteristics
         * carry password hashes, WiFi credentials and OTA URLs, so they must
         * not be reachable over a plaintext connection. */
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                 BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_auth_val_handle,
    },
    {
        .uuid = BLE_UUID128_DECLARE(SDF_BLE_COMPANION_CONFIG_UUID128),
        .access_cb = sdf_ble_companion_config_access,
        /* _ENC requires the link to be encrypted (paired/bonded) before a
         * read or write is allowed; the stack triggers pairing on the first
         * access if the link isn't already secured. These characteristics
         * carry password hashes, WiFi credentials and OTA URLs, so they must
         * not be reachable over a plaintext connection. */
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                 BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_config_val_handle,
    },
    {
        .uuid = BLE_UUID128_DECLARE(SDF_BLE_COMPANION_ENROLL_UUID128),
        .access_cb = sdf_ble_companion_enroll_access,
        /* _ENC requires the link to be encrypted (paired/bonded) before a
         * read or write is allowed; the stack triggers pairing on the first
         * access if the link isn't already secured. These characteristics
         * carry password hashes, WiFi credentials and OTA URLs, so they must
         * not be reachable over a plaintext connection. */
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                 BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_enroll_val_handle,
    },
    {
        .uuid = BLE_UUID128_DECLARE(SDF_BLE_COMPANION_OTA_UUID128),
        .access_cb = sdf_ble_companion_ota_access,
        /* _ENC requires the link to be encrypted (paired/bonded) before a
         * read or write is allowed; the stack triggers pairing on the first
         * access if the link isn't already secured. These characteristics
         * carry password hashes, WiFi credentials and OTA URLs, so they must
         * not be reachable over a plaintext connection. */
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                 BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_ota_val_handle,
    },
    { 0 }
};

static const struct ble_gatt_svc_def s_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(SDF_BLE_COMPANION_SVC_UUID128),
        .characteristics = s_characteristics,
    },
    { 0 }
};

static int sdf_ble_companion_gap_event(struct ble_gap_event *event, void *arg) {
    /* See the equivalent guard in sdf_ble_companion_auth_access(): the GAP
     * callback is never unregistered by sdf_ble_companion_deinit() either. */
    if (!s_initialized) {
        return 0;
    }

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Connected, conn_handle=%d", event->connect.conn_handle);

                bool slot_claimed = false;
                if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
                    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_free_conn();
                    if (conn) {
                        /* Full zero, not a field-by-field reset: this slot may be
                         * reused from a previous connection (see the equivalent
                         * memset in BLE_GAP_EVENT_DISCONNECT below), and stale
                         * config_value/enroll_value/ota_value/password_hash from
                         * that prior session must not be readable by whoever
                         * ends up authenticated on this new one. */
                        memset(conn, 0, sizeof(*conn));
                        conn->conn_handle = event->connect.conn_handle;
                        conn->connected = true;
                        conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
                        slot_claimed = true;
                    }
                    xSemaphoreGive(s_lock);
                } else {
                    ESP_LOGW(TAG, "gap_event connect: lock contention");
                }

                if (!slot_claimed) {
                    /* All SDF_BLE_COMPANION_MAX_CONNECTIONS slots are in use (or
                     * the lock was contended) - this link can never be tracked,
                     * authenticated, or torn down by this component, so it must
                     * not be left open. Terminate it immediately rather than
                     * leaking a NimBLE connection slot indefinitely. */
                    ESP_LOGW(TAG, "No free connection slot for conn_handle=%d, terminating",
                             event->connect.conn_handle);
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    break;
                }

                // Request MTU exchange after connection
                int rc = ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);
                if (rc != 0) {
                    ESP_LOGW(TAG, "Failed to request MTU exchange: %d", rc);
                }
            } else {
                ESP_LOGW(TAG, "Connection failed: %d", event->connect.status);
            }
            break;
        }
        case BLE_GAP_EVENT_DISCONNECT: {
            ESP_LOGI(TAG, "Disconnected, conn_handle=%d, reason=%d",
                     event->disconnect.conn.conn_handle, event->disconnect.reason);

            if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
                for (int i = 0; i < SDF_BLE_COMPANION_MAX_CONNECTIONS; i++) {
                    if (s_connections[i].connected &&
                        s_connections[i].conn_handle == event->disconnect.conn.conn_handle) {
                        /* Full zero (not just connected/auth_state) so the
                         * credential and GATT-buffer fields don't linger in
                         * memory once this session ends, and so the slot starts
                         * clean if/when BLE_GAP_EVENT_CONNECT reclaims it. */
                        memset(&s_connections[i], 0, sizeof(s_connections[i]));
                        break;
                    }
                }
                xSemaphoreGive(s_lock);
            } else {
                ESP_LOGW(TAG, "gap_event disconnect: lock contention");
            }
            sdf_ble_companion_start_advertising_fast();
            break;
        }
        case BLE_GAP_EVENT_ADV_COMPLETE: {
            ESP_LOGI(TAG, "Advertising complete: %d", event->adv_complete.reason);
            break;
        }
        case BLE_GAP_EVENT_MTU: {
            ESP_LOGI(TAG, "MTU update: conn_handle=%d, mtu=%d",
                     event->mtu.conn_handle, event->mtu.value);
            break;
        }
        default:
            break;
    }
    return 0;
}

static void sdf_ble_companion_on_host_sync(void *ctx) {
    (void)ctx;
    ESP_LOGI(TAG, "Shared NimBLE host synced");
    sdf_ble_companion_start_advertising();
}

void sdf_ble_companion_start_advertising_fast(void) {
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN,
        .itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX,
    };

    int rc = ble_gap_adv_set_data(s_adv_data, s_adv_data_len);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, sdf_ble_companion_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start fast advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "Fast advertising started");
    }

    // Cancel any existing timer and arm a new one to switch to slow advertising after 30 seconds
    esp_timer_stop(s_adv_timer);
    esp_timer_start_once(s_adv_timer, SDF_BLE_COMPANION_ADV_FAST_DURATION_MS * 1000);
}

void sdf_ble_companion_start_advertising_slow(void) {
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = SDF_BLE_COMPANION_ADV_SLOW_INTERVAL_MIN,
        .itvl_max = SDF_BLE_COMPANION_ADV_SLOW_INTERVAL_MAX,
    };

    int rc = ble_gap_adv_set_data(s_adv_data, s_adv_data_len);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, sdf_ble_companion_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start slow advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "Slow advertising started");
    }
}

// Keep the old function name for compatibility
void sdf_ble_companion_start_advertising(void) {
    sdf_ble_companion_start_advertising_fast();
}

static void sdf_ble_companion_build_adv_data(void) {
    // Build advertisement data with device name and service UUID
    uint8_t adv_data[31];
    size_t offset = 0;
    
    // Flags: LE General Discoverable Mode, BR/EDR Not Supported
    adv_data[offset++] = 0x02;
    adv_data[offset++] = 0x01;
    adv_data[offset++] = 0x06;
    
    // Service UUID (128-bit)
    adv_data[offset++] = 0x11;  // Length: 17 bytes (1 + 16)
    adv_data[offset++] = 0x06;  // Complete 128-bit UUIDs
    memcpy(&adv_data[offset], (uint8_t[]){SDF_BLE_COMPANION_SVC_UUID128}, 16);
    offset += 16;
    
    // Device name (shorter to fit in 31 bytes)
    const char *device_name = "SDF";
    size_t name_len = strlen(device_name);
    adv_data[offset++] = name_len + 1;
    adv_data[offset++] = 0x09;  // Complete Local Name
    memcpy(&adv_data[offset], device_name, name_len);
    offset += name_len;
    
    // Store for later use
    memcpy(s_adv_data, adv_data, offset);
    s_adv_data_len = offset;
}

static int sdf_ble_companion_register_gatt(void *ctx) {
    (void)ctx;

    sdf_ble_companion_build_adv_data();

    int rc = ble_gatts_count_cfg(s_svc_defs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATTS count cfg failed: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(s_svc_defs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATTS add svcs failed: %d", rc);
    }
    return rc;
}

esp_err_t sdf_ble_companion_init(const sdf_ble_companion_callbacks_t *callbacks) {
    if (s_initialized) {
        return ESP_OK;
    }

    if (callbacks) {
        s_callbacks = *callbacks;
    }

    /* s_lock deliberately survives a deinit/init cycle (see
     * sdf_ble_companion_deinit()) rather than being recreated here every
     * time, so only create it the first time. */
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    memset(s_connections, 0, sizeof(s_connections));

    // Initialize the advertising timer
    esp_timer_create_args_t timer_args = {
        .callback = sdf_ble_companion_adv_timer_cb,
        .arg = NULL,
        .name = "sdf_ble_adv_timer",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_adv_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create advertising timer: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }

    if (sdf_nuki_ble_register_server_service(sdf_ble_companion_register_gatt,
                                             sdf_ble_companion_on_host_sync,
                                             NULL) != 0) {
        esp_timer_delete(s_adv_timer);
        s_adv_timer = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    // Subscribe to enrollment events
    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,
                                      SDF_EVENT_ROUTER_PRIO_NORMAL,
                                      sdf_ble_companion_enrollment_complete_handler,
                                      NULL, &s_enrollment_sub);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to subscribe to enrollment complete: %s", esp_err_to_name(err));
    }

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_FAILED,
                                      SDF_EVENT_ROUTER_PRIO_NORMAL,
                                      sdf_ble_companion_enrollment_failed_handler,
                                      NULL, &s_enrollment_failed_sub);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to subscribe to enrollment failed: %s", esp_err_to_name(err));
    }

    s_initialized = true;
    ESP_LOGI(TAG, "BLE Companion Service registered with shared NimBLE host");
    return ESP_OK;
}

esp_err_t sdf_ble_companion_deinit(void) {
    if (!s_initialized) {
        return ESP_OK;
    }

    /* The NimBLE host task can be running one of the GATT access callbacks
     * or sdf_ble_companion_gap_event() concurrently with this call (it isn't
     * the caller's task). Neither the GATT characteristics nor the GAP
     * callback are unregistered below - there's no clean unregister path for
     * either through sdf_nuki_ble_transport - so events for this service can
     * keep arriving after deinit starts. Clearing s_initialized first (under
     * the lock) makes every one of those callbacks bail out early instead of
     * touching s_connections/s_callbacks mid-teardown, and taking the lock
     * here also blocks until any critical section that was *already*
     * in-flight when we got here has finished, before we go on to reset
     * state below. s_lock itself is intentionally never deleted (see
     * sdf_ble_companion_init()), so a callback that slips in after this
     * function returns still finds a valid, if now-inert, mutex instead of a
     * dangling handle. */
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_initialized = false;
        xSemaphoreGive(s_lock);
    } else {
        s_initialized = false;
    }

    for (int i = 0; i < SDF_BLE_COMPANION_MAX_CONNECTIONS; i++) {
        if (s_connections[i].connected) {
            ble_gap_terminate(s_connections[i].conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }

    if (s_enrollment_sub) {
        sdf_event_router_unsubscribe(s_enrollment_sub);
        s_enrollment_sub = NULL;
    }
    if (s_enrollment_failed_sub) {
        sdf_event_router_unsubscribe(s_enrollment_failed_sub);
        s_enrollment_failed_sub = NULL;
    }

    // Stop and delete the advertising timer
    if (s_adv_timer) {
        esp_timer_stop(s_adv_timer);
        esp_timer_delete(s_adv_timer);
        s_adv_timer = NULL;
    }

    memset(&s_callbacks, 0, sizeof(s_callbacks));
    s_initialized = false;
    ESP_LOGI(TAG, "BLE Companion Service deinitialized");
    return ESP_OK;
}

bool sdf_ble_companion_is_authenticated(uint16_t conn_handle) {
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    bool result = conn && conn->auth_state == SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED;
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t sdf_ble_companion_set_authenticated(uint16_t conn_handle, bool authenticated) {
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    if (authenticated) {
        conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED;
        conn->auth_pending = false;
        conn->auth_value_len = 1;
        conn->auth_value[0] = 0x01;
    } else {
        conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
        conn->auth_pending = false;
        conn->auth_value_len = 1;
        conn->auth_value[0] = 0x00;
    }

    bool connected = conn->connected;
    uint16_t handle = conn->conn_handle;
    uint8_t val[1] = {conn->auth_value[0]};
    xSemaphoreGive(s_lock);

    /* BLE call outside lock */
    if (connected) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(val, 1);
        if (om) {
            ble_gatts_notify_custom(handle, s_auth_val_handle, om);
        }
    }

    return ESP_OK;
}

esp_err_t sdf_ble_companion_reply_auth(const char *username, bool authorized) {
    if (!username) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint16_t found_handle = 0;
    bool found = false;
    for (int i = 0; i < SDF_BLE_COMPANION_MAX_CONNECTIONS; i++) {
        if (s_connections[i].connected && s_connections[i].auth_pending) {
            if (strncmp(s_connections[i].username, username, SDF_STORAGE_WEB_USER_NAME_MAX) == 0) {
                found_handle = s_connections[i].conn_handle;
                found = true;
                break;
            }
        }
    }
    xSemaphoreGive(s_lock);

    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    /* set_authenticated acquires the lock internally */
    return sdf_ble_companion_set_authenticated(found_handle, authorized);
}

esp_err_t sdf_ble_companion_reply_nuki_repair(uint16_t conn_handle, bool authorized) {
    char payload[32];
    int n = snprintf(payload, sizeof(payload), "{\"nuki_repair\":%s}",
                      authorized ? "true" : "false");
    if (n <= 0 || (size_t)n >= sizeof(payload)) {
        return ESP_ERR_INVALID_STATE;
    }
    /* notify_config acquires the lock internally and already handles a
     * connection that's gone or no longer authenticated (ESP_ERR_INVALID_STATE). */
    return sdf_ble_companion_notify_config(conn_handle, (const uint8_t *)payload, (size_t)n);
}

esp_err_t sdf_ble_companion_notify_config(uint16_t conn_handle, const uint8_t *data, size_t len) {
    if (!data || len == 0 || len >= SDF_BLE_COMPANION_ATTR_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || !conn->connected) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    if (conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(conn->config_value, data, len);
    conn->config_value_len = len;
    xSemaphoreGive(s_lock);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, s_config_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t sdf_ble_companion_notify_enroll(uint16_t conn_handle, const uint8_t *data, size_t len) {
    if (!data || len == 0 || len >= SDF_BLE_COMPANION_ATTR_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || !conn->connected) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    if (conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(conn->enroll_value, data, len);
    conn->enroll_value_len = len;
    xSemaphoreGive(s_lock);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, s_enroll_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t sdf_ble_companion_notify_ota(uint16_t conn_handle, const uint8_t *data, size_t len) {
    if (!data || len == 0 || len >= SDF_BLE_COMPANION_ATTR_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || !conn->connected) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    if (conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(conn->ota_value, data, len);
    conn->ota_value_len = len;
    xSemaphoreGive(s_lock);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, s_ota_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t sdf_ble_companion_broadcast_ota(const uint8_t *data, size_t len) {
    if (!data || len == 0 || len >= SDF_BLE_COMPANION_ATTR_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Copy connection handles for authenticated connections
    uint16_t conn_handles[SDF_BLE_COMPANION_MAX_CONNECTIONS];
    int count = 0;
    for (int i = 0; i < SDF_BLE_COMPANION_MAX_CONNECTIONS; i++) {
        if (s_connections[i].connected &&
            s_connections[i].auth_state == SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
            conn_handles[count++] = s_connections[i].conn_handle;
        }
    }
    xSemaphoreGive(s_lock);

    // Send notifications outside the lock
    for (int i = 0; i < count; i++) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
        if (om) {
            ble_gatts_notify_custom(conn_handles[i], s_ota_val_handle, om);
        }
    }

    return ESP_OK;
}

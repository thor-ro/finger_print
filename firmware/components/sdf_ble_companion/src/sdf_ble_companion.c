#include "sdf_ble_companion.h"
#include "sdf_storage.h"
#include "sdf_event_router.h"
#include "sdf_nuki_ble_transport.h"
#include "sdf_config.h"

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
#include "mbedtls/constant_time.h"
#include "cJSON.h"

#define TAG "sdf_ble_companion"

#define SDF_BLE_COMPANION_MAX_CONNECTIONS 3
#define SDF_BLE_COMPANION_ATTR_MAX_LEN 512

#define SDF_BLE_COMPANION_AUTH_LOGIN 0x01
#define SDF_BLE_COMPANION_AUTH_REGISTER 0x02
#define SDF_BLE_COMPANION_AUTH_LOGOUT 0x00
#define SDF_BLE_COMPANION_AUTH_RESULT_PENDING 0x02
#define SDF_BLE_COMPANION_AUTH_RESULT_OK 0x01

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

static sdf_ble_companion_connection_t s_connections[SDF_BLE_COMPANION_MAX_CONNECTIONS];
static sdf_ble_companion_callbacks_t s_callbacks = {0};
static bool s_initialized = false;
static SemaphoreHandle_t s_lock = NULL;

static uint16_t s_auth_val_handle = 0;
static uint16_t s_config_val_handle = 0;
static uint16_t s_enroll_val_handle = 0;
static uint16_t s_ota_val_handle = 0;

static sdf_event_router_subscriber_t *s_enrollment_sub = NULL;
static sdf_event_router_subscriber_t *s_enrollment_failed_sub = NULL;

static uint8_t s_adv_data[31];
static uint8_t s_adv_data_len = 0;

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

static void sdf_ble_companion_start_advertising(void);

static int sdf_ble_companion_auth_access(uint16_t conn_handle, uint16_t attr_handle,
                                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;

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
            uint8_t buf[SDF_BLE_COMPANION_ATTR_MAX_LEN];
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
                        mbedtls_ct_memcmp(user.password_hash,
                                          conn->password_hash,
                                          SDF_STORAGE_WEB_USER_HASH_LEN) != 0) {
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

static int sdf_ble_companion_config_access(uint16_t conn_handle, uint16_t attr_handle,
                                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;

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
            void *cb_ctx = s_callbacks.ctx;
            uint8_t tmp[SDF_BLE_COMPANION_ATTR_MAX_LEN];
            memcpy(tmp, conn->config_value, len);
            xSemaphoreGive(s_lock);
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
        uint8_t tmp[SDF_BLE_COMPANION_ATTR_MAX_LEN];
        uint16_t tmp_len = conn->enroll_value_len;
        memcpy(tmp, conn->enroll_value, tmp_len);
        xSemaphoreGive(s_lock);
        int rc = os_mbuf_append(ctxt->om, tmp, tmp_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        if (len < SDF_BLE_COMPANION_ATTR_MAX_LEN) {
            os_mbuf_copydata(om, 0, len, conn->enroll_value);
            conn->enroll_value_len = len;
            void (*on_enroll)(void *, const uint8_t *, size_t) = s_callbacks.on_enroll_write;
            void *cb_ctx = s_callbacks.ctx;
            uint8_t tmp[SDF_BLE_COMPANION_ATTR_MAX_LEN];
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
        uint8_t tmp[SDF_BLE_COMPANION_ATTR_MAX_LEN];
        uint16_t tmp_len = conn->ota_value_len;
        memcpy(tmp, conn->ota_value, tmp_len);
        xSemaphoreGive(s_lock);
        int rc = os_mbuf_append(ctxt->om, tmp, tmp_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        if (len < SDF_BLE_COMPANION_ATTR_MAX_LEN) {
            os_mbuf_copydata(om, 0, len, conn->ota_value);
            conn->ota_value_len = len;
            void (*on_ota)(void *, const uint8_t *, size_t) = s_callbacks.on_ota_write;
            void *cb_ctx = s_callbacks.ctx;
            uint8_t tmp[SDF_BLE_COMPANION_ATTR_MAX_LEN];
            memcpy(tmp, conn->ota_value, len);
            xSemaphoreGive(s_lock);
            if (on_ota) {
                on_ota(cb_ctx, tmp, len);
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
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_auth_val_handle,
    },
    {
        .uuid = BLE_UUID128_DECLARE(SDF_BLE_COMPANION_CONFIG_UUID128),
        .access_cb = sdf_ble_companion_config_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_config_val_handle,
    },
    {
        .uuid = BLE_UUID128_DECLARE(SDF_BLE_COMPANION_ENROLL_UUID128),
        .access_cb = sdf_ble_companion_enroll_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_enroll_val_handle,
    },
    {
        .uuid = BLE_UUID128_DECLARE(SDF_BLE_COMPANION_OTA_UUID128),
        .access_cb = sdf_ble_companion_ota_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
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
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Connected, conn_handle=%d", event->connect.conn_handle);

                if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
                    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_free_conn();
                    if (conn) {
                        conn->conn_handle = event->connect.conn_handle;
                        conn->connected = true;
                        conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
                        conn->auth_pending = false;
                        memset(conn->username, 0, sizeof(conn->username));
                        memset(conn->password_hash, 0, sizeof(conn->password_hash));
                    }
                    xSemaphoreGive(s_lock);
                } else {
                    ESP_LOGW(TAG, "gap_event connect: lock contention");
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
                        s_connections[i].connected = false;
                        s_connections[i].conn_handle = 0;
                        s_connections[i].auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
                        s_connections[i].auth_pending = false;
                        break;
                    }
                }
                xSemaphoreGive(s_lock);
            } else {
                ESP_LOGW(TAG, "gap_event disconnect: lock contention");
            }
            sdf_ble_companion_start_advertising();
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

void sdf_ble_companion_start_advertising(void) {
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
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising started");
    }
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

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    memset(s_connections, 0, sizeof(s_connections));

    if (sdf_nuki_ble_register_server_service(sdf_ble_companion_register_gatt,
                                             sdf_ble_companion_on_host_sync,
                                             NULL) != 0) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    // Subscribe to enrollment events
    esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,
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

    if (s_lock) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
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

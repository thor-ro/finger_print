#include "sdf_ble_companion.h"
#include "sdf_storage.h"
#include "sdf_event_router.h"
#include "sdf_nuki_ble_transport.h"

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

static uint8_t s_auth_value[SDF_BLE_COMPANION_ATTR_MAX_LEN];
static uint8_t s_config_value[SDF_BLE_COMPANION_ATTR_MAX_LEN];
static uint8_t s_enroll_value[SDF_BLE_COMPANION_ATTR_MAX_LEN];
static uint8_t s_ota_value[SDF_BLE_COMPANION_ATTR_MAX_LEN];
static uint16_t s_auth_value_len = 0;
static uint16_t s_config_value_len = 0;
static uint16_t s_enroll_value_len = 0;
static uint16_t s_ota_value_len = 0;

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

static void sdf_ble_companion_start_advertising(void);

static int sdf_ble_companion_auth_access(uint16_t conn_handle, uint16_t attr_handle,
                                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;
    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn) {
        return BLE_ATT_ERR_INVALID_HANDLE;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (conn->auth_state == SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
            const char *resp = "AUTH_OK";
            int rc = os_mbuf_append(ctxt->om, resp, strlen(resp));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        } else {
            const char *resp = "AUTH_REQUIRED";
            int rc = os_mbuf_append(ctxt->om, resp, strlen(resp));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
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
                        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
                    }

                    if (sdf_ble_companion_set_authenticated(conn_handle, true) !=
                        ESP_OK) {
                        return BLE_ATT_ERR_UNLIKELY;
                    }
                    return 0;
                }

                conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_PENDING;
                conn->auth_pending = true;
                if (s_callbacks.on_auth_request) {
                    s_callbacks.on_auth_request(s_callbacks.ctx,
                                                conn->username,
                                                conn->password_hash,
                                                SDF_STORAGE_WEB_USER_HASH_LEN);
                }

                s_auth_value_len = 1;
                s_auth_value[0] = SDF_BLE_COMPANION_AUTH_RESULT_PENDING;
                return 0;
            } else if (cmd == SDF_BLE_COMPANION_AUTH_LOGOUT) {
                conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
                conn->auth_pending = false;
                memset(conn->username, 0, sizeof(conn->username));
                memset(conn->password_hash, 0, sizeof(conn->password_hash));
                s_auth_value_len = 1;
                s_auth_value[0] = SDF_BLE_COMPANION_AUTH_LOGOUT;
                return 0;
            }
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int sdf_ble_companion_config_access(uint16_t conn_handle, uint16_t attr_handle,
                                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;
    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, s_config_value, s_config_value_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        if (len < SDF_BLE_COMPANION_ATTR_MAX_LEN) {
            os_mbuf_copydata(om, 0, len, s_config_value);
            s_config_value_len = len;

            if (s_callbacks.on_config_write) {
                s_callbacks.on_config_write(s_callbacks.ctx, s_config_value, len);
            }
            return 0;
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int sdf_ble_companion_enroll_access(uint16_t conn_handle, uint16_t attr_handle,
                                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;
    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, s_enroll_value, s_enroll_value_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        if (len < SDF_BLE_COMPANION_ATTR_MAX_LEN) {
            os_mbuf_copydata(om, 0, len, s_enroll_value);
            s_enroll_value_len = len;

            if (s_callbacks.on_enroll_write) {
                s_callbacks.on_enroll_write(s_callbacks.ctx, s_enroll_value, len);
            }
            return 0;
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int sdf_ble_companion_ota_access(uint16_t conn_handle, uint16_t attr_handle,
                                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;
    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, s_ota_value, s_ota_value_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        if (len < SDF_BLE_COMPANION_ATTR_MAX_LEN) {
            os_mbuf_copydata(om, 0, len, s_ota_value);
            s_ota_value_len = len;

            if (s_callbacks.on_ota_write) {
                s_callbacks.on_ota_write(s_callbacks.ctx, s_ota_value, len);
            }
            return 0;
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
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

                sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_free_conn();
                if (conn) {
                    conn->conn_handle = event->connect.conn_handle;
                    conn->connected = true;
                    conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
                    conn->auth_pending = false;
                    memset(conn->username, 0, sizeof(conn->username));
                    memset(conn->password_hash, 0, sizeof(conn->password_hash));
                }
            } else {
                ESP_LOGW(TAG, "Connection failed: %d", event->connect.status);
            }
            break;
        }
        case BLE_GAP_EVENT_DISCONNECT: {
            ESP_LOGI(TAG, "Disconnected, conn_handle=%d, reason=%d",
                     event->disconnect.conn.conn_handle, event->disconnect.reason);

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
    memset(s_auth_value, 0, sizeof(s_auth_value));
    memset(s_config_value, 0, sizeof(s_config_value));
    memset(s_enroll_value, 0, sizeof(s_enroll_value));
    memset(s_ota_value, 0, sizeof(s_ota_value));

    if (sdf_nuki_ble_register_server_service(sdf_ble_companion_register_gatt,
                                             sdf_ble_companion_on_host_sync,
                                             NULL) != 0) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_INVALID_STATE;
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
    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    return conn && conn->auth_state == SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED;
}

esp_err_t sdf_ble_companion_set_authenticated(uint16_t conn_handle, bool authenticated) {
    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn) {
        return ESP_ERR_NOT_FOUND;
    }

    if (authenticated) {
        conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED;
        conn->auth_pending = false;
        s_auth_value_len = 1;
        s_auth_value[0] = 0x01;
    } else {
        conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
        conn->auth_pending = false;
        s_auth_value_len = 1;
        s_auth_value[0] = 0x00;
    }

    if (conn->connected) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(s_auth_value, s_auth_value_len);
        if (om) {
            ble_gatts_notify_custom(conn->conn_handle, s_auth_val_handle, om);
        }
    }

    return ESP_OK;
}

esp_err_t sdf_ble_companion_reply_auth(const char *username, bool authorized) {
    if (!username) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < SDF_BLE_COMPANION_MAX_CONNECTIONS; i++) {
        if (s_connections[i].connected && s_connections[i].auth_pending) {
            if (strncmp(s_connections[i].username, username, SDF_STORAGE_WEB_USER_NAME_MAX) == 0) {
                return sdf_ble_companion_set_authenticated(s_connections[i].conn_handle, authorized);
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sdf_ble_companion_notify_config(uint16_t conn_handle, const uint8_t *data, size_t len) {
    if (!data || len == 0 || len >= SDF_BLE_COMPANION_ATTR_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || !conn->connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(s_config_value, data, len);
    s_config_value_len = len;

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

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || !conn->connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(s_enroll_value, data, len);
    s_enroll_value_len = len;

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

    sdf_ble_companion_connection_t *conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || !conn->connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(s_ota_value, data, len);
    s_ota_value_len = len;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, s_ota_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

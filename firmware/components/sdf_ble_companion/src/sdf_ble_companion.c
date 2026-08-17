#include "sdf_ble_companion.h"
#include "sdf_ble_companion_bond_state.h"
#include "sdf_ble_companion_gatt_scratch.h"
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
#include "esp_random.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_timer.h"

#define TAG "sdf_ble_companion"

#define SDF_BLE_COMPANION_MAX_CONNECTIONS 3
/* Derived from the staging module's capacity rather than restated, so the
 * attribute-length guards here and the buffer they stage into cannot drift. */
#define SDF_BLE_COMPANION_ATTR_MAX_LEN SDF_BLE_COMPANION_GATT_SCRATCH_LEN

/* LOGIN (single-message, [cmd][username][password_hash]) is retired by the
 * challenge-response protocol below - opcode 0x01 is deliberately left
 * unassigned (rather than reused) so an old web-companion app's LOGIN write
 * falls through to the unknown-opcode rejection instead of ever being
 * misinterpreted as something else. REGISTER and LOGOUT are unchanged. */
#define SDF_BLE_COMPANION_AUTH_REGISTER 0x02
#define SDF_BLE_COMPANION_AUTH_LOGOUT 0x00
#define SDF_BLE_COMPANION_AUTH_LOGIN_INIT 0x03
#define SDF_BLE_COMPANION_AUTH_LOGIN_VERIFY 0x04
#define SDF_BLE_COMPANION_AUTH_RESULT_PENDING 0x02
#define SDF_BLE_COMPANION_AUTH_RESULT_OK 0x01

/* Largest well-formed write the Auth characteristic accepts, over all four
 * commands - REGISTER is the longest:
 *
 *   LOGOUT       [cmd]                                            = 1
 *   LOGIN_INIT   [cmd][name_len][name]                            <= 2 + 31
 *   LOGIN_VERIFY [cmd][response(32)]                              = 1 + 32
 *   REGISTER     [cmd][name_len][name][password_hash(32)]         <= 2 + 31 + 32
 *
 * Usernames are NUL-terminated into a SDF_STORAGE_WEB_USER_NAME_MAX buffer
 * and the branches below require name_len < that, so the longest username on
 * the wire is NAME_MAX - 1. This is an outer bound checked before the
 * payload is copied or its command byte dispatched on; each command still
 * enforces its own exact length below. */
#define SDF_BLE_COMPANION_AUTH_WRITE_MAX_LEN \
    (2 + (SDF_STORAGE_WEB_USER_NAME_MAX - 1) + SDF_STORAGE_WEB_USER_HASH_LEN)

/* LOGIN_INIT read-branch challenge payload: [salt(16)][iteration_count(4,
 * little-endian)][nonce(16)] = 36 bytes. See tasks.md 6.2. */
#define SDF_BLE_COMPANION_LOGIN_CHALLENGE_WIRE_LEN \
    (SDF_STORAGE_WEB_USER_SALT_LEN + 4 + SDF_SERVICES_WEB_AUTH_NONCE_LEN)

/* Admin-fingerprint-gated action request sentinels on the Config
 * characteristic: a write whose JSON body is `{"action":"<name>"}` requests
 * one of these instead of being treated as a config field update. No new
 * characteristic is added (NimBLE CCCD budget is already fully committed by
 * the 4 existing NOTIFY-capable characteristics), so this reuses Config's
 * existing authenticated write path and JSON convention for all of them. */
#define SDF_BLE_COMPANION_CONFIG_ACTION_KEY "action"
#define SDF_BLE_COMPANION_CONFIG_ACTION_NUKI_REPAIR "nuki_repair"
#define SDF_BLE_COMPANION_CONFIG_ACTION_ENROLL_ADMIN "enroll_admin"
#define SDF_BLE_COMPANION_CONFIG_ACTION_ZB_JOIN "zb_join"

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

/* Default-mode advertising: sparse and allow-list-filtered (see
 * sdf_ble_companion_start_advertising_sparse() and design.md "Sparse,
 * Allow-List-Filtered Default Advertising"). Replaces the old
 * fast-then-slow-forever loop - there's no more unfiltered "fast discovery"
 * phase after boot/reconnect, since that would defeat the allow-list gate
 * for the whole first 30 seconds after every boot/disconnect. */
#define SDF_BLE_COMPANION_ADV_SPARSE_INTERVAL_MIN BLE_GAP_ADV_ITVL_MS(1000)
#define SDF_BLE_COMPANION_ADV_SPARSE_INTERVAL_MAX BLE_GAP_ADV_ITVL_MS(2000)

static sdf_ble_companion_connection_t s_connections[SDF_BLE_COMPANION_MAX_CONNECTIONS];
static sdf_ble_companion_callbacks_t s_callbacks = {0};
static bool s_initialized = false;
static SemaphoreHandle_t s_lock = NULL;

/* Allow-list membership and failed-login counters for the BLE Companion
 * trust model (see sdf_ble_companion_bond_state.h). Protected by s_lock,
 * same as s_connections. */
static sdf_ble_companion_bond_state_t s_bond_state;

/* Guards sdf_ble_companion_seed_allow_list() so it runs once per s_bond_state
 * lifetime. Cleared in sdf_ble_companion_init() alongside the
 * sdf_ble_companion_bond_state_init() that resets the state it seeds. */
static bool s_allow_list_seeded = false;

/* The buffer the Config/Enrollment/OTA writes stage into - so a payload can
 * outlive the s_lock release and be handed to a user callback, without the
 * 512-byte stack frames on the NimBLE host task that were audit finding A14
 * - now lives in sdf_ble_companion_gatt_scratch.c behind an acquire/release
 * pair, rather than as a static array in scope of this whole file. Ownership
 * is checked there instead of resting on a comment asserting that the host
 * task serialises every user. Only sdf_ble_companion_stage_write() below
 * touches it; the notify_* paths, which run on other tasks, must not. */

static uint16_t s_auth_val_handle = 0;
static uint16_t s_config_val_handle = 0;
static uint16_t s_enroll_val_handle = 0;
static uint16_t s_ota_val_handle = 0;



/* One-shot timeout for the Admin-Fingerprint-Gated Device Pairing Window
 * (see sdf_ble_companion_open_pairing_window()). Armed when the window
 * opens; stopped early if a device is admitted before it fires. */
static esp_timer_handle_t s_pairing_window_timer;

static uint8_t s_adv_data[31];
static uint8_t s_adv_data_len = 0;

// Forward declarations
static void sdf_ble_companion_pairing_window_timer_cb(void *arg);
static void sdf_ble_companion_push_allow_list(void);
static void sdf_ble_companion_restart_advertising(void);

void sdf_ble_companion_start_advertising_sparse(void);
void sdf_ble_companion_start_advertising_pairing(void);

static sdf_ble_companion_connection_t *sdf_ble_companion_get_conn(uint16_t conn_handle);
static sdf_ble_companion_connection_t *sdf_ble_companion_get_free_conn(void);

static void sdf_ble_companion_pairing_window_timer_cb(void *arg) {
    (void)arg;
    bool was_open = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        was_open = sdf_ble_companion_bond_window_is_open(&s_bond_state);
        sdf_ble_companion_bond_close_window(&s_bond_state);
        xSemaphoreGive(s_lock);
    } else {
        ESP_LOGW(TAG, "pairing_window_timer_cb: lock contention");
    }

    if (was_open) {
        ESP_LOGI(TAG, "BLE Companion pairing window closed (timeout, no device admitted)");
        sdf_ble_companion_restart_advertising();
    }
    /* If the window was already closed (a device beat the clock and was
     * admitted via BLE_GAP_EVENT_ENC_CHANGE, which stops this timer before
     * it can fire), this callback firing anyway would just be a lost race
     * against esp_timer_stop() - treat it as a no-op rather than reverting
     * advertising out from under an in-progress or already-restarted mode. */
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
        if (conn->auth_state == SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED) {
            const char *resp = "AUTH_OK";
            xSemaphoreGive(s_lock);
            int rc = os_mbuf_append(ctxt->om, resp, strlen(resp));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        } else if (conn->auth_state == SDF_BLE_COMPANION_AUTH_STATE_LOGIN_CHALLENGE_ISSUED) {
            /* Deliver the LOGIN_INIT challenge issued for this connection:
             * write-then-read, not write-then-notify - see design.md
             * "Challenge delivery" decision. Wire layout: [salt(16)]
             * [iteration_count(4, little-endian)][nonce(16)]. */
            uint8_t payload[SDF_BLE_COMPANION_LOGIN_CHALLENGE_WIRE_LEN];
            memcpy(payload, conn->pending_login_challenge.salt, SDF_STORAGE_WEB_USER_SALT_LEN);
            uint32_t iter = conn->pending_login_challenge.iteration_count;
            payload[SDF_STORAGE_WEB_USER_SALT_LEN + 0] = (uint8_t)(iter & 0xFF);
            payload[SDF_STORAGE_WEB_USER_SALT_LEN + 1] = (uint8_t)((iter >> 8) & 0xFF);
            payload[SDF_STORAGE_WEB_USER_SALT_LEN + 2] = (uint8_t)((iter >> 16) & 0xFF);
            payload[SDF_STORAGE_WEB_USER_SALT_LEN + 3] = (uint8_t)((iter >> 24) & 0xFF);
            memcpy(payload + SDF_STORAGE_WEB_USER_SALT_LEN + 4,
                   conn->pending_login_challenge.nonce, SDF_SERVICES_WEB_AUTH_NONCE_LEN);
            xSemaphoreGive(s_lock);
            int rc = os_mbuf_append(ctxt->om, payload, sizeof(payload));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        } else {
            const char *resp = "AUTH_REQUIRED";
            xSemaphoreGive(s_lock);
            int rc = os_mbuf_append(ctxt->om, resp, strlen(resp));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        struct os_mbuf *om = ctxt->om;
        size_t len = OS_MBUF_PKTLEN(om);
        /* Floor of 1, not 2: LOGOUT carries no operands and is exactly one
         * byte. The LOGIN_INIT and REGISTER branches read buf[1] as a
         * username length *before* validating len against it, so at len == 1
         * that byte is past what os_mbuf_copydata() wrote - hence the
         * zero-initialisation below, which makes it a deterministic 0 that
         * their `username_len == 0` check rejects. Both would reject a
         * 1-byte write regardless (len != 2 + username_len can't hold at
         * len == 1), but not off an uninitialised read.
         *
         * The payload goes on the stack rather than through the shared GATT
         * staging buffer: nothing here reads buf after s_lock is given (the
         * post-lock on_auth_req call receives username_copy and
         * password_hash, both stack locals), so auth needs no storage that
         * outlives the lock. At AUTH_WRITE_MAX_LEN bytes this is nothing
         * like the 512-byte frame that made A14 a finding. */
        if (len >= 1 && len <= SDF_BLE_COMPANION_AUTH_WRITE_MAX_LEN) {
            uint8_t buf[SDF_BLE_COMPANION_AUTH_WRITE_MAX_LEN] = {0};
            os_mbuf_copydata(om, 0, len, buf);
            uint8_t cmd = buf[0];
            if (cmd == SDF_BLE_COMPANION_AUTH_LOGIN_INIT) {
                size_t username_len = buf[1];
                if (username_len == 0 ||
                    username_len >= SDF_STORAGE_WEB_USER_NAME_MAX ||
                    len != 2 + username_len) {
                    xSemaphoreGive(s_lock);
                    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                }

                memcpy(conn->username, &buf[2], username_len);
                conn->username[username_len] = '\0';

                uint8_t nonce[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
                esp_fill_random(nonce, sizeof(nonce));

                sdf_storage_web_user_t user = {0};
                uint8_t index = 0;
                esp_err_t find_err =
                    sdf_storage_web_user_find_by_name(conn->username, &user, &index);

                sdf_services_web_auth_challenge_t challenge;
                if (find_err == ESP_OK) {
                    challenge = sdf_services_web_auth_make_login_challenge(&user, nonce);
                } else {
                    /* Unknown username: deterministic pseudo-salt challenge,
                     * same shape as a real account's - see design.md
                     * "Username-enumeration mitigation". No LOGIN_VERIFY can
                     * ever match this. */
                    uint8_t pseudo_key[SDF_STORAGE_WEB_PSEUDO_SALT_KEY_LEN];
                    sdf_storage_web_pseudo_salt_key_load_or_generate(pseudo_key);
                    challenge = sdf_services_web_auth_make_pseudo_challenge(
                        pseudo_key, conn->username, nonce);
                }

                conn->pending_login_challenge = challenge;
                conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_LOGIN_CHALLENGE_ISSUED;
                conn->auth_pending = false;
                xSemaphoreGive(s_lock);
                /* Client follows up with a characteristic read to fetch the
                 * challenge fields - see the READ_CHR branch above. */
                return 0;
            } else if (cmd == SDF_BLE_COMPANION_AUTH_LOGIN_VERIFY) {
                if (len != 1 + SDF_SERVICES_WEB_AUTH_RESPONSE_LEN) {
                    xSemaphoreGive(s_lock);
                    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                }
                if (conn->auth_state != SDF_BLE_COMPANION_AUTH_STATE_LOGIN_CHALLENGE_ISSUED) {
                    /* No outstanding nonce for this connection: either
                     * LOGIN_VERIFY arrived without a prior LOGIN_INIT, or
                     * it's a replay of an already-consumed nonce (state was
                     * cleared below after the first attempt). */
                    xSemaphoreGive(s_lock);
                    return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
                }

                uint8_t response[SDF_SERVICES_WEB_AUTH_RESPONSE_LEN];
                memcpy(response, &buf[1], sizeof(response));
                char username_copy[SDF_STORAGE_WEB_USER_NAME_MAX];
                strlcpy(username_copy, conn->username, sizeof(username_copy));
                uint8_t nonce_copy[SDF_SERVICES_WEB_AUTH_NONCE_LEN];
                memcpy(nonce_copy, conn->pending_login_challenge.nonce, sizeof(nonce_copy));

                /* Single-use: invalidate the challenge/nonce now, before
                 * verifying, so this nonce can never be replayed regardless
                 * of the outcome below. */
                conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
                memset(&conn->pending_login_challenge, 0, sizeof(conn->pending_login_challenge));

                sdf_storage_web_user_t user = {0};
                uint8_t index = 0;
                esp_err_t find_err =
                    sdf_storage_web_user_find_by_name(username_copy, &user, &index);
                bool ok = find_err == ESP_OK &&
                          sdf_services_web_auth_verify_response(
                              user.stretched_credential, nonce_copy, sizeof(nonce_copy),
                              response, sizeof(response));

                if (!ok) {
                    conn->auth_pending = false;

                    /* Failed BLE login lockout: keyed by the peer's
                     * resolved identity address (not conn_handle), so
                     * the counter survives disconnect/reconnect. See
                     * sdf_ble_companion_bond_state.h. LOGIN_INIT never
                     * reaches here, so only LOGIN_VERIFY outcomes count. */
                    struct ble_gap_conn_desc desc;
                    sdf_ble_companion_addr_t identity = {0};
                    bool evict = false;
                    if (ble_gap_conn_find(conn_handle, &desc) == 0) {
                        identity.type = desc.peer_id_addr.type;
                        memcpy(identity.val, desc.peer_id_addr.val,
                               sizeof(identity.val));
                        uint8_t count = sdf_ble_companion_bond_note_login_failure(
                            &s_bond_state, &identity);
                        ESP_LOGW(TAG,
                                 "BLE Companion LOGIN_VERIFY failed, conn_handle=%d, "
                                 "failure_count=%u",
                                 conn_handle, (unsigned)count);
                        if (sdf_ble_companion_bond_should_evict(count)) {
                            evict = true;
                            sdf_ble_companion_bond_allow_list_remove(&s_bond_state,
                                                                      &identity);
                        }
                    } else {
                        ESP_LOGW(TAG,
                                 "BLE Companion LOGIN_VERIFY failed, conn_handle=%d "
                                 "(no connection descriptor, lockout counter not updated)",
                                 conn_handle);
                    }
                    xSemaphoreGive(s_lock);

                    if (evict) {
                        /* Bond store deletion, allow-list push and connection
                         * termination all make NimBLE calls, so they happen
                         * outside s_lock like every other BLE call in this
                         * file. */
                        ESP_LOGW(TAG,
                                 "BLE Companion failed-login threshold reached, "
                                 "evicting bond and terminating connection: "
                                 "conn_handle=%d",
                                 conn_handle);
                        ble_addr_t store_addr = {
                            .type = identity.type,
                        };
                        memcpy(store_addr.val, identity.val, sizeof(store_addr.val));
                        int rc = ble_store_util_delete_peer(&store_addr);
                        if (rc != 0) {
                            ESP_LOGW(TAG, "ble_store_util_delete_peer failed: %d", rc);
                        }
                        sdf_ble_companion_push_allow_list();
                        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    }
                    return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
                }

                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(conn_handle, &desc) == 0) {
                    sdf_ble_companion_addr_t identity;
                    identity.type = desc.peer_id_addr.type;
                    memcpy(identity.val, desc.peer_id_addr.val, sizeof(identity.val));
                    sdf_ble_companion_bond_note_login_success(&s_bond_state, &identity);
                }
                xSemaphoreGive(s_lock);
                if (sdf_ble_companion_set_authenticated(conn_handle, true) !=
                    ESP_OK) {
                    return BLE_ATT_ERR_UNLIKELY;
                }
                return 0;
            } else if (cmd == SDF_BLE_COMPANION_AUTH_REGISTER) {
                size_t username_len = buf[1];
                if (username_len == 0 ||
                    username_len >= SDF_STORAGE_WEB_USER_NAME_MAX ||
                    len != 2 + username_len + SDF_STORAGE_WEB_USER_HASH_LEN) {
                    xSemaphoreGive(s_lock);
                    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                }

                memcpy(conn->username, &buf[2], username_len);
                conn->username[username_len] = '\0';
                uint8_t password_hash[SDF_STORAGE_WEB_USER_HASH_LEN];
                memcpy(password_hash, &buf[2 + username_len],
                       SDF_STORAGE_WEB_USER_HASH_LEN);

                conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_PENDING;
                conn->auth_pending = true;
                /* Copy callback pointer before releasing lock */
                void (*on_auth_req)(void *, const char *, const uint8_t *, size_t) =
                    s_callbacks.on_auth_request;
                void *cb_ctx = s_callbacks.ctx;
                char username_copy[SDF_STORAGE_WEB_USER_NAME_MAX];
                strlcpy(username_copy, conn->username, sizeof(username_copy));

                conn->auth_value_len = 1;
                conn->auth_value[0] = SDF_BLE_COMPANION_AUTH_RESULT_PENDING;
                xSemaphoreGive(s_lock);

                if (on_auth_req) {
                    on_auth_req(cb_ctx, username_copy, password_hash,
                                SDF_STORAGE_WEB_USER_HASH_LEN);
                }
                return 0;
            } else if (cmd == SDF_BLE_COMPANION_AUTH_LOGOUT) {
                /* No operands: reject any trailing bytes before touching
                 * connection state, so a padded LOGOUT does not log the
                 * connection out. */
                if (len != 1) {
                    xSemaphoreGive(s_lock);
                    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                }
                conn->auth_state = SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED;
                conn->auth_pending = false;
                memset(conn->username, 0, sizeof(conn->username));
                memset(&conn->pending_login_challenge, 0, sizeof(conn->pending_login_challenge));
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

/* Returns true and sets *out_action if `data` is a well-formed
 * `{"action":"<name>"}` request for one of the admin-fingerprint-gated
 * actions above, false for any other Config write (including malformed
 * JSON), which is left for the normal on_config_write passthrough to
 * reject/ignore as it already does. */
static bool sdf_ble_companion_parse_admin_action_request(
    const uint8_t *data, size_t len, sdf_services_admin_action_t *out_action) {
    cJSON *root = cJSON_ParseWithLength((const char *)data, len);
    if (!root) {
        return false;
    }
    cJSON *action = cJSON_GetObjectItemCaseSensitive(root, SDF_BLE_COMPANION_CONFIG_ACTION_KEY);
    bool matched = false;
    if (cJSON_IsString(action) && action->valuestring != NULL) {
        if (strcmp(action->valuestring, SDF_BLE_COMPANION_CONFIG_ACTION_NUKI_REPAIR) == 0) {
            *out_action = SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR;
            matched = true;
        } else if (strcmp(action->valuestring,
                           SDF_BLE_COMPANION_CONFIG_ACTION_ENROLL_ADMIN) == 0) {
            *out_action = SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN;
            matched = true;
        } else if (strcmp(action->valuestring,
                           SDF_BLE_COMPANION_CONFIG_ACTION_ZB_JOIN) == 0) {
            *out_action = SDF_SERVICES_ADMIN_ACTION_ZB_JOIN;
            matched = true;
        }
    }
    cJSON_Delete(root);
    return matched;
}

/* Post-staging dispatch for a characteristic write. Runs with s_lock
 * released, `data` (len bytes) pointing at the staged payload and `cb` a
 * snapshot of s_callbacks taken under the lock. Returns the ATT status. */
typedef int (*sdf_ble_companion_staged_dispatch_fn)(
    uint16_t conn_handle, const sdf_ble_companion_callbacks_t *cb,
    const uint8_t *data, size_t len);

/* The one place in this component that acquires and releases GATT write
 * staging. Called with s_lock held; always gives it back.
 *
 * The Config, Enrollment and OTA writes are the same sequence - mirror the
 * payload into the connection's read-back buffer, stage a copy that survives
 * the lock release, snapshot the callbacks, unlock, dispatch, unstage - and
 * differ only in the dispatch. Keeping the acquire/release pair inside a
 * single-exit helper is what makes "staging is never leaked" checkable by
 * eye: a missed release is permanent, refusing every later staged write
 * until reboot. */
static int sdf_ble_companion_stage_write(struct os_mbuf *om, size_t len,
                                          uint16_t conn_handle,
                                          uint8_t *mirror, uint16_t *mirror_len,
                                          sdf_ble_companion_staged_dispatch_fn dispatch) {
    uint8_t *staged = sdf_ble_companion_gatt_scratch_acquire();
    if (!staged) {
        /* Already logged and counted as a contract violation by the staging
         * module. Fail just this write, leaving connection state untouched
         * and the dispatch uncalled. */
        xSemaphoreGive(s_lock);
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Mirror while still locked: it is connection state, which a concurrent
     * disconnect memsets. The READ_CHR paths serve it back from there. */
    os_mbuf_copydata(om, 0, len, mirror);
    *mirror_len = (uint16_t)len;
    memcpy(staged, mirror, len);
    sdf_ble_companion_callbacks_t cb = s_callbacks;
    xSemaphoreGive(s_lock);

    int rc = dispatch(conn_handle, &cb, staged, len);
    sdf_ble_companion_gatt_scratch_release();
    return rc;
}

static int sdf_ble_companion_dispatch_config_write(
    uint16_t conn_handle, const sdf_ble_companion_callbacks_t *cb,
    const uint8_t *data, size_t len) {
    sdf_services_admin_action_t requested_action;
    if (sdf_ble_companion_parse_admin_action_request(data, len, &requested_action)) {
        /* NUKI_REPAIR is only reachable once initial Nuki setup is already
         * complete - initial pairing happens via the physical button flow,
         * not this trigger. Rejected synchronously (no pending state
         * entered), matching how an unauthenticated write is already
         * rejected synchronously by the caller. ENROLL_ADMIN/ZB_JOIN have no
         * equivalent precondition: an authenticated companion session
         * already implies an Admin fingerprint exists (it had to authorize
         * this session's own WEB_REG_AUTH), so there's no "not set up yet"
         * state to guard against for those two. */
        if (requested_action == SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR &&
            sdf_services_get_setup_state() != SDF_SERVICES_SETUP_STATE_CLAIMED_COMPLETE) {
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }
        if (cb->on_admin_action_request) {
            cb->on_admin_action_request(cb->ctx, requested_action, conn_handle);
        }
        return 0;
    }

    if (cb->on_config_write) {
        cb->on_config_write(cb->ctx, data, len);
    }
    return 0;
}

static int sdf_ble_companion_dispatch_enroll_write(
    uint16_t conn_handle, const sdf_ble_companion_callbacks_t *cb,
    const uint8_t *data, size_t len) {
    (void)conn_handle;
    if (cb->on_enroll_write) {
        cb->on_enroll_write(cb->ctx, data, len);
    }
    return 0;
}

static int sdf_ble_companion_dispatch_ota_write(
    uint16_t conn_handle, const sdf_ble_companion_callbacks_t *cb,
    const uint8_t *data, size_t len) {
    /* Unlike the other characteristics, a malformed/out-of-protocol OTA
     * write must be rejected at the GATT layer (non-zero ATT error, no
     * notify) rather than silently accepted - the failed write itself is the
     * client's synchronous signal per the BLE OTA chunked-transfer wire
     * format. */
    if (cb->on_ota_write && !cb->on_ota_write(cb->ctx, conn_handle, data, len)) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
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
            return sdf_ble_companion_stage_write(
                om, len, conn_handle, conn->config_value, &conn->config_value_len,
                sdf_ble_companion_dispatch_config_write);
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
            return sdf_ble_companion_stage_write(
                om, len, conn_handle, conn->enroll_value, &conn->enroll_value_len,
                sdf_ble_companion_dispatch_enroll_write);
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
            return sdf_ble_companion_stage_write(
                om, len, conn_handle, conn->ota_value, &conn->ota_value_len,
                sdf_ble_companion_dispatch_ota_write);
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
                         * config_value/enroll_value/ota_value/pending_login_challenge
                         * from that prior session must not be readable by whoever
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
            sdf_ble_companion_restart_advertising();
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
        case BLE_GAP_EVENT_ENC_CHANGE: {
            ESP_LOGI(TAG, "Encryption change: conn_handle=%d, status=%d",
                     event->enc_change.conn_handle, event->enc_change.status);

            if (event->enc_change.status != 0) {
                /* Pairing/encryption attempt failed - nothing to admit. The
                 * pairing window (if open) stays open for another attempt
                 * until its own timeout fires. */
                break;
            }

            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) != 0) {
                break;
            }

            sdf_ble_companion_addr_t identity;
            identity.type = desc.peer_id_addr.type;
            memcpy(identity.val, desc.peer_id_addr.val, sizeof(identity.val));

            /* Note: this admits on *any* successful encryption while the
             * window is open, including an already-trusted device that
             * happens to reconnect during someone else's deliberately
             * opened window - admit_if_window_open() is a no-op for an
             * already allow-listed identity other than closing the window.
             * Per design.md, "the first device to complete bonding during
             * that window is added ... window closes immediately" - this is
             * the literal behavior, not specifically restricted to *new*
             * devices. */
            bool admitted = false;
            if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                admitted = sdf_ble_companion_bond_admit_if_window_open(&s_bond_state,
                                                                        &identity);
                xSemaphoreGive(s_lock);
            } else {
                ESP_LOGW(TAG, "gap_event enc_change: lock contention");
            }

            if (admitted) {
                ESP_LOGI(TAG,
                         "BLE Companion pairing window: device admitted to allow "
                         "list, conn_handle=%d",
                         event->enc_change.conn_handle);
                esp_timer_stop(s_pairing_window_timer);
                sdf_ble_companion_push_allow_list();
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

/* Re-populates the allow list from NimBLE's own NVS-persisted bond store so a
 * reboot doesn't strand every already-paired companion device behind the
 * filtered-advertising default (see sdf_ble_companion_bond_state.h). Only
 * allow-list *membership* is restored - failed-login counters intentionally
 * start at zero every boot, they are never persisted to NVS (see design.md
 * "Decisions").
 *
 * Runs from the host-sync hook rather than sdf_ble_companion_init(), because
 * ble_store_util_bonded_peers() is a bond *store read*: it takes ble_hs_lock()
 * and so needs a host that exists. sdf_ble_companion_init() runs before
 * nimble_port_init() (sdf_app.c:1810 vs 1865), where ble_hs_mutex is still
 * zero-initialized, and NimBLE gives no error return for that case - it
 * dereferences the null handle and panics. Registering the GATT database
 * before host start is fine and still happens in init(); reading host-owned
 * state before host start is not. See fix-ble-bond-seed-init-order.
 *
 * Seeds once per s_bond_state lifetime: a NimBLE resync fires this hook
 * again, and re-seeding there would let a controller reset resurrect a bond
 * that the failed-login eviction path had since removed from the list. */
static void sdf_ble_companion_seed_allow_list(void) {
    if (s_allow_list_seeded) {
        return;
    }
    s_allow_list_seeded = true;

    ble_addr_t bonded[SDF_BLE_COMPANION_BOND_TABLE_MAX];
    int num_peers = 0;
    /* Called without s_lock held: this file's rule is that s_lock is never
     * held across a NimBLE call (cf. sdf_ble_companion_push_allow_list()). */
    int rc = ble_store_util_bonded_peers(bonded, &num_peers,
                                          SDF_BLE_COMPANION_BOND_TABLE_MAX);
    if (rc != 0) {
        /* Unlike before the move, this branch is now genuinely reachable.
         * An empty allow list means no companion can reconnect until the
         * admin-gated pairing window is used - recoverable, unlike the boot
         * loop that reading the store too early produced. */
        ESP_LOGW(TAG, "ble_store_util_bonded_peers failed: %d; allow list left empty", rc);
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "seed_allow_list: lock contention, allow list left empty");
        return;
    }
    for (int i = 0; i < num_peers; i++) {
        sdf_ble_companion_addr_t addr;
        addr.type = bonded[i].type;
        memcpy(addr.val, bonded[i].val, sizeof(addr.val));
        sdf_ble_companion_bond_allow_list_add(&s_bond_state, &addr);
    }
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Seeded BLE Companion allow list with %d bonded peer(s)", num_peers);
}

static void sdf_ble_companion_on_host_sync(void *ctx) {
    (void)ctx;
    ESP_LOGI(TAG, "Shared NimBLE host synced");
    /* This callback runs on the NimBLE host task, which is also the task that
     * runs every GATT access callback - so it is the right owner for GATT
     * write staging. Binding here, before restart_advertising() below starts
     * accepting connections, means no client can be connected (and so no
     * acquire can happen) before ownership exists. A NimBLE resync re-enters
     * this hook on the same task, which rebinds harmlessly. */
    sdf_ble_companion_gatt_scratch_bind_owner();
    /* Must precede restart_advertising(): that is what pushes the list into
     * the controller's Filter Accept List via ble_gap_wl_set() and starts
     * filtered advertising, so seeding after it would leave a window in which
     * an already-bonded companion is refused its first post-reboot
     * reconnect. */
    sdf_ble_companion_seed_allow_list();
    sdf_ble_companion_restart_advertising();
}

/* Pushes the current allow-list snapshot into the NimBLE controller's Filter
 * Accept List. Must be called with advertising stopped (NimBLE rejects
 * ble_gap_wl_set() while an advertise/scan procedure using the list is
 * active) - sdf_ble_companion_restart_advertising() takes care of that by
 * always calling this before (re)starting filtered advertising. */
static void sdf_ble_companion_push_allow_list(void) {
    sdf_ble_companion_addr_t snapshot[SDF_BLE_COMPANION_BOND_TABLE_MAX];
    size_t count = 0;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = sdf_ble_companion_bond_snapshot_allow_list(&s_bond_state, snapshot,
                                                            SDF_BLE_COMPANION_BOND_TABLE_MAX);
        xSemaphoreGive(s_lock);
    } else {
        ESP_LOGW(TAG, "push_allow_list: lock contention, skipping");
        return;
    }

    ble_addr_t wl[SDF_BLE_COMPANION_BOND_TABLE_MAX];
    for (size_t i = 0; i < count; i++) {
        wl[i].type = snapshot[i].type;
        memcpy(wl[i].val, snapshot[i].val, sizeof(wl[i].val));
    }

    int rc = ble_gap_wl_set(wl, (uint8_t)count);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_wl_set failed: %d (count=%u)", rc, (unsigned)count);
    }
}

/* Default-mode advertising: sparse interval, restricted by the Filter
 * Accept List (BLE_HCI_ADV_FILT_CONN) so only already-allow-listed devices
 * can complete a connection. Scan requests are left unfiltered so the
 * companion app can still see the device advertise by name before it's been
 * through the pairing window - it just can't connect until admitted. See
 * design.md "Sparse, Allow-List-Filtered Default Advertising". */
void sdf_ble_companion_start_advertising_sparse(void) {
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = SDF_BLE_COMPANION_ADV_SPARSE_INTERVAL_MIN,
        .itvl_max = SDF_BLE_COMPANION_ADV_SPARSE_INTERVAL_MAX,
        .filter_policy = BLE_HCI_ADV_FILT_CONN,
    };

    int rc = ble_gap_adv_set_data(s_adv_data, s_adv_data_len);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, sdf_ble_companion_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start sparse advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "Sparse, allow-list-filtered advertising started");
    }
}

/* Pairing-window-mode advertising: fast interval, unfiltered
 * (BLE_HCI_ADV_FILT_NONE) so a brand-new device can find and connect to it.
 * Only used while sdf_ble_companion_bond_state_t's window is open - see
 * sdf_ble_companion_open_pairing_window(). */
void sdf_ble_companion_start_advertising_pairing(void) {
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN,
        .itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX,
        .filter_policy = BLE_HCI_ADV_FILT_NONE,
    };

    int rc = ble_gap_adv_set_data(s_adv_data, s_adv_data_len);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, sdf_ble_companion_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start pairing-window advertising: %d", rc);
    } else {
        ESP_LOGI(TAG, "Unfiltered pairing-window advertising started");
    }
}

/* Single choke point for (re)starting advertising in whichever mode
 * currently applies - called after every disconnect, at boot (host sync),
 * when the pairing window opens, and when it closes (admission or
 * timeout). Always stops any advertising procedure already in progress
 * first (ble_gap_adv_start() fails with BLE_HS_EALREADY otherwise); that
 * call is a harmless, silently-ignored no-op (BLE_HS_EALREADY) when nothing
 * was advertising, e.g. right after a peer just connected. */
static void sdf_ble_companion_restart_advertising(void) {
    int stop_rc = ble_gap_adv_stop();
    if (stop_rc != 0 && stop_rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "ble_gap_adv_stop failed: %d", stop_rc);
    }

    bool window_open = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        window_open = sdf_ble_companion_bond_window_is_open(&s_bond_state);
        xSemaphoreGive(s_lock);
    } else {
        ESP_LOGW(TAG, "restart_advertising: lock contention, defaulting to sparse");
    }

    if (window_open) {
        sdf_ble_companion_start_advertising_pairing();
    } else {
        sdf_ble_companion_push_allow_list();
        sdf_ble_companion_start_advertising_sparse();
    }
}

esp_err_t sdf_ble_companion_open_pairing_window(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (sdf_ble_companion_bond_window_is_open(&s_bond_state)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    sdf_ble_companion_bond_open_window(&s_bond_state);
    xSemaphoreGive(s_lock);

    esp_timer_stop(s_pairing_window_timer); /* defensive; should already be idle */
    esp_timer_start_once(s_pairing_window_timer,
                          (uint64_t)SDF_BLE_COMPANION_PAIRING_WINDOW_MS * 1000);

    sdf_ble_companion_restart_advertising();
    ESP_LOGI(TAG, "BLE Companion pairing window opened (%u ms)",
             (unsigned)SDF_BLE_COMPANION_PAIRING_WINDOW_MS);
    return ESP_OK;
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

    /* No NimBLE host call may appear between here and the
     * sdf_nuki_ble_register_server_service() below: this function runs before
     * nimble_port_init(). The allow list is seeded from the bond store later,
     * once the host is up - see sdf_ble_companion_seed_allow_list(). */
    sdf_ble_companion_bond_state_init(&s_bond_state);
    s_allow_list_seeded = false;

    // Initialize the pairing-window timeout timer
    esp_timer_create_args_t timer_args = {
        .callback = sdf_ble_companion_pairing_window_timer_cb,
        .arg = NULL,
        .name = "sdf_ble_pair_timer",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_pairing_window_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create pairing window timer: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }

    if (sdf_nuki_ble_register_server_service(sdf_ble_companion_register_gatt,
                                             sdf_ble_companion_on_host_sync,
                                             NULL) != 0) {
        esp_timer_delete(s_pairing_window_timer);
        s_pairing_window_timer = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    // Subscribe to enrollment events (permanent for the lifetime of the boot)
    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_COMPLETE,
                                     SDF_EVENT_ROUTER_PRIO_NORMAL,
                                     sdf_ble_companion_enrollment_complete_handler,
                                     NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to subscribe to enrollment complete: %s", esp_err_to_name(err));
    }

    err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_ENROLLMENT_FAILED,
                                     SDF_EVENT_ROUTER_PRIO_NORMAL,
                                     sdf_ble_companion_enrollment_failed_handler,
                                     NULL);
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

    /* Subscriptions registered with the event router are permanent for the
     * lifetime of the boot and cannot be unregistered. If this uncalled
     * deinit path is ever revived, callbacks will continue to be invoked by
     * the event router but will exit safely because s_initialized is false. */

    // Stop and delete the pairing-window timeout timer
    if (s_pairing_window_timer) {
        esp_timer_stop(s_pairing_window_timer);
        esp_timer_delete(s_pairing_window_timer);
        s_pairing_window_timer = NULL;
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

esp_err_t sdf_ble_companion_reply_admin_action(uint16_t conn_handle,
                                                sdf_services_admin_action_t action,
                                                bool authorized) {
    const char *key;
    switch (action) {
    case SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR:
        key = SDF_BLE_COMPANION_CONFIG_ACTION_NUKI_REPAIR;
        break;
    case SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN:
        key = SDF_BLE_COMPANION_CONFIG_ACTION_ENROLL_ADMIN;
        break;
    case SDF_SERVICES_ADMIN_ACTION_ZB_JOIN:
        key = SDF_BLE_COMPANION_CONFIG_ACTION_ZB_JOIN;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    char payload[32];
    int n = snprintf(payload, sizeof(payload), "{\"%s\":%s}", key,
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

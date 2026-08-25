#ifndef SDF_BLE_COMPANION_H
#define SDF_BLE_COMPANION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sdf_services.h"
#include "sdf_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDF_BLE_COMPANION_MAX_CONNECTIONS 3

typedef enum {
    SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED = 0,
    SDF_BLE_COMPANION_AUTH_STATE_PENDING = 1, /* REGISTER: admin-fingerprint pending */
    SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED = 2,
    /* LOGIN_INIT issued a challenge for this connection; conn->pending_login_challenge
     * is valid until the client's LOGIN_VERIFY consumes it (success or failure) or
     * the connection is dropped. Deliberately distinct from AUTH_STATE_PENDING
     * above, which is REGISTER's unrelated admin-fingerprint wait. */
    SDF_BLE_COMPANION_AUTH_STATE_LOGIN_CHALLENGE_ISSUED = 3,
} sdf_ble_companion_auth_state_t;

typedef struct {
    uint16_t conn_handle;
    bool connected;
    sdf_ble_companion_auth_state_t auth_state;
    bool auth_pending;
    char username[SDF_STORAGE_WEB_USER_NAME_MAX];
    /* Fingerprint user id of the account this connection authenticated
     * against (companion-identity). 0 = unbound. Set on a successful
     * LOGIN_VERIFY / REGISTER confirmation; cleared on LOGOUT and by the
     * disconnect memset. Admin authority is resolved LIVE from this id's
     * current enrolment + permission on every restricted access - never
     * from anything stored on the account. */
    uint16_t bound_user_id;
    /* Outstanding LOGIN challenge for this connection - single-connection,
     * single-attempt scoped (never persisted), valid only while
     * auth_state == SDF_BLE_COMPANION_AUTH_STATE_LOGIN_CHALLENGE_ISSUED.
     * Cleared by the disconnect memset and explicitly after each
     * LOGIN_VERIFY attempt. */
    sdf_services_web_auth_challenge_t pending_login_challenge;
    uint8_t auth_value[512];
    uint16_t auth_value_len;
    uint8_t config_value[512];
    uint16_t config_value_len;
    uint8_t enroll_value[512];
    uint16_t enroll_value_len;
    /* Set while this connection has a user-management request in flight
     * (companion-user-mgmt): a second request is answered busy rather than
     * queued or dropped. */
    bool um_in_flight;
    uint8_t ota_value[512];
    uint16_t ota_value_len;
} sdf_ble_companion_connection_t;

typedef void (*sdf_ble_companion_auth_request_cb)(void *ctx,
                                                   const char *username,
                                                   const uint8_t *password_hash,
                                                   size_t hash_len);

typedef void (*sdf_ble_companion_config_write_cb)(void *ctx,
                                                   const uint8_t *data,
                                                   size_t len);

/* User-management verbs carried by the Enrollment characteristic's
 * request/reply protocol (companion-user-mgmt). */
typedef enum {
    SDF_BLE_COMPANION_UM_VERB_LIST = 0,
    SDF_BLE_COMPANION_UM_VERB_ENROLL = 1,
    SDF_BLE_COMPANION_UM_VERB_DELETE = 2,
    SDF_BLE_COMPANION_UM_VERB_SET_PERMISSION = 3,
    SDF_BLE_COMPANION_UM_VERB_RENAME = 4,
} sdf_ble_companion_um_verb_t;

#define SDF_BLE_COMPANION_UM_NAME_MAX SDF_STORAGE_WEB_USER_NAME_MAX

/* A parsed user-management request. req_id is client-supplied and echoed on
 * every reply, including a rejection of the rest of the payload (where
 * req_id_valid is false but req_id may still carry whatever was parsed so
 * the invalid-request reply can correlate; 0 otherwise). */
typedef struct {
    uint32_t req_id;
    bool req_id_valid;
    sdf_ble_companion_um_verb_t verb;
    uint16_t user_id;
    uint8_t permission;
    char name[SDF_BLE_COMPANION_UM_NAME_MAX];
} sdf_ble_companion_um_request_t;

/* Parses a user-management request. Returns false for a malformed payload,
 * an unknown verb, or an out-of-range field - the caller answers those with
 * an invalid-request reply rather than dropping them (req_id captures any
 * request id seen before the failure). */
bool sdf_ble_companion_um_parse_request(const uint8_t *data, size_t len,
                                        sdf_ble_companion_um_request_t *req);

/* Formats the terminal reply {"req":N,"result":"<outcome>"}. `result` is a
 * sdf_services_um_outcome_name() string. Returns the length written, or -1
 * if it did not fit. */
int sdf_ble_companion_um_format_reply(uint32_t req_id, const char *result,
                                      char *buf, size_t cap);

/* Formats one chunk of a list reply:
 * {"req":N,"verb":"list","part":i,"end":true|false,"users":[...]}.
 * The final part carries end=true, so a truncated list is never
 * indistinguishable from a complete one. */
int sdf_ble_companion_um_format_list_part(uint32_t req_id, int part, bool end,
                                          const char *users_json, char *buf,
                                          size_t cap);

/* Admission decision for a write to the Enrollment characteristic: live
 * admin authority admits every verb; a setup-phase connection to a device
 * with no enrolled users admits ONLY enrolment (no account and no admin
 * exists yet to scan). The other arguments come from the caller so this
 * stays a pure, host-testable function. */
bool sdf_ble_companion_um_admits(const sdf_ble_companion_um_request_t *req,
                                 bool conn_has_admin_authority,
                                 bool setup_phase_armed,
                                 bool no_users_enrolled);

/**
 * Fired when an already-authenticated GATT client requests one of the
 * admin-fingerprint-gated actions reachable over BLE - Nuki re-pair,
 * Enroll-Admin, or Zigbee Join (a `{"action":"nuki_repair"|"enroll_admin"|
 * "zb_join"}` write on the Config characteristic; nuki_repair is only
 * accepted once setup is already complete - see
 * sdf_ble_companion_config_access()). Parallel to on_auth_request: the
 * originating connection handle is passed through so the result can later
 * be routed back to it via sdf_ble_companion_reply_admin_action().
 */
typedef void (*sdf_ble_companion_admin_action_request_cb)(void *ctx,
                                                            sdf_services_admin_action_t action,
                                                            uint16_t conn_handle);

/**
 * Returns true if the write was well-formed and accepted per the BLE OTA
 * chunked-transfer wire format (regardless of whether the underlying OTA
 * operation ultimately succeeds - such failures are reported asynchronously
 * via sdf_ble_companion_notify_ota()), or false if the write was malformed
 * or out-of-protocol and must be rejected by the GATT layer (non-zero ATT
 * error, no notify) without opening, resuming, or corrupting any session.
 */
typedef bool (*sdf_ble_companion_ota_write_cb)(void *ctx,
                                                uint16_t conn_handle,
                                                const uint8_t *data,
                                                size_t len);

/**
 * Fired when an authenticated companion session writes the explicit
 * setup-completion request (`{"action":"finish_setup"}` on the Config
 * characteristic - Config writes are authenticated-only, which is exactly
 * the gate the wizard's final step needs). The device-side completion
 * sequence (persist admission record BEFORE latch, populate/push allow
 * list, switch advertising) runs in sdf_app's handler; its outcome is
 * routed back via sdf_ble_companion_reply_setup_complete().
 */
typedef void (*sdf_ble_companion_setup_complete_cb)(void *ctx,
                                                     uint16_t conn_handle);

/**
 * Fired when an authenticated companion session writes the setup-phase Nuki
 * pairing request (`{"action":"setup_nuki_pair"}` on Config). Reachable only
 * while the setup-completion latch is unset; sdf_app's handler starts
 * initial Nuki pairing and replies via a Config notify.
 */
typedef void (*sdf_ble_companion_setup_nuki_pair_cb)(void *ctx,
                                                      uint16_t conn_handle);

/**
 * Fired when a write to the Enrollment characteristic parses as a
 * user-management request AND the connection is admitted (live admin
 * authority, or the setup-phase enrolment case). Runs on the NimBLE host
 * task: the implementer must hand the request to a task that may wait and
 * return without blocking - no user-management verb, and no wait for its
 * result, may happen here (companion-user-mgmt). The terminal reply is
 * notified later via sdf_ble_companion_reply_um().
 */
typedef void (*sdf_ble_companion_um_request_cb)(void *ctx,
                                                uint16_t conn_handle,
                                                const sdf_ble_companion_um_request_t *request);


typedef struct {
    void *ctx;
    sdf_ble_companion_auth_request_cb on_auth_request;
    sdf_ble_companion_config_write_cb on_config_write;
    sdf_ble_companion_ota_write_cb on_ota_write;
    sdf_ble_companion_admin_action_request_cb on_admin_action_request;
    sdf_ble_companion_setup_complete_cb on_setup_complete;
    sdf_ble_companion_setup_nuki_pair_cb on_setup_nuki_pair;
    sdf_ble_companion_um_request_cb on_um_request;
} sdf_ble_companion_callbacks_t;

esp_err_t sdf_ble_companion_init(const sdf_ble_companion_callbacks_t *callbacks);
esp_err_t sdf_ble_companion_deinit(void);

bool sdf_ble_companion_is_authenticated(uint16_t conn_handle);
esp_err_t sdf_ble_companion_set_authenticated(uint16_t conn_handle, bool authenticated);
esp_err_t sdf_ble_companion_reply_auth(const char *username, bool authorized);

/**
 * Routes the result of a pending BLE-triggered admin action request (Nuki
 * re-pair, Enroll-Admin, or Zigbee Join) back to the originating connection
 * (identified by conn_handle, unlike sdf_ble_companion_reply_auth() which is
 * keyed by username) as a `{"nuki_repair"|"enroll_admin"|"zb_join":
 * true|false}` notification on the Config characteristic. Returns
 * ESP_ERR_INVALID_ARG if `action` isn't one of those three, or
 * ESP_ERR_INVALID_STATE if the connection is no longer connected or
 * authenticated - the caller has no further recovery action to take in
 * either case, the client that would have received the reply is simply gone
 * (or was never a valid target).
 */
esp_err_t sdf_ble_companion_reply_admin_action(uint16_t conn_handle,
                                                sdf_services_admin_action_t action,
                                                bool authorized);

esp_err_t sdf_ble_companion_notify_config(uint16_t conn_handle, const uint8_t *data, size_t len);
esp_err_t sdf_ble_companion_notify_enroll(uint16_t conn_handle, const uint8_t *data, size_t len);
esp_err_t sdf_ble_companion_notify_ota(uint16_t conn_handle, const uint8_t *data, size_t len);

/**
 * Signals that a reported device-health value changed (companion-device-
 * health). Coalesces bursts: changes arriving within a short window produce
 * one Status notification carrying the latest report, pushed to every
 * subscribed authenticated connection. A report too large for one
 * notification at the negotiated MTU is delivered as an empty change marker;
 * the client obtains the full value with a read.
 */
esp_err_t sdf_ble_companion_notify_status_change(void);

/**
 * Notifies a user-management reply or list part on the Enrollment
 * characteristic. Unlike notify_enroll(), this admits any connected
 * connection: the setup-phase connection that may send user-management
 * requests is deliberately unauthenticated (no account exists yet), and its
 * requests must still be answered.
 */
esp_err_t sdf_ble_companion_notify_um(uint16_t conn_handle, const uint8_t *data, size_t len);

/**
 * Formats and notifies a terminal reply {"req":N,"result":"<outcome>"} for
 * `req_id` on the Enrollment characteristic. Also clears the connection's
 * in-flight marker set when the request was accepted (see
 * sdf_ble_companion_um_set_in_flight()).
 */
esp_err_t sdf_ble_companion_reply_um(uint16_t conn_handle, uint32_t req_id,
                                     const char *result);

/**
 * Marks/clears a connection's single in-flight user-management request. A
 * second request from the same connection while the marker is set is
 * answered busy rather than queued or dropped.
 */
void sdf_ble_companion_um_set_in_flight(uint16_t conn_handle, bool in_flight);

/**
 * Request id attributed to enrolment progress notifications (task 4.3):
 * set by the app layer when it starts an enrolment on behalf of a
 * user-management request; the Enrollment characteristic's progress,
 * complete and failed notifications carry `"req":<id>` while this is
 * non-zero. The complete/failed handlers clear it after broadcasting.
 */
void sdf_ble_companion_um_set_active_enroll_request(uint32_t req_id);

/**
 * Broadcast data to all authenticated connections.
 * Iterates over all connections and sends notifications to authenticated ones.
 */
esp_err_t sdf_ble_companion_broadcast_ota(const uint8_t *data, size_t len);

/**
 * Handle a write to the OTA characteristic implementing the chunked binary
 * firmware transfer protocol over the existing authenticated BLE GATT
 * connection: opcode-prefixed control/data messages, `[opcode:1][payload...]`
 *   - 0x01 BEGIN: 4-byte little-endian uint32 declared image size (5 bytes
 *     total). Opens a new OTA session, or - if a session with a matching
 *     declared size is already open - resumes it by reporting the current
 *     byte offset.
 *   - 0x02 CHUNK: raw firmware bytes (1..max_chunk_len, where
 *     max_chunk_len = ble_att_mtu(conn_handle) - 4).
 *   - 0x03 END: no payload (1 byte total). Triggers integrity/signature
 *     verification and commit.
 * Per-chunk and completion status are reported back via
 * sdf_ble_companion_notify_ota(). Intended to be wired up as the
 * `on_ota_write` callback in sdf_ble_companion_callbacks_t.
 */
bool sdf_ble_companion_handle_ota_write(void *ctx, uint16_t conn_handle,
                                         const uint8_t *data, size_t len);

/**
 * Opens the Admin-Fingerprint-Gated Device Pairing Window: switches
 * advertising to unfiltered for SDF_BLE_COMPANION_PAIRING_WINDOW_MS (see
 * sdf_ble_companion_bond_state.h). The first device to complete bonding
 * during that window is added to the allow list and the window closes
 * immediately; otherwise it closes on timeout with nothing added. Intended
 * to be called only after the "request BLE Companion pairing window" admin
 * action (SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW) has been authorized
 * by an Admin fingerprint - see sdf_app_on_admin_action(). Returns
 * ESP_ERR_INVALID_STATE if a window is already open or the service isn't
 * initialized.
 */
esp_err_t sdf_ble_companion_open_pairing_window(void);

/**
 * Routes the outcome of the explicit setup-completion request back to the
 * requesting connection as a Config-characteristic notification:
 *   completed:  {"finish_setup":true}
 *   rejected:   {"finish_setup":false,"step":"<outstanding_step>"}
 * where <outstanding_step> is one of "admin_enrollment", "registration" or
 * "nuki_pairing" - the first wizard step that is still outstanding.
 */
esp_err_t sdf_ble_companion_reply_setup_complete(uint16_t conn_handle,
                                                  bool completed,
                                                  const char *outstanding_step);

/**
 * Copies the peer's resolved identity address for an active connection into
 * (addr_type, addr6). Used by sdf_app's setup-completion sequence to persist
 * the admission record for exactly the requesting client. Returns false if
 * the connection is gone.
 */
bool sdf_ble_companion_get_conn_identity(uint16_t conn_handle,
                                          uint8_t *addr_type, uint8_t addr6[6]);

/**
 * Completion-path tail: adds the just-admitted identity to the runtime
 * allow list, pushes the list into the controller's Filter Accept List, and
 * re-evaluates advertising mode. With the setup-completion latch already
 * persisted by the caller (admission record first, then latch - the
 * crash-safe order), this switches to sparse allow-list-filtered
 * advertising and restores the ordinary connection limit.
 */
esp_err_t sdf_ble_companion_admit_and_switch_to_filtered(uint8_t addr_type,
                                                          const uint8_t addr[6]);

#ifdef __cplusplus
}
#endif

#endif /* SDF_BLE_COMPANION_H */

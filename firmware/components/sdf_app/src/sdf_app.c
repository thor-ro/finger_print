#include "sdf_app.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "sdf_nuki_ble_transport.h"
#include "sdf_nuki_pairing.h"
#include "sdf_protocol_ble.h"
#include "sdf_storage.h"

#define SDF_APP_ID 1u
#define SDF_APP_NAME "SDF"
#define SDF_APP_LOCK_SUFFIX "SDF"
#define SDF_APP_LOCK_ACTION_MAX_RETRIES 2u
// Set this to your lock's BLE address to avoid accidental pairing.
// Example for lock address AA:BB:CC:DD:EE:FF (LSB first for NimBLE):
// {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}
#define SDF_NUKI_TARGET_ADDR_TYPE BLE_ADDR_RANDOM
static const ble_addr_t SDF_NUKI_TARGET_ADDR = {
    .type = SDF_NUKI_TARGET_ADDR_TYPE,
    .val = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}
};

static const char *TAG = "sdf_app";

static sdf_nuki_ble_transport_t s_ble;
static sdf_nuki_client_t s_client;
static sdf_nuki_pairing_t s_pairing;
static sdf_app_event_cb s_event_cb;
static void *s_event_ctx;
static bool s_has_creds;
static bool s_pairing_active;

typedef enum {
    SDF_APP_LOCK_FLOW_IDLE = 0,
    SDF_APP_LOCK_FLOW_WAIT_CHALLENGE,
    SDF_APP_LOCK_FLOW_WAIT_COMPLETION
} sdf_app_lock_flow_state_t;

typedef struct {
    sdf_app_lock_flow_state_t state;
    uint8_t requested_action;
    uint8_t flags;
    uint8_t retry_count;
    uint8_t nonce_nk[SDF_NUKI_CHALLENGE_NONCE_LEN];
} sdf_app_lock_flow_t;

static sdf_app_lock_flow_t s_lock_flow;

static const char *sdf_app_status_name(uint8_t status)
{
    switch (status) {
    case SDF_NUKI_STATUS_ACCEPTED:
        return "ACCEPTED";
    case SDF_NUKI_STATUS_COMPLETE:
        return "COMPLETE";
    default:
        return "UNKNOWN";
    }
}

static const char *sdf_app_error_name(int8_t code)
{
    switch ((uint8_t)code) {
    case 0x45:
        return "K_ERROR_BUSY";
    case 0x46:
        return "K_ERROR_CANCELED";
    case 0x47:
        return "K_ERROR_NOT_CALIBRATED";
    case 0x49:
        return "K_ERROR_MOTOR_LOW_VOLTAGE";
    case 0x4A:
        return "K_ERROR_MOTOR_POWER_FAILURE";
    case 0x4B:
        return "K_ERROR_CLUTCH_POWER_FAILURE";
    default:
        return "UNKNOWN_ERROR";
    }
}

static bool sdf_app_valid_lock_action(uint8_t lock_action)
{
    switch (lock_action) {
    case SDF_NUKI_LOCK_ACTION_UNLOCK:
    case SDF_NUKI_LOCK_ACTION_LOCK:
    case SDF_NUKI_LOCK_ACTION_UNLATCH:
    case SDF_NUKI_LOCK_ACTION_LOCK_N_GO:
    case SDF_NUKI_LOCK_ACTION_LOCK_N_GO_UNLATCH:
    case SDF_NUKI_LOCK_ACTION_FULL_LOCK:
        return true;
    default:
        return false;
    }
}

static void sdf_app_emit_event(const sdf_app_event_t *event)
{
    if (s_event_cb != NULL && event != NULL) {
        s_event_cb(s_event_ctx, event);
    }
}

static void sdf_app_emit_lock_progress(void)
{
    sdf_app_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = SDF_APP_EVENT_LOCK_ACTION_PROGRESS;
    event.lock_action_in_progress = s_lock_flow.state != SDF_APP_LOCK_FLOW_IDLE;
    event.lock_action = s_lock_flow.requested_action;
    event.retry_count = s_lock_flow.retry_count;
    sdf_app_emit_event(&event);
}

static int sdf_app_request_challenge_locked(void)
{
    int res = sdf_nuki_client_send_request_data(
        &s_client,
        SDF_NUKI_CMD_CHALLENGE,
        NULL,
        0);
    if (res == SDF_NUKI_RESULT_OK) {
        s_lock_flow.state = SDF_APP_LOCK_FLOW_WAIT_CHALLENGE;
    }
    return res;
}

static void sdf_app_lock_flow_reset(void)
{
    memset(&s_lock_flow, 0, sizeof(s_lock_flow));
    s_lock_flow.state = SDF_APP_LOCK_FLOW_IDLE;
}

static int sdf_app_retry_lock_action(const char *reason)
{
    if (s_lock_flow.state == SDF_APP_LOCK_FLOW_IDLE) {
        return SDF_NUKI_RESULT_OK;
    }

    if (s_lock_flow.retry_count >= SDF_APP_LOCK_ACTION_MAX_RETRIES) {
        ESP_LOGE(
            TAG,
            "Lock action 0x%02X failed after %u retries (%s)",
            s_lock_flow.requested_action,
            (unsigned)s_lock_flow.retry_count,
            reason);
        sdf_app_lock_flow_reset();
        sdf_app_emit_lock_progress();
        return SDF_NUKI_RESULT_ERR_AUTH;
    }

    s_lock_flow.retry_count++;
    ESP_LOGW(
        TAG,
        "Retrying lock action 0x%02X (%u/%u): %s",
        s_lock_flow.requested_action,
        (unsigned)s_lock_flow.retry_count,
        (unsigned)SDF_APP_LOCK_ACTION_MAX_RETRIES,
        reason);

    int res = sdf_app_request_challenge_locked();
    if (res != SDF_NUKI_RESULT_OK) {
        ESP_LOGE(TAG, "Challenge retry failed: %d", res);
        sdf_app_lock_flow_reset();
    }
    sdf_app_emit_lock_progress();
    return res;
}

static int sdf_app_send_encrypted(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    return sdf_nuki_ble_send(&s_ble, SDF_NUKI_BLE_CHANNEL_USDIO, data, len);
}

static int sdf_app_send_unencrypted(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    return sdf_nuki_ble_send(&s_ble, SDF_NUKI_BLE_CHANNEL_GDIO, data, len);
}

int sdf_app_request_keyturner_state(void)
{
    if (!s_has_creds || s_pairing_active || !sdf_nuki_ble_is_ready(&s_ble)) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    return sdf_nuki_client_send_request_data(
        &s_client,
        SDF_NUKI_CMD_KEYTURNER_STATES,
        NULL,
        0);
}

int sdf_app_lock_action(uint8_t lock_action, uint8_t flags)
{
    if (!s_has_creds || s_pairing_active || !sdf_nuki_ble_is_ready(&s_ble)) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    if (!sdf_app_valid_lock_action(lock_action)) {
        return SDF_NUKI_RESULT_ERR_ARG;
    }

    if (s_lock_flow.state != SDF_APP_LOCK_FLOW_IDLE) {
        return SDF_NUKI_RESULT_ERR_INCOMPLETE;
    }

    memset(&s_lock_flow, 0, sizeof(s_lock_flow));
    s_lock_flow.state = SDF_APP_LOCK_FLOW_IDLE;
    s_lock_flow.requested_action = lock_action;
    s_lock_flow.flags = flags;

    int res = sdf_app_request_challenge_locked();
    if (res != SDF_NUKI_RESULT_OK) {
        sdf_app_lock_flow_reset();
        return res;
    }

    sdf_app_emit_lock_progress();
    ESP_LOGI(TAG, "Started lock action flow for action=0x%02X", lock_action);
    return SDF_NUKI_RESULT_OK;
}

void sdf_app_set_event_callback(sdf_app_event_cb cb, void *ctx)
{
    s_event_cb = cb;
    s_event_ctx = ctx;
}

static void sdf_app_on_message(void *ctx, const sdf_nuki_message_t *msg)
{
    (void)ctx;

    if (msg == NULL) {
        return;
    }

    if (msg->command_id == SDF_NUKI_CMD_CHALLENGE) {
        if (s_lock_flow.state != SDF_APP_LOCK_FLOW_WAIT_CHALLENGE) {
            return;
        }

        int res = sdf_nuki_parse_challenge(msg, s_lock_flow.nonce_nk);
        if (res != SDF_NUKI_RESULT_OK) {
            sdf_app_retry_lock_action("challenge parse failed");
            return;
        }

        res = sdf_nuki_client_send_lock_action(
            &s_client,
            s_lock_flow.requested_action,
            s_lock_flow.flags,
            (const uint8_t *)SDF_APP_LOCK_SUFFIX,
            strlen(SDF_APP_LOCK_SUFFIX),
            s_lock_flow.nonce_nk);
        if (res != SDF_NUKI_RESULT_OK) {
            sdf_app_retry_lock_action("lock action write failed");
            return;
        }

        s_lock_flow.state = SDF_APP_LOCK_FLOW_WAIT_COMPLETION;
        sdf_app_emit_lock_progress();
        return;
    }

    if (msg->command_id == SDF_NUKI_CMD_STATUS) {
        uint8_t status = 0;
        if (sdf_nuki_parse_status(msg, &status) != SDF_NUKI_RESULT_OK) {
            return;
        }

        ESP_LOGI(TAG, "Status 0x%02X (%s)", status, sdf_app_status_name(status));

        sdf_app_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = SDF_APP_EVENT_STATUS;
        event.data.status = status;
        event.lock_action_in_progress = s_lock_flow.state != SDF_APP_LOCK_FLOW_IDLE;
        event.lock_action = s_lock_flow.requested_action;
        event.retry_count = s_lock_flow.retry_count;
        sdf_app_emit_event(&event);

        if (s_lock_flow.state == SDF_APP_LOCK_FLOW_WAIT_COMPLETION) {
            if (status == SDF_NUKI_STATUS_ACCEPTED) {
                return;
            }

            if (status == SDF_NUKI_STATUS_COMPLETE) {
                uint8_t action = s_lock_flow.requested_action;
                sdf_app_lock_flow_reset();
                sdf_app_emit_lock_progress();
                ESP_LOGI(TAG, "Lock action 0x%02X completed", action);
                sdf_app_request_keyturner_state();
                return;
            }

            sdf_app_retry_lock_action("unexpected status");
        }
        return;
    }

    if (msg->command_id == SDF_NUKI_CMD_KEYTURNER_STATES) {
        sdf_nuki_keyturner_state_t state;
        if (sdf_nuki_parse_keyturner_states(msg, &state) != SDF_NUKI_RESULT_OK) {
            return;
        }

        ESP_LOGI(
            TAG,
            "Keyturner state: nuki=%u lock=%u trigger=%u battery_critical=%u",
            (unsigned)state.nuki_state,
            (unsigned)state.lock_state,
            (unsigned)state.trigger,
            (unsigned)state.critical_battery_state);

        sdf_app_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = SDF_APP_EVENT_KEYTURNER_STATE;
        event.data.keyturner_state = state;
        event.lock_action_in_progress = s_lock_flow.state != SDF_APP_LOCK_FLOW_IDLE;
        event.lock_action = s_lock_flow.requested_action;
        event.retry_count = s_lock_flow.retry_count;
        sdf_app_emit_event(&event);
        return;
    }

    if (msg->command_id == SDF_NUKI_CMD_ERROR_REPORT) {
        sdf_nuki_error_report_t report;
        if (sdf_nuki_parse_error_report(msg, &report) != SDF_NUKI_RESULT_OK) {
            return;
        }

        ESP_LOGW(
            TAG,
            "Error report code=0x%02X (%s) cmd=0x%04X",
            (unsigned)((uint8_t)report.error_code),
            sdf_app_error_name(report.error_code),
            report.command_identifier);

        sdf_app_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = SDF_APP_EVENT_ERROR;
        event.data.error_report = report;
        event.lock_action_in_progress = s_lock_flow.state != SDF_APP_LOCK_FLOW_IDLE;
        event.lock_action = s_lock_flow.requested_action;
        event.retry_count = s_lock_flow.retry_count;
        sdf_app_emit_event(&event);

        if (s_lock_flow.state != SDF_APP_LOCK_FLOW_IDLE) {
            sdf_app_retry_lock_action("received error report");
        }
        return;
    }

    ESP_LOGI(TAG, "Nuki message cmd=0x%04X len=%u", msg->command_id, (unsigned)msg->payload_len);
}

static void sdf_app_on_ble_ready(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "BLE transport ready");

    if (s_lock_flow.state != SDF_APP_LOCK_FLOW_IDLE) {
        ESP_LOGW(TAG, "Resetting stale lock action flow after reconnect");
        sdf_app_lock_flow_reset();
        sdf_app_emit_lock_progress();
    }

    if (!s_has_creds) {
        int res = sdf_nuki_pairing_init(&s_pairing, &s_client, 0, SDF_APP_ID, SDF_APP_NAME);
        if (res != SDF_NUKI_RESULT_OK) {
            ESP_LOGE(TAG, "Pairing init failed: %d", res);
            return;
        }
        s_pairing_active = true;

        res = sdf_nuki_pairing_start(&s_pairing);
        if (res != SDF_NUKI_RESULT_OK) {
            ESP_LOGE(TAG, "Pairing start failed: %d", res);
        }
        return;
    }

    int res = sdf_app_request_keyturner_state();
    if (res != SDF_NUKI_RESULT_OK) {
        ESP_LOGW(TAG, "Initial keyturner state request failed: %d", res);
    }
}

static void sdf_app_on_ble_rx(
    void *ctx,
    sdf_nuki_ble_channel_t channel,
    const uint8_t *data,
    size_t len)
{
    (void)ctx;

    if (channel == SDF_NUKI_BLE_CHANNEL_GDIO) {
        if (s_pairing_active) {
            sdf_nuki_pairing_handle_unencrypted(&s_pairing, data, len);
        }
        return;
    }

    if (s_pairing_active) {
        sdf_nuki_pairing_handle_encrypted(&s_pairing, data, len);
        if (s_pairing.state == SDF_NUKI_PAIRING_COMPLETE) {
            sdf_nuki_credentials_t creds;
            if (sdf_nuki_pairing_get_credentials(&s_pairing, &creds) == SDF_NUKI_RESULT_OK) {
                s_client.creds = creds;
                s_has_creds = true;
                s_pairing_active = false;
                esp_err_t err = sdf_storage_nuki_save(creds.authorization_id, creds.shared_key);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to save credentials: %s", esp_err_to_name(err));
                }
                ESP_LOGI(TAG, "Pairing complete; credentials stored");
                int res = sdf_app_request_keyturner_state();
                if (res != SDF_NUKI_RESULT_OK) {
                    ESP_LOGW(TAG, "Post-pair keyturner state request failed: %d", res);
                }
            }
        }
        return;
    }

    sdf_nuki_client_feed_encrypted(&s_client, data, len);
}

void sdf_app_init(void)
{
    sdf_app_lock_flow_reset();

    esp_err_t err = sdf_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Storage init failed: %s", esp_err_to_name(err));
    }

    sdf_nuki_credentials_t creds = {0};
    uint32_t auth_id = 0;
    uint8_t shared_key[32] = {0};

    if (sdf_storage_nuki_load(&auth_id, shared_key) == ESP_OK) {
        creds.authorization_id = auth_id;
        memcpy(creds.shared_key, shared_key, sizeof(shared_key));
        creds.app_id = SDF_APP_ID;
        s_has_creds = true;
        ESP_LOGI(TAG, "Loaded stored Nuki credentials");
    }

    sdf_nuki_client_init(
        &s_client,
        &creds,
        sdf_app_send_encrypted,
        NULL,
        sdf_app_send_unencrypted,
        NULL,
        sdf_app_on_message,
        NULL);

    sdf_nuki_ble_init(&s_ble, sdf_app_on_ble_rx, NULL, sdf_app_on_ble_ready, NULL);
    sdf_nuki_ble_set_target_addr(&s_ble, &SDF_NUKI_TARGET_ADDR);
    ESP_LOGI(TAG, "Starting BLE scan (target address set)");
    sdf_nuki_ble_start(&s_ble);
}

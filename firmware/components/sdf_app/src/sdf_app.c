#include "sdf_app.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "sdf_nuki_ble_transport.h"
#include "sdf_nuki_pairing.h"
#include "sdf_protocol_ble.h"
#include "sdf_protocol_zigbee.h"
#include "sdf_storage.h"

#define SDF_APP_ID 1u
#define SDF_APP_NAME "SDF"
#define SDF_APP_LOCK_SUFFIX "SDF"
#define SDF_APP_LOCK_ACTION_MAX_RETRIES 2u
#define SDF_APP_ZB_ALARM_ACTION_FAILURE 0x0001u
#define SDF_APP_ZB_ALARM_LOW_BATTERY 0x0002u
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
static uint16_t s_zigbee_alarm_mask;
static bool s_latch_sequence_active;

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

static void sdf_app_set_alarm_mask_bits(uint16_t set_bits, uint16_t clear_bits)
{
    s_zigbee_alarm_mask |= set_bits;
    s_zigbee_alarm_mask &= (uint16_t)(~clear_bits);
    sdf_protocol_zigbee_update_alarm_mask(s_zigbee_alarm_mask);
}

static void sdf_app_abort_latch_sequence(const char *reason)
{
    if (!s_latch_sequence_active) {
        return;
    }

    s_latch_sequence_active = false;
    ESP_LOGW(TAG, "Aborted latch sequence: %s", reason);
}

static sdf_protocol_zigbee_lock_state_t sdf_app_map_lock_state_to_zigbee(uint8_t nuki_lock_state)
{
    switch (nuki_lock_state) {
    case 0x01: /* locked */
        return SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED;
    case 0x03: /* unlocked */
    case 0x05: /* unlatched */
    case 0x06: /* unlocked (lock n go) */
        return SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED;
    case 0x02: /* unlocking */
    case 0x04: /* locking */
    case 0x07: /* unlatching */
        return SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED;
    default:
        return SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED;
    }
}

static void sdf_app_update_zigbee_from_action(uint8_t lock_action)
{
    switch (lock_action) {
    case SDF_NUKI_LOCK_ACTION_LOCK:
    case SDF_NUKI_LOCK_ACTION_LOCK_N_GO:
    case SDF_NUKI_LOCK_ACTION_FULL_LOCK:
        sdf_protocol_zigbee_update_lock_state(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED);
        break;
    case SDF_NUKI_LOCK_ACTION_UNLOCK:
    case SDF_NUKI_LOCK_ACTION_UNLATCH:
        sdf_protocol_zigbee_update_lock_state(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED);
        break;
    default:
        break;
    }
}

static int sdf_app_start_unlock_unlatch_sequence(void)
{
    if (s_latch_sequence_active || s_lock_flow.state != SDF_APP_LOCK_FLOW_IDLE) {
        return SDF_NUKI_RESULT_ERR_INCOMPLETE;
    }

    int res = sdf_app_lock_action(SDF_NUKI_LOCK_ACTION_UNLOCK, 0);
    if (res == SDF_NUKI_RESULT_OK) {
        s_latch_sequence_active = true;
        ESP_LOGI(TAG, "Started latch sequence: unlock -> unlatch");
    }
    return res;
}

static esp_err_t sdf_app_on_zigbee_command(void *ctx, sdf_protocol_zigbee_command_t command)
{
    (void)ctx;

    int res = SDF_NUKI_RESULT_ERR_ARG;
    switch (command) {
    case SDF_PROTOCOL_ZIGBEE_COMMAND_LOCK:
        ESP_LOGI(TAG, "Received Zigbee lock command");
        res = sdf_app_lock_action(SDF_NUKI_LOCK_ACTION_LOCK, 0);
        break;
    case SDF_PROTOCOL_ZIGBEE_COMMAND_UNLOCK:
        ESP_LOGI(TAG, "Received Zigbee unlock command");
        res = sdf_app_lock_action(SDF_NUKI_LOCK_ACTION_UNLOCK, 0);
        break;
    case SDF_PROTOCOL_ZIGBEE_COMMAND_LATCH:
        ESP_LOGI(TAG, "Received Zigbee latch command (unlock + unlatch)");
        res = sdf_app_start_unlock_unlatch_sequence();
        break;
    case SDF_PROTOCOL_ZIGBEE_COMMAND_PROGRAMMING_EVENT:
        ESP_LOGI(TAG, "Received Zigbee programming event (enrollment flow pending)");
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    if (res != SDF_NUKI_RESULT_OK) {
        ESP_LOGW(TAG, "Unable to execute Zigbee command, lock action result=%d", res);
        sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
        return ESP_FAIL;
    }

    return ESP_OK;
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
        sdf_app_abort_latch_sequence("lock action failed");
        sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
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
        sdf_app_abort_latch_sequence("challenge retry failed");
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
                sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_ACTION_FAILURE);
                sdf_app_update_zigbee_from_action(action);

                if (s_latch_sequence_active && action == SDF_NUKI_LOCK_ACTION_UNLOCK) {
                    int res = sdf_app_lock_action(SDF_NUKI_LOCK_ACTION_UNLATCH, 0);
                    if (res == SDF_NUKI_RESULT_OK) {
                        ESP_LOGI(TAG, "Latch sequence continuing with unlatch");
                        return;
                    }
                    ESP_LOGW(TAG, "Failed to start unlatch step in latch sequence: %d", res);
                    sdf_app_abort_latch_sequence("failed to start unlatch");
                    sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);
                } else if (s_latch_sequence_active) {
                    if (action == SDF_NUKI_LOCK_ACTION_UNLATCH) {
                        ESP_LOGI(TAG, "Latch sequence finished");
                    }
                    s_latch_sequence_active = false;
                }

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

        sdf_protocol_zigbee_update_lock_state(sdf_app_map_lock_state_to_zigbee(state.lock_state));
        if (state.critical_battery_state) {
            sdf_protocol_zigbee_update_battery_percent(10);
            sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_LOW_BATTERY, 0);
        } else {
            sdf_protocol_zigbee_update_battery_percent(100);
            sdf_app_set_alarm_mask_bits(0, SDF_APP_ZB_ALARM_LOW_BATTERY);
        }
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
        sdf_app_set_alarm_mask_bits(SDF_APP_ZB_ALARM_ACTION_FAILURE, 0);

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
        sdf_app_abort_latch_sequence("BLE reconnect during action");
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
    s_zigbee_alarm_mask = 0;
    s_latch_sequence_active = false;

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

    err = sdf_protocol_zigbee_set_command_handler(sdf_app_on_zigbee_command, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set Zigbee command handler: %s", esp_err_to_name(err));
    }

    err = sdf_protocol_zigbee_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start Zigbee protocol: %s", esp_err_to_name(err));
    } else {
        sdf_protocol_zigbee_update_lock_state(SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED);
        sdf_protocol_zigbee_update_battery_percent(100);
        sdf_protocol_zigbee_update_alarm_mask(0);
    }

    sdf_nuki_ble_init(&s_ble, sdf_app_on_ble_rx, NULL, sdf_app_on_ble_ready, NULL);
    sdf_nuki_ble_set_target_addr(&s_ble, &SDF_NUKI_TARGET_ADDR);
    ESP_LOGI(TAG, "Starting BLE scan (target address set)");
    sdf_nuki_ble_start(&s_ble);
}

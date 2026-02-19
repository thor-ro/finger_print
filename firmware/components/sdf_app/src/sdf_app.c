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

static const char *TAG = "sdf_app";

static sdf_nuki_ble_transport_t s_ble;
static sdf_nuki_client_t s_client;
static sdf_nuki_pairing_t s_pairing;
static bool s_has_creds;
static bool s_pairing_active;

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

static void sdf_app_on_message(void *ctx, const sdf_nuki_message_t *msg)
{
    (void)ctx;
    ESP_LOGI(TAG, "Nuki message cmd=0x%04X len=%u", msg->command_id, (unsigned)msg->payload_len);
}

static void sdf_app_on_ble_ready(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "BLE transport ready");

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
            }
        }
        return;
    }

    sdf_nuki_client_feed_encrypted(&s_client, data, len);
}

void sdf_app_init(void)
{
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
    ESP_LOGI(TAG, "Starting BLE scan (set target address to avoid accidental pairing)");
    sdf_nuki_ble_start(&s_ble);
}

#include "sdf_ble_companion.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "sdf_ota.h"

#define SDF_BLE_OTA_SSID_MAX_LEN 32
#define SDF_BLE_OTA_PASSWORD_MAX_LEN 63
#define SDF_BLE_OTA_URL_MAX_LEN 256
#define SDF_BLE_OTA_CONNECT_TIMEOUT_MS 20000
#define SDF_BLE_OTA_DOWNLOAD_BUFFER_LEN 4096

#define SDF_BLE_OTA_WIFI_CONNECTED BIT0
#define SDF_BLE_OTA_WIFI_FAILED BIT1

static const char *TAG = "sdf_ble_ota";

typedef struct {
    char ssid[SDF_BLE_OTA_SSID_MAX_LEN + 1];
    char password[SDF_BLE_OTA_PASSWORD_MAX_LEN + 1];
    char firmware_url[SDF_BLE_OTA_URL_MAX_LEN + 1];
} sdf_ble_ota_request_t;

static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_wifi_netif;
static bool s_wifi_initialized;
static bool s_ota_task_running;

static void sdf_ble_ota_event_handler(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data) {
    (void)arg;
    (void)event_data;

    if (s_wifi_events == NULL) {
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, SDF_BLE_OTA_WIFI_CONNECTED);
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_events, SDF_BLE_OTA_WIFI_FAILED);
    }
}

static esp_err_t sdf_ble_ota_init_wifi(void) {
    if (s_wifi_initialized) {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (s_wifi_events == NULL) {
        s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_wifi_netif == NULL) {
        s_wifi_netif = esp_netif_create_default_wifi_sta();
        if (s_wifi_netif == NULL) {
            return ESP_FAIL;
        }
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                     sdf_ble_ota_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     sdf_ble_ota_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_wifi_initialized = true;
    return ESP_OK;
}

static esp_err_t sdf_ble_ota_connect(const sdf_ble_ota_request_t *request) {
    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, request->ssid, strlen(request->ssid));
    memcpy(wifi_config.sta.password, request->password,
           strlen(request->password));

    xEventGroupClearBits(s_wifi_events,
                         SDF_BLE_OTA_WIFI_CONNECTED | SDF_BLE_OTA_WIFI_FAILED);
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, SDF_BLE_OTA_WIFI_CONNECTED | SDF_BLE_OTA_WIFI_FAILED,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(SDF_BLE_OTA_CONNECT_TIMEOUT_MS));
    if ((bits & SDF_BLE_OTA_WIFI_CONNECTED) != 0) {
        return ESP_OK;
    }
    return (bits & SDF_BLE_OTA_WIFI_FAILED) != 0 ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

static esp_err_t sdf_ble_ota_download(const char *firmware_url) {
    esp_http_client_config_t config = {
        .url = firmware_url,
        .timeout_ms = SDF_BLE_OTA_CONNECT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    const int64_t content_length = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200 || content_length <= 0 || content_length > UINT32_MAX) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    err = sdf_ota_init();
    sdf_ota_handle_t ota_handle = NULL;
    if (err == ESP_OK) {
        err = sdf_ota_begin(SDF_OTA_SOURCE_BLE, (uint32_t)content_length,
                            &ota_handle);
    }

    uint8_t *buffer = NULL;
    if (err == ESP_OK) {
        buffer = malloc(SDF_BLE_OTA_DOWNLOAD_BUFFER_LEN);
        if (buffer == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }

    while (err == ESP_OK) {
        const int read_len = esp_http_client_read(
            client, (char *)buffer, SDF_BLE_OTA_DOWNLOAD_BUFFER_LEN);
        if (read_len < 0) {
            err = ESP_FAIL;
        } else if (read_len == 0) {
            break;
        } else {
            err = sdf_ota_write(ota_handle, buffer, (uint32_t)read_len);
        }
    }

    if (err == ESP_OK) {
        err = sdf_ota_verify_integrity(ota_handle);
    }
    if (err == ESP_OK) {
        err = sdf_ota_verify_and_commit(ota_handle);
    }
    if (err != ESP_OK && ota_handle != NULL) {
        (void)sdf_ota_abort(ota_handle);
    }

    if (buffer != NULL) {
        mbedtls_platform_zeroize(buffer, SDF_BLE_OTA_DOWNLOAD_BUFFER_LEN);
        free(buffer);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static void sdf_ble_ota_task(void *arg) {
    sdf_ble_ota_request_t *request = arg;
    esp_err_t err = sdf_ble_ota_init_wifi();
    if (err == ESP_OK) {
        err = sdf_ble_ota_connect(request);
    }
    if (err == ESP_OK) {
        err = sdf_ble_ota_download(request->firmware_url);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE OTA failed: %s", esp_err_to_name(err));
    }

    mbedtls_platform_zeroize(request, sizeof(*request));
    free(request);
    s_ota_task_running = false;
    vTaskDelete(NULL);
}

esp_err_t sdf_ble_companion_start_ota_request(const uint8_t *data, size_t len) {
    if (data == NULL || len == 0 || len >= 512 || s_ota_task_running) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts((const char *)data, len,
                                             &parse_end, false);
    if (root == NULL || parse_end != (const char *)data + len) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    const cJSON *firmware_url =
        cJSON_GetObjectItemCaseSensitive(root, "firmwareUrl");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password) ||
        !cJSON_IsString(firmware_url) || ssid->valuestring == NULL ||
        password->valuestring == NULL || firmware_url->valuestring == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_len = strlen(ssid->valuestring);
    const size_t password_len = strlen(password->valuestring);
    const size_t url_len = strlen(firmware_url->valuestring);
    if (ssid_len == 0 || ssid_len > SDF_BLE_OTA_SSID_MAX_LEN ||
        password_len > SDF_BLE_OTA_PASSWORD_MAX_LEN ||
        url_len <= strlen("https://") || url_len > SDF_BLE_OTA_URL_MAX_LEN ||
        strncmp(firmware_url->valuestring, "https://", strlen("https://")) !=
            0) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    sdf_ble_ota_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    memcpy(request->ssid, ssid->valuestring, ssid_len + 1);
    memcpy(request->password, password->valuestring, password_len + 1);
    memcpy(request->firmware_url, firmware_url->valuestring, url_len + 1);
    cJSON_Delete(root);

    s_ota_task_running = true;
    if (xTaskCreate(sdf_ble_ota_task, "sdf_ble_ota", 8192, request,
                    tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        s_ota_task_running = false;
        mbedtls_platform_zeroize(request, sizeof(*request));
        free(request);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

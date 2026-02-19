#include "sdf_services.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SDF_SERVICES_TASK_NAME "sdf_fp"
#define SDF_SERVICES_TASK_STACK 4096
#define SDF_SERVICES_TASK_PRIORITY 5

#define SDF_SERVICES_LOCK_WAIT_MS 250u

#define SDF_SERVICES_DEFAULT_UART_PORT 1
#define SDF_SERVICES_DEFAULT_UART_TX_PIN 4
#define SDF_SERVICES_DEFAULT_UART_RX_PIN 5
#define SDF_SERVICES_DEFAULT_UART_BAUD_RATE 19200u
#define SDF_SERVICES_DEFAULT_UART_TIMEOUT_MS 12000u
#define SDF_SERVICES_DEFAULT_UART_RX_BUFFER 256u
#define SDF_SERVICES_DEFAULT_UART_TX_BUFFER 256u

#define SDF_SERVICES_DEFAULT_MATCH_POLL_MS 400u
#define SDF_SERVICES_DEFAULT_MATCH_COOLDOWN_MS 3000u

/*
 * LED command values are vendor-module specific. These are best-effort defaults
 * and can be tuned without changing the enrollment flow.
 */
#define SDF_SERVICES_LED_MODE_BREATH 0x01u
#define SDF_SERVICES_LED_MODE_FLASH 0x02u
#define SDF_SERVICES_LED_MODE_SOLID 0x03u
#define SDF_SERVICES_LED_COLOR_RED 0x01u
#define SDF_SERVICES_LED_COLOR_GREEN 0x02u
#define SDF_SERVICES_LED_COLOR_BLUE 0x03u

static const char *TAG = "sdf_services";

typedef struct {
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    bool initialized;
    sdf_services_config_t config;
    sdf_enrollment_sm_t enrollment;
    bool enrollment_request_pending;
    uint16_t request_user_id;
    uint8_t request_permission;
    int64_t match_cooldown_until_us;
} sdf_services_state_t;

static sdf_services_state_t s_state = {0};

static const char *sdf_services_enrollment_result_name(sdf_enrollment_result_t result)
{
    switch (result) {
    case SDF_ENROLLMENT_RESULT_NONE:
        return "NONE";
    case SDF_ENROLLMENT_RESULT_SUCCESS:
        return "SUCCESS";
    case SDF_ENROLLMENT_RESULT_FAILED:
        return "FAILED";
    case SDF_ENROLLMENT_RESULT_TIMEOUT:
        return "TIMEOUT";
    case SDF_ENROLLMENT_RESULT_FULL:
        return "FULL";
    case SDF_ENROLLMENT_RESULT_USER_OCCUPIED:
        return "USER_OCCUPIED";
    case SDF_ENROLLMENT_RESULT_FINGER_OCCUPIED:
        return "FINGER_OCCUPIED";
    case SDF_ENROLLMENT_RESULT_PROTOCOL_ERROR:
        return "PROTOCOL_ERROR";
    case SDF_ENROLLMENT_RESULT_IO_ERROR:
        return "IO_ERROR";
    case SDF_ENROLLMENT_RESULT_BAD_ARG:
        return "BAD_ARG";
    case SDF_ENROLLMENT_RESULT_BUSY:
        return "BUSY";
    default:
        return "UNKNOWN";
    }
}

static const char *sdf_services_fingerprint_result_name(sdf_fingerprint_op_result_t result)
{
    switch (result) {
    case SDF_FINGERPRINT_OP_OK:
        return "OK";
    case SDF_FINGERPRINT_OP_NO_MATCH:
        return "NO_MATCH";
    case SDF_FINGERPRINT_OP_TIMEOUT:
        return "TIMEOUT";
    case SDF_FINGERPRINT_OP_FULL:
        return "FULL";
    case SDF_FINGERPRINT_OP_USER_OCCUPIED:
        return "USER_OCCUPIED";
    case SDF_FINGERPRINT_OP_FINGER_OCCUPIED:
        return "FINGER_OCCUPIED";
    case SDF_FINGERPRINT_OP_FAILED:
        return "FAILED";
    case SDF_FINGERPRINT_OP_IO_ERROR:
        return "IO_ERROR";
    case SDF_FINGERPRINT_OP_PROTOCOL_ERROR:
        return "PROTOCOL_ERROR";
    case SDF_FINGERPRINT_OP_BAD_ARG:
        return "BAD_ARG";
    default:
        return "UNKNOWN";
    }
}

static void sdf_services_notify_enrollment(void)
{
    if (s_state.lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    sdf_services_enrollment_cb cb = s_state.config.enrollment_cb;
    void *ctx = s_state.config.enrollment_ctx;
    sdf_enrollment_sm_t snapshot = s_state.enrollment;
    xSemaphoreGive(s_state.lock);

    if (cb != NULL) {
        cb(ctx, &snapshot);
    }
}

static void sdf_services_try_set_led(uint8_t mode, uint8_t color, uint8_t cycles)
{
    sdf_fingerprint_led_command_t led_cmd = {
        .mode = mode,
        .color = color,
        .cycles = cycles,
    };

    sdf_fingerprint_op_result_t led_result = sdf_fingerprint_driver_control_led(&led_cmd);
    if (led_result != SDF_FINGERPRINT_OP_OK) {
        ESP_LOGD(TAG, "LED command failed: %s", sdf_services_fingerprint_result_name(led_result));
    }
}

static void sdf_services_handle_enrollment_feedback(const sdf_enrollment_sm_t *before, const sdf_enrollment_sm_t *after)
{
    if (before == NULL || after == NULL) {
        return;
    }

    if (after->state == SDF_ENROLLMENT_STATE_SUCCESS) {
        sdf_services_try_set_led(SDF_SERVICES_LED_MODE_SOLID, SDF_SERVICES_LED_COLOR_GREEN, 2);
        return;
    }

    if (after->state == SDF_ENROLLMENT_STATE_ERROR) {
        sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH, SDF_SERVICES_LED_COLOR_RED, 3);
        return;
    }

    if (after->completed_steps > before->completed_steps) {
        uint8_t flashes = after->completed_steps;
        if (flashes == 0) {
            flashes = 1;
        }
        sdf_services_try_set_led(SDF_SERVICES_LED_MODE_FLASH, SDF_SERVICES_LED_COLOR_GREEN, flashes);
    }
}

static void sdf_services_run_match_cycle(void)
{
    sdf_services_unlock_cb unlock_cb = NULL;
    void *unlock_ctx = NULL;
    uint32_t cooldown_ms = SDF_SERVICES_DEFAULT_MATCH_COOLDOWN_MS;
    int64_t now_us = esp_timer_get_time();

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    if (now_us < s_state.match_cooldown_until_us || sdf_enrollment_sm_is_active(&s_state.enrollment) ||
        s_state.enrollment_request_pending) {
        xSemaphoreGive(s_state.lock);
        return;
    }

    unlock_cb = s_state.config.unlock_cb;
    unlock_ctx = s_state.config.unlock_ctx;
    cooldown_ms = s_state.config.match_cooldown_ms;
    xSemaphoreGive(s_state.lock);

    if (unlock_cb == NULL) {
        return;
    }

    sdf_fingerprint_match_t match = {0};
    sdf_fingerprint_op_result_t match_result = sdf_fingerprint_driver_match_1n(&match);
    if (match_result == SDF_FINGERPRINT_OP_NO_MATCH || match_result == SDF_FINGERPRINT_OP_TIMEOUT) {
        return;
    }

    if (match_result != SDF_FINGERPRINT_OP_OK) {
        ESP_LOGW(TAG, "Fingerprint match error: %s", sdf_services_fingerprint_result_name(match_result));
        return;
    }

    ESP_LOGI(
        TAG,
        "Fingerprint match user_id=%u permission=%u",
        (unsigned)match.user_id,
        (unsigned)match.permission);
    int unlock_result = unlock_cb(unlock_ctx, match.user_id);
    if (unlock_result != 0) {
        ESP_LOGW(TAG, "Unlock callback returned %d for user_id=%u", unlock_result, (unsigned)match.user_id);
    }

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
        s_state.match_cooldown_until_us = now_us + ((int64_t)cooldown_ms * 1000LL);
        xSemaphoreGive(s_state.lock);
    }
}

static void sdf_services_run_enrollment_step(void)
{
    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    if (!sdf_enrollment_sm_is_active(&s_state.enrollment)) {
        xSemaphoreGive(s_state.lock);
        return;
    }

    sdf_enrollment_sm_t before = s_state.enrollment;
    uint8_t step = sdf_enrollment_sm_current_step(&s_state.enrollment);
    uint16_t user_id = s_state.enrollment.user_id;
    uint8_t permission = s_state.enrollment.permission;
    xSemaphoreGive(s_state.lock);

    sdf_fingerprint_enroll_step_t driver_step = SDF_FINGERPRINT_ENROLL_STEP_1;
    switch (step) {
    case 1:
        driver_step = SDF_FINGERPRINT_ENROLL_STEP_1;
        break;
    case 2:
        driver_step = SDF_FINGERPRINT_ENROLL_STEP_2;
        break;
    case 3:
        driver_step = SDF_FINGERPRINT_ENROLL_STEP_3;
        break;
    default:
        return;
    }

    sdf_fingerprint_op_result_t step_result =
        sdf_fingerprint_driver_enroll_step(driver_step, user_id, permission);

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    sdf_enrollment_sm_apply_step_result(&s_state.enrollment, step_result);
    sdf_enrollment_sm_t after = s_state.enrollment;
    xSemaphoreGive(s_state.lock);

    ESP_LOGI(
        TAG,
        "Enrollment step %u result=%s state=%d completed=%u",
        (unsigned)step,
        sdf_services_fingerprint_result_name(step_result),
        (int)after.state,
        (unsigned)after.completed_steps);

    sdf_services_handle_enrollment_feedback(&before, &after);
    sdf_services_notify_enrollment();

    if (after.state == SDF_ENROLLMENT_STATE_SUCCESS || after.state == SDF_ENROLLMENT_STATE_ERROR) {
        ESP_LOGI(
            TAG,
            "Enrollment finished for user_id=%u (%s)",
            (unsigned)after.user_id,
            sdf_services_enrollment_result_name(after.result));

        if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
            s_state.match_cooldown_until_us =
                esp_timer_get_time() + ((int64_t)s_state.config.match_cooldown_ms * 1000LL);
            xSemaphoreGive(s_state.lock);
        }
    }
}

static void sdf_services_start_pending_enrollment_if_any(void)
{
    bool started = false;
    bool failed = false;

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return;
    }

    if (!s_state.enrollment_request_pending || sdf_enrollment_sm_is_active(&s_state.enrollment)) {
        xSemaphoreGive(s_state.lock);
        return;
    }

    esp_err_t err = sdf_enrollment_sm_start(&s_state.enrollment, s_state.request_user_id, s_state.request_permission);
    s_state.enrollment_request_pending = false;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to start enrollment state machine: %s", esp_err_to_name(err));
        failed = true;
        xSemaphoreGive(s_state.lock);
    } else {
        ESP_LOGI(
            TAG,
            "Enrollment started for user_id=%u permission=%u",
            (unsigned)s_state.enrollment.user_id,
            (unsigned)s_state.enrollment.permission);
        started = true;
        xSemaphoreGive(s_state.lock);
    }

    if (started) {
        sdf_services_try_set_led(SDF_SERVICES_LED_MODE_BREATH, SDF_SERVICES_LED_COLOR_BLUE, 1);
        sdf_services_notify_enrollment();
        return;
    }

    if (failed) {
        sdf_services_notify_enrollment();
    }
}

static void sdf_services_task(void *arg)
{
    (void)arg;

    while (true) {
        uint32_t poll_interval_ms = SDF_SERVICES_DEFAULT_MATCH_POLL_MS;

        if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
            poll_interval_ms = s_state.config.match_poll_interval_ms;
            xSemaphoreGive(s_state.lock);
        }

        sdf_services_start_pending_enrollment_if_any();
        sdf_services_run_enrollment_step();
        sdf_services_run_match_cycle();
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
    }
}

void sdf_services_get_default_config(sdf_services_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->fingerprint.uart_port = SDF_SERVICES_DEFAULT_UART_PORT;
    config->fingerprint.tx_pin = SDF_SERVICES_DEFAULT_UART_TX_PIN;
    config->fingerprint.rx_pin = SDF_SERVICES_DEFAULT_UART_RX_PIN;
    config->fingerprint.baud_rate = SDF_SERVICES_DEFAULT_UART_BAUD_RATE;
    config->fingerprint.response_timeout_ms = SDF_SERVICES_DEFAULT_UART_TIMEOUT_MS;
    config->fingerprint.rx_buffer_size = SDF_SERVICES_DEFAULT_UART_RX_BUFFER;
    config->fingerprint.tx_buffer_size = SDF_SERVICES_DEFAULT_UART_TX_BUFFER;

    config->match_poll_interval_ms = SDF_SERVICES_DEFAULT_MATCH_POLL_MS;
    config->match_cooldown_ms = SDF_SERVICES_DEFAULT_MATCH_COOLDOWN_MS;
    config->unlock_cb = NULL;
    config->unlock_ctx = NULL;
    config->enrollment_cb = NULL;
    config->enrollment_ctx = NULL;
}

esp_err_t sdf_services_init(const sdf_services_config_t *config)
{
    if (config == NULL || config->match_poll_interval_ms == 0 || config->match_cooldown_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_state.lock == NULL) {
        s_state.lock = xSemaphoreCreateMutex();
        if (s_state.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_state.initialized) {
        xSemaphoreGive(s_state.lock);
        return ESP_OK;
    }

    s_state.config = *config;
    sdf_enrollment_sm_init(&s_state.enrollment);
    s_state.enrollment_request_pending = false;
    s_state.request_user_id = 0;
    s_state.request_permission = 0;
    s_state.match_cooldown_until_us = 0;
    xSemaphoreGive(s_state.lock);

    esp_err_t err = sdf_fingerprint_driver_init(&config->fingerprint);
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t task_ok = xTaskCreate(
        sdf_services_task,
        SDF_SERVICES_TASK_NAME,
        SDF_SERVICES_TASK_STACK,
        NULL,
        SDF_SERVICES_TASK_PRIORITY,
        &s_state.task);
    if (task_ok != pdPASS) {
        sdf_fingerprint_driver_deinit();
        return ESP_FAIL;
    }

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
        s_state.initialized = true;
        xSemaphoreGive(s_state.lock);
    }

    ESP_LOGI(TAG, "Fingerprint services initialized");
    return ESP_OK;
}

bool sdf_services_is_ready(void)
{
    if (s_state.lock == NULL) {
        return false;
    }

    bool ready = false;
    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
        ready = s_state.initialized;
        xSemaphoreGive(s_state.lock);
    }
    return ready;
}

esp_err_t sdf_services_request_enrollment(uint16_t user_id, uint8_t permission)
{
    if (user_id < SDF_FINGERPRINT_USER_ID_MIN || user_id > SDF_FINGERPRINT_USER_ID_MAX ||
        permission < 1u || permission > 3u) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_state.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_state.initialized || s_state.enrollment_request_pending || sdf_enrollment_sm_is_active(&s_state.enrollment)) {
        xSemaphoreGive(s_state.lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_state.request_user_id = user_id;
    s_state.request_permission = permission;
    s_state.enrollment_request_pending = true;
    xSemaphoreGive(s_state.lock);
    return ESP_OK;
}

bool sdf_services_is_enrollment_active(void)
{
    if (s_state.lock == NULL) {
        return false;
    }

    bool active = false;
    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
        active = sdf_enrollment_sm_is_active(&s_state.enrollment) || s_state.enrollment_request_pending;
        xSemaphoreGive(s_state.lock);
    }
    return active;
}

sdf_enrollment_sm_t sdf_services_get_enrollment_state(void)
{
    sdf_enrollment_sm_t snapshot;
    sdf_enrollment_sm_init(&snapshot);

    if (s_state.lock == NULL) {
        return snapshot;
    }

    if (xSemaphoreTake(s_state.lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) == pdTRUE) {
        snapshot = s_state.enrollment;
        xSemaphoreGive(s_state.lock);
    }
    return snapshot;
}

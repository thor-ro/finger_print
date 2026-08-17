#include "sdf_ble_companion_gatt_scratch.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sdf_ble_scratch";

static uint8_t s_scratch[SDF_BLE_COMPANION_GATT_SCRATCH_LEN];
static TaskHandle_t s_owner = NULL;
static bool s_held = false;
static uint32_t s_violations = 0;

/* The acquire/release lifetime spans the release of s_lock - the whole point
 * of staging is to survive it - so s_lock cannot be what makes these checks
 * atomic. A short critical section makes the owner/held test-and-set
 * indivisible against any other task, which is exactly the class of caller
 * this module exists to catch. Nothing here blocks or logs inside the
 * region. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void sdf_ble_companion_gatt_scratch_bind_owner(void) {
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    TaskHandle_t previous;

    portENTER_CRITICAL(&s_mux);
    previous = s_owner;
    if (previous == NULL) {
        s_owner = self;
    }
    portEXIT_CRITICAL(&s_mux);

    if (previous == NULL) {
        /* Logged once, at init volume, so a boot log shows staging came
         * under ownership before advertising started. */
        ESP_LOGI(TAG, "GATT write staging owned by task '%s'", pcTaskGetName(self));
        return;
    }
    if (previous == self) {
        /* The host task re-entering its sync hook after a NimBLE resync. */
        return;
    }

    portENTER_CRITICAL(&s_mux);
    s_violations++;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGE(TAG, "bind refused: task '%s' is not the GATT staging owner '%s'",
             pcTaskGetName(self), pcTaskGetName(previous));
}

uint8_t *sdf_ble_companion_gatt_scratch_acquire(void) {
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    bool granted;
    const char *reason;

    portENTER_CRITICAL(&s_mux);
    if (s_owner == NULL) {
        granted = false;
        reason = "staging has no owner yet";
    } else if (s_owner != self) {
        granted = false;
        reason = "caller is not the staging owner";
    } else if (s_held) {
        granted = false;
        reason = "staging is already held";
    } else {
        granted = true;
        reason = NULL;
        s_held = true;
    }
    if (!granted) s_violations++;
    portEXIT_CRITICAL(&s_mux);

    if (!granted) {
        /* A contract violation, not a malformed client request: the caller
         * fails this one GATT operation with an ATT error and the device
         * keeps running. */
        ESP_LOGE(TAG, "acquire refused for task '%s': %s", pcTaskGetName(self), reason);
        return NULL;
    }
    return s_scratch;
}

void sdf_ble_companion_gatt_scratch_release(void) {
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    bool wrong_task;

    portENTER_CRITICAL(&s_mux);
    /* Releasing when nothing is held is a no-op and is not a violation, so
     * an error path may release unconditionally. Releasing from a non-owner
     * is refused and must leave s_held set - clearing it would hand the
     * owner's in-flight buffer to the next acquirer. */
    wrong_task = s_held && s_owner != self;
    if (s_held && !wrong_task) s_held = false;
    if (wrong_task) s_violations++;
    portEXIT_CRITICAL(&s_mux);

    if (wrong_task) {
        ESP_LOGE(TAG, "release refused: task '%s' does not hold GATT staging",
                 pcTaskGetName(self));
    }
}

uint32_t sdf_ble_companion_gatt_scratch_violation_count(void) {
    uint32_t count;
    portENTER_CRITICAL(&s_mux);
    count = s_violations;
    portEXIT_CRITICAL(&s_mux);
    return count;
}

#ifdef CONFIG_IDF_TARGET_LINUX
void sdf_ble_companion_gatt_scratch_reset_for_test(void) {
    portENTER_CRITICAL(&s_mux);
    s_owner = NULL;
    s_held = false;
    s_violations = 0;
    portEXIT_CRITICAL(&s_mux);
    memset(s_scratch, 0, sizeof(s_scratch));
}
#endif

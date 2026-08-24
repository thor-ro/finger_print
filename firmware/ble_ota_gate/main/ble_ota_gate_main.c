#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdf_app.h"
#include "sdf_ble_companion.h"
#include "sdf_services_internal.h"

/* ble-ota-emulator-harness Layer 2 fixture (add-ble-ota-emulator-harness,
 * tasks.md 7.1 and 7.3). Identical to firmware/main/main.c's app_main() -
 * which this file deliberately mirrors rather than modifies - with two
 * additions, both fixture-only substitutes for a physical action production
 * requires:
 *
 *  1. A boot-time call to sdf_ble_companion_open_pairing_window(). Production
 *     only reaches that call from sdf_app.c's
 *     SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW admin-action handler,
 *     which requires a physical GPIO button press authorized by an Admin
 *     fingerprint (sdf_app.c:558-569) - there is no CLI, Zigbee, or other
 *     software-reachable trigger (confirmed by search; see design.md D6's
 *     "Outcome" note). An emulator has no GPIO, so this fixture calls the
 *     same public sdf_ble_companion API directly instead - see design.md D7.
 *
 *  2. A background task that auto-approves any pending admin-fingerprint-
 *     gated action (sdf_services_admin_action_t) - needed for task 7.3's
 *     REGISTER over BLE, which routes through
 *     sdf_services_request_admin_action(SDF_SERVICES_ADMIN_ACTION_WEB_REG_
 *     AUTH) and is normally only resolved by a real fingerprint sensor
 *     producing an ADMIN-permission (permission==3) match
 *     (sdf_services_match.c's sdf_match_task ->
 *     sdf_services_try_claim_admin_action()). An emulator has no fingerprint
 *     sensor, so this task calls that same function directly with a
 *     synthetic ADMIN match, on the same poll cadence a real admin's finger
 *     press would satisfy it. sdf_services_try_claim_admin_action() and the
 *     sdf_fingerprint_match_t type it takes are declared in
 *     "sdf_services_internal.h" / "fingerprint.h" - both still sit under
 *     sdf_services's and sdf_drivers's *public* (non-PRIV_INCLUDE_DIRS)
 *     include dirs (confirmed against sdf_services/CMakeLists.txt), and
 *     firmware/components/sdf_services/test/test_sdf_services.c already
 *     includes "sdf_services_internal.h" for the same kind of white-box
 *     access - so this is established repo convention for test/fixture code,
 *     not a boundary violation. See design.md D10. No production source is
 *     modified for either addition.
 */

static const char *TAG = "ble_ota_gate";

#define BLE_OTA_GATE_ADMIN_APPROVE_POLL_MS 200
#define BLE_OTA_GATE_ADMIN_APPROVE_TASK_STACK 3072
#define BLE_OTA_GATE_ADMIN_APPROVE_TASK_PRIORITY 4

/* Fixture-only "admin is always present" substitute for a real fingerprint
 * sensor: repeatedly offers an ADMIN-permission (permission==3) match to
 * sdf_services_try_claim_admin_action(), which is a harmless no-op
 * (has_pending=false, returns false) whenever no admin action is actually
 * pending. See the file header comment (point 2) and design.md D10. */
static void ble_ota_gate_admin_approve_task(void *arg) {
  (void)arg;
  const sdf_fingerprint_match_t admin_match = {
      .user_id = 1,
      .permission = 3,
  };
  while (true) {
    if (sdf_services_try_claim_admin_action(&admin_match)) {
      ESP_LOGI(TAG, "Auto-approved a pending admin-fingerprint-gated action");
    }
    vTaskDelay(pdMS_TO_TICKS(BLE_OTA_GATE_ADMIN_APPROVE_POLL_MS));
  }
}

void app_main(void) {
  esp_err_t err = sdf_app_init();
  if (err != ESP_OK) {
    /* Same degraded-boot policy as firmware/main/main.c: a subsystem
     * failing to come up should not halt the fixture, since the BLE
     * companion service (what this harness actually exercises) may still
     * be healthy even if e.g. the fingerprint sensor or Zigbee radio isn't
     * present under emulation. */
    ESP_LOGE(TAG,
             "*** DEGRADED BOOT: sdf_app_init() failed (%s); ble_ota_gate is "
             "running with one or more subsystems disabled ***",
             esp_err_to_name(err));
  }

  esp_err_t pw_err = sdf_ble_companion_open_pairing_window();
  if (pw_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open BLE pairing window: %s",
             esp_err_to_name(pw_err));
  } else {
    ESP_LOGI(TAG, "BLE pairing window open");
  }

  xTaskCreate(ble_ota_gate_admin_approve_task, "ble_ota_gate_admin_approve",
              BLE_OTA_GATE_ADMIN_APPROVE_TASK_STACK, NULL,
              BLE_OTA_GATE_ADMIN_APPROVE_TASK_PRIORITY, NULL);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdf_app.h"
#include "sdf_ble_companion.h"
#include "sdf_services_internal.h"
#include "sdf_storage.h"

/* ble-ota-emulator-harness Layer 2 fixture (add-ble-ota-emulator-harness,
 * tasks.md 7.1 and 7.3). Identical to firmware/main/main.c's app_main() -
 * which this file deliberately mirrors rather than modifies - with additions,
 * all fixture-only substitutes for a physical action production requires:
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
 *
 *  3. (companion-identity, tasks.md 9.3) A boot-time seed of the enrolled-
 *     user cache with user 1 as an enrolled ADMIN. Registration now binds
 *     the credential to the authorizing admin's user id and LOGIN_VERIFY
 *     resolves authority live from the enrolled-user cache - a synthetic
 *     match alone no longer implies an enrolled admin, so the fixture must
 *     stand one in, exactly as a physically-enrolled device would have.
 *
 *  4. (companion-identity, tasks.md 9.3) A one-shot DEMOTION of that same
 *     user, fired when the SECOND Web Registration Authorization is claimed.
 *     This lets the harness drive the full register -> login -> demote ->
 *     refused-access sequence deterministically: the second REGISTER is both
 *     the password-reset path (its credential replaces the first in place)
 *     and, via this hook, the stand-in for the physical act nobody can
 *     perform under emulation - an admin demotion (CLI/Zigbee both need
 *     hardware or a coordinator). No production source is modified.
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
static void ble_ota_gate_seed_enrolled_admin(void) {
  sdf_services_state_t *s = sdf_services_state();
  if (s == NULL || s->lock == NULL ||
      xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
          pdTRUE) {
    ESP_LOGW(TAG, "Failed to seed enrolled-admin cache (lock)");
    return;
  }
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 1);
  sdf_services_perm_set(s->enrolled_perm_packed, 1, 3);
  xSemaphoreGive(s->lock);

  esp_err_t err = sdf_storage_enrolled_users_save(s->enrolled_user_bmp,
                                                  s->enrolled_perm_packed);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to persist seeded enrolled-admin cache: %s",
             esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Seeded enrolled-admin cache: user_id=1 permission=3");
  }
}

/* Point 4: demote the bound admin in place - exactly what a CLI/Zigbee
 * permission change writes into the same authoritative cache. Live session
 * authority (companion-identity) re-reads this on every restricted access,
 * so the demotion takes effect on the still-open harness connection without
 * touching its account record. */
static void ble_ota_gate_demote_enrolled_admin(void) {
  sdf_services_state_t *s = sdf_services_state();
  if (s == NULL || s->lock == NULL ||
      xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
          pdTRUE) {
    ESP_LOGW(TAG, "Failed to demote enrolled admin (lock)");
    return;
  }
  sdf_services_perm_set(s->enrolled_perm_packed, 1, 1);
  xSemaphoreGive(s->lock);

  /* Best-effort persistence; the in-RAM cache is what gates authority. */
  sdf_storage_enrolled_users_save(s->enrolled_user_bmp, s->enrolled_perm_packed);
  ESP_LOGW(TAG, "Demoted enrolled admin: user_id=1 permission=3 -> 1");
}

static int s_web_reg_approvals = 0;

static void ble_ota_gate_admin_approve_task(void *arg) {
  (void)arg;
  const sdf_fingerprint_match_t admin_match = {
      .user_id = 1,
      .permission = 3,
  };
  while (true) {
    /* Peek which action is about to be claimed so Web Registration
     * approvals can be counted for the identity-scenario demotion hook.
     * Single-claimer fixture: the peek-then-claim window is harmless. */
    bool web_reg_pending = false;
    sdf_services_state_t *s = sdf_services_state();
    if (s != NULL && s->lock != NULL &&
        xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) ==
            pdTRUE) {
      web_reg_pending =
          (s->pending_admin_action == SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH);
      xSemaphoreGive(s->lock);
    }

    if (sdf_services_try_claim_admin_action(&admin_match)) {
      ESP_LOGI(TAG, "Auto-approved a pending admin-fingerprint-gated action");
      if (web_reg_pending) {
        s_web_reg_approvals++;
        ESP_LOGI(TAG, "Web Registration Authorization approval #%d",
                 s_web_reg_approvals);
        if (s_web_reg_approvals == 2) {
          ble_ota_gate_demote_enrolled_admin();
        }
      }
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

  ble_ota_gate_seed_enrolled_admin();

  xTaskCreate(ble_ota_gate_admin_approve_task, "ble_ota_gate_admin_approve",
              BLE_OTA_GATE_ADMIN_APPROVE_TASK_STACK, NULL,
              BLE_OTA_GATE_ADMIN_APPROVE_TASK_PRIORITY, NULL);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

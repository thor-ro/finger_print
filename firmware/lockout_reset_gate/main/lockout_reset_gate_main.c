#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "sdf_app.h"
#include "sdf_config.h"
#include "sdf_event_router.h"
#include "sdf_services_internal.h"
#include "sdf_storage.h"

/* persist-biometric-lockout, tasks.md 7.3 - the one claim in that change no
 * unit test can make: that a lockout armed before a device reset is still in
 * force after it, on a real chip target, through the real
 * sdf_services_init().
 *
 * The fixture runs two boots of the same image, told apart by a phase key in
 * its own NVS namespace (the emulator starts every run from a blank NVS, so
 * the absent key *is* "first boot"):
 *
 *   Boot 1  Arm the lockout and esp_restart(). Boots 1 and 2 are separated by
 *           a genuine CPU reset with the emulated flash carrying the record
 *           across - which is the whole scenario, and is exactly what neither
 *           the host nor the on-chip Unity suite can produce.
 *   Boot 2  Bring the app up normally and check that the restore took: the
 *           deadline is a full CONFIG_SDF_SECURITY_BIOMETRIC_LOCKOUT_MS from
 *           this boot (not a remainder, and not zero), a match cycle refuses
 *           to reach the sensor, and the restored lockout announces itself as
 *           a CRITICAL SECURITY_LOCKOUT.
 *
 * Two fixture-only stand-ins, both forced by the emulator having no
 * fingerprint sensor, and both following the precedent set by
 * ble_ota_gate_main.c's synthetic ADMIN match:
 *
 *  1. Boot 1 arms the lockout by calling sdf_storage_lockout_save(true) - the
 *     exact call sdf_match_task_run_match_cycle() makes at the entry
 *     transition - instead of feeding the sensor five non-matching fingers.
 *     Under emulation fp_match_1n() can only ever time out, and a timeout is
 *     deliberately not a failed attempt, so the threshold is unreachable
 *     here. What boot 1 produces is therefore the same NVS record production
 *     writes; boot 2, the part actually under test, is untouched by this.
 *  2. Boot 2 seeds one enrolled user into the cache before driving a match
 *     cycle. The cycle returns immediately when the enrolled set is empty, so
 *     without a seeded user it could never reach either the refusal or the
 *     announcement, and an emulated device has no way to enrol anyone.
 *
 * No production source is modified for either. The one production-side change
 * this fixture does need is unrelated to them: sdf_event_router_capacity.h
 * gains a SDF_EVENT_ROUTER_SUBS_GATE_FIXTURE bucket for the observer below,
 * which is 0 in every build that does not define it (see CMakeLists.txt).
 */

static const char *TAG = "lockout_reset_gate";

#define GATE_PHASE_NAMESPACE "lo_gate"
#define GATE_PHASE_KEY "phase"

/* Pinned to sdkconfig.defaults rather than read back from the running config,
 * so a config drift shows up as a gate failure instead of being absorbed by
 * the assertion. */
#define GATE_EXPECTED_LOCKOUT_MS CONFIG_SDF_SECURITY_BIOMETRIC_LOCKOUT_MS

/* The restore computes its deadline from esp_timer_get_time() inside
 * sdf_services_init(); this file samples it around that call, so the two
 * differ by however long the rest of init takes. Generous enough to absorb
 * that under emulation, tight enough that a "remainder" or a zero deadline
 * could not pass. */
#define GATE_DEADLINE_TOLERANCE_US (30LL * 1000LL * 1000LL)

static int s_lockout_events = 0;
static sdf_event_router_priority_t s_lockout_event_prio =
    SDF_EVENT_ROUTER_PRIO_NORMAL;

static void gate_lockout_event_cb(void *ctx,
                                  const sdf_event_router_event_t *event) {
  (void)ctx;
  s_lockout_events++;
  s_lockout_event_prio = event->priority;
  ESP_LOGI(TAG, "Observed SECURITY_LOCKOUT event #%d priority=%d",
           s_lockout_events, (int)event->priority);
}

static esp_err_t gate_phase_read(uint8_t *phase_out) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(GATE_PHASE_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return err;
  }
  err = nvs_get_u8(handle, GATE_PHASE_KEY, phase_out);
  nvs_close(handle);
  return err;
}

static esp_err_t gate_phase_write(uint8_t phase) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(GATE_PHASE_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  err = nvs_set_u8(handle, GATE_PHASE_KEY, phase);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

static void gate_report(const char *status, const char *detail) {
  ESP_LOGI(TAG, "LOCKOUT_RESET_GATE_RESULT status=%s detail=%s", status,
           detail);
}

/* Stand-in #2: one enrolled user, so the match cycle gets past its
 * empty-enrolled-set early return. */
static void gate_seed_enrolled_user(void) {
  sdf_services_state_t *s = sdf_services_state();
  if (s == NULL || s->lock == NULL ||
      xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
          pdTRUE) {
    ESP_LOGW(TAG, "Failed to seed enrolled user (lock)");
    return;
  }
  SDF_SERVICES_BMP_SET(s->enrolled_user_bmp, 1);
  sdf_services_perm_set(s->enrolled_perm_packed, 1, 3);
  xSemaphoreGive(s->lock);
  ESP_LOGI(TAG, "Seeded enrolled user: user_id=1 permission=3");
}

static void gate_boot_1(void) {
  ESP_LOGI(TAG, "BOOT 1: arming the lockout, then resetting the device");

  /* Stand-in #1: the entry-transition write, without the five failed scans
   * an emulated device cannot produce. */
  esp_err_t err = sdf_storage_lockout_save(true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to arm the lockout record: %s", esp_err_to_name(err));
    gate_report("FAIL", "arm_write_failed");
    return;
  }

  bool armed = false;
  err = sdf_storage_lockout_load(&armed);
  if (err != ESP_OK || !armed) {
    ESP_LOGE(TAG, "Lockout record did not read back as armed: %s armed=%d",
             esp_err_to_name(err), (int)armed);
    gate_report("FAIL", "arm_readback_failed");
    return;
  }

  err = gate_phase_write(2);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to record the gate phase: %s", esp_err_to_name(err));
    gate_report("FAIL", "phase_write_failed");
    return;
  }

  ESP_LOGI(TAG, "BOOT 1: lockout armed and persisted; resetting now");
  vTaskDelay(pdMS_TO_TICKS(200));
  esp_restart();
}

static void gate_boot_2(void) {
  ESP_LOGI(TAG, "BOOT 2: verifying the lockout survived the reset");

  /* Subscribe before sdf_app_init(), which is what eventually calls
   * sdf_event_router_start() and freezes the subscriber table. Both inits
   * below are idempotent, so sdf_app_init() still runs its own full sequence
   * (sdf_storage_init -> sdf_config_init -> sdf_event_router_init ->
   * sdf_services_init -> sdf_event_router_start) and simply finds these two
   * already done. sdf_config_init() has to come first: the router sizes its
   * queue from sdf_config_get()->event_router_queue_depth, which is 0 until
   * the config is loaded, and xQueueCreate(0, ...) asserts. */
  esp_err_t cfg_err = sdf_config_init();
  if (cfg_err != ESP_OK) {
    ESP_LOGE(TAG, "sdf_config_init() failed: %s", esp_err_to_name(cfg_err));
    gate_report("FAIL", "config_init_failed");
    return;
  }
  esp_err_t router_err = sdf_event_router_init();
  if (router_err != ESP_OK) {
    ESP_LOGE(TAG, "sdf_event_router_init() failed: %s",
             esp_err_to_name(router_err));
    gate_report("FAIL", "event_router_init_failed");
    return;
  }
  esp_err_t err = sdf_event_router_subscribe(SDF_EVENT_ROUTER_SECURITY_LOCKOUT,
                                             SDF_EVENT_ROUTER_PRIO_NORMAL,
                                             gate_lockout_event_cb, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to SECURITY_LOCKOUT: %s",
             esp_err_to_name(err));
    gate_report("FAIL", "subscribe_failed");
    return;
  }

  const int64_t before_init_us = esp_timer_get_time();
  err = sdf_app_init();
  if (err != ESP_OK) {
    /* Same degraded-boot policy as firmware/main/main.c: the fingerprint
     * sensor and the Zigbee radio are both absent under emulation, and
     * neither is what this gate is about. */
    ESP_LOGW(TAG,
             "sdf_app_init() reported %s; continuing (subsystems are expected "
             "to be missing under emulation)",
             esp_err_to_name(err));
  }
  const int64_t after_init_us = esp_timer_get_time();

  sdf_services_state_t *s = sdf_services_state();
  if (s == NULL || s->lock == NULL) {
    gate_report("FAIL", "services_not_initialized");
    return;
  }

  int64_t deadline_us = 0;
  bool persist_armed = false;
  bool announce_pending = false;
  int64_t cooldown_us = -1;
  if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    gate_report("FAIL", "state_lock_timeout");
    return;
  }
  deadline_us = s->lockout_until_us;
  persist_armed = s->lockout_persist_armed;
  announce_pending = s->lockout_restore_announce_pending;
  xSemaphoreGive(s->lock);

  ESP_LOGI(TAG,
           "BOOT 2: lockout_until_us=%" PRId64 " persist_armed=%d "
           "announce_pending=%d (init spanned %" PRId64 "..%" PRId64 ")",
           deadline_us, (int)persist_armed, (int)announce_pending,
           before_init_us, after_init_us);

  if (deadline_us <= 0) {
    ESP_LOGE(TAG, "Lockout was NOT restored - the reset cleared it");
    gate_report("FAIL", "lockout_not_restored");
    return;
  }
  if (!persist_armed) {
    gate_report("FAIL", "persist_flag_not_set");
    return;
  }

  /* A full fresh duration from this boot, not a remainder of the previous
   * one - there is no remainder to compute across a power loss (D1/D2). */
  const int64_t expected_us =
      before_init_us + ((int64_t)GATE_EXPECTED_LOCKOUT_MS * 1000LL);
  const int64_t skew_us = deadline_us - expected_us;
  if (skew_us < 0 || skew_us > GATE_DEADLINE_TOLERANCE_US) {
    ESP_LOGE(TAG,
             "Restored deadline is not a full %d ms from boot: skew=%" PRId64
             " us",
             GATE_EXPECTED_LOCKOUT_MS, skew_us);
    gate_report("FAIL", "deadline_not_full_duration");
    return;
  }

  gate_seed_enrolled_user();

  /* One match cycle on this task: it must refuse to reach the sensor, and it
   * must announce the restored lockout. match_cooldown_until_us is the
   * refusal evidence - the cycle only arms it after a scan has actually been
   * attempted, so a still-zero cooldown means no scan went out. */
  sdf_match_task_run_cycle_for_test();
  vTaskDelay(pdMS_TO_TICKS(500));

  if (xSemaphoreTake(s->lock, pdMS_TO_TICKS(SDF_SERVICES_LOCK_WAIT_MS)) !=
      pdTRUE) {
    gate_report("FAIL", "state_lock_timeout_after_cycle");
    return;
  }
  cooldown_us = s->match_cooldown_until_us;
  announce_pending = s->lockout_restore_announce_pending;
  xSemaphoreGive(s->lock);

  if (cooldown_us != 0) {
    ESP_LOGE(TAG,
             "A scan was attempted despite the restored lockout "
             "(match_cooldown_until_us=%" PRId64 ")",
             cooldown_us);
    gate_report("FAIL", "matching_not_refused");
    return;
  }

  if (s_lockout_events != 1) {
    ESP_LOGE(TAG, "Expected exactly one SECURITY_LOCKOUT event, saw %d",
             s_lockout_events);
    gate_report("FAIL", "lockout_event_not_emitted");
    return;
  }
  if (s_lockout_event_prio != SDF_EVENT_ROUTER_PRIO_CRITICAL) {
    ESP_LOGE(TAG, "SECURITY_LOCKOUT was emitted at priority %d, not CRITICAL",
             (int)s_lockout_event_prio);
    gate_report("FAIL", "lockout_event_not_critical");
    return;
  }
  if (announce_pending) {
    gate_report("FAIL", "announce_flag_not_consumed");
    return;
  }

  ESP_LOGI(TAG,
           "BOOT 2: matching refused, lockout re-armed for %d ms from boot, "
           "CRITICAL SECURITY_LOCKOUT announced once",
           GATE_EXPECTED_LOCKOUT_MS);
  gate_report("PASS", "restored=1 refused=1 announced=CRITICAL");
}

void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    gate_report("FAIL", "nvs_init_failed");
    goto idle;
  }

  uint8_t phase = 1;
  esp_err_t phase_err = gate_phase_read(&phase);
  if (phase_err != ESP_OK) {
    /* No phase key: a blank NVS, i.e. the first boot of this run. */
    phase = 1;
  }

  if (phase == 1) {
    gate_boot_1();
  } else {
    gate_boot_2();
  }

idle:
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

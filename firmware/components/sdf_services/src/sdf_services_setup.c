/* Setup-phase lifecycle: arming, the two exposure timers (arm window and
 * setup deadline), the connection idle timer, the timeout wipe, and the
 * button reclaim gesture. See the device-setup-phase capability
 * (openspec/changes/app-guided-first-time-setup).
 *
 * The state machine itself is pure with respect to time: every entry point
 * either takes an explicit `now_us` or reads esp_timer_get_time() once and
 * funnels into the same `_at(now_us)` core, so host tests can drive the
 * clocks deterministically.
 *
 * Timer semantics (device-setup-phase spec):
 * - The arm window runs from arming and bounds open advertising before any
 *   client has ever connected. Once the deadline starts it stops governing
 *   entirely ("the arm window no longer governs the setup phase"), so a
 *   disconnect mid-wizard leaves the deadline as the only bound rather than
 *   silently granting a fresh open-air window.
 * - The setup deadline starts at the first accepted connection and is never
 *   extended by activity, progress, disconnection or reconnection. Only the
 *   physical button press restarts it - the single documented exception to
 *   "not extendable", and it requires physical presence.
 * - The idle timer drops a silent connection only; the phase stays armed,
 *   the deadline keeps running, no state is erased.
 *
 * Worst-case open air per arm is therefore ARM_WINDOW + DEADLINE (15 min at
 * the defaults), reached only when a client connects in the last instant of
 * the arm window. */
#include "sdf_services_internal.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "sdf_drivers.h"
#include "sdf_event_router.h"
#include "sdf_storage.h"

static const char *TAG = "sdf_services_setup";

typedef struct {
  bool armed;
  int64_t armed_at_us;        /* start of the current open-air window */
  bool deadline_started;
  int64_t deadline_start_us;
  bool conn_active;
  uint16_t conn_handle;
  int64_t last_activity_us;   /* start of the current idle window */
} sdf_setup_phase_state_t;

static sdf_setup_phase_state_t s_setup = {
    .armed = false,
    .armed_at_us = 0,
    .deadline_started = false,
    .deadline_start_us = 0,
    .conn_active = false,
    .conn_handle = 0,
    .last_activity_us = 0,
};

void sdf_services_setup_phase_reset_for_test(void) {
  s_setup = (sdf_setup_phase_state_t){0};
}

static void sdf_setup_emit(uint8_t action, uint16_t conn_handle) {
  sdf_event_router_event_t evt = {
      .type = SDF_EVENT_ROUTER_SETUP_PHASE,
      .priority = SDF_EVENT_ROUTER_PRIO_HIGH,
      .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
      .payload.setup_phase = {.action = action, .conn_handle = conn_handle},
  };
  /* A dropped emit (full queue) only delays the BLE-side half of the action;
   * the device-side wipe/disarm below has already run. */
  sdf_event_router_emit(&evt, SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS);
}

bool sdf_services_setup_phase_is_armed(void) { return s_setup.armed; }

bool sdf_services_setup_phase_owns_buttons(void) {
  if (s_setup.armed) {
    return true;
  }
  /* Disarmed but still unclaimed: presses re-arm rather than dispatch admin
   * actions, so the gesture must keep routing here after a timeout lapse.
   * A latch read failure is treated as unset - matching boot_arm()'s
   * fail-open treatment of a transient NVS error. */
  bool complete = false;
  sdf_storage_setup_complete_load(&complete);
  return !complete;
}

/* Time-injected core of arm(); also the button-reclaim timer restart.
 * Non-static so host tests can drive the clock deterministically. */
void sdf_services_setup_phase_arm_at(int64_t now_us) {
  s_setup.armed = true;
  s_setup.armed_at_us = now_us;
  if (s_setup.deadline_started) {
    /* Button-press restart: the one way either timer extends. */
    s_setup.deadline_start_us = now_us;
  }
}

void sdf_services_setup_phase_arm(void) {
  sdf_services_setup_phase_arm_at(esp_timer_get_time());
  ESP_LOGI(TAG, "Setup phase armed");
}

void sdf_services_setup_phase_disarm(void) {
  s_setup.armed = false;
  s_setup.conn_active = false;
  ESP_LOGI(TAG, "Setup phase disarmed");
}

/* Boot arming: called from sdf_services_init() once NVS is up. Arms when the
 * latch is unset - a brand-new device, or the reboot following a factory
 * reset (which cleared the latch). A completed device never enters the
 * setup phase. */
void sdf_services_setup_phase_boot_arm(void) {
  bool complete = false;
  esp_err_t err = sdf_storage_setup_complete_load(&complete);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "setup latch load failed (%s); treating as unset",
             esp_err_to_name(err));
  }
  if (!complete) {
    sdf_services_setup_phase_arm_at(esp_timer_get_time());
    ESP_LOGI(TAG, "Setup phase armed at boot (latch unset)");
  }
}

/* Internal time-injected cores - declared in sdf_services_internal.h so host
 * tests can drive the clocks deterministically. */
void sdf_services_setup_phase_notify_connected_at(uint16_t conn_handle,
                                                  int64_t now_us) {
  if (!s_setup.conn_active) {
    s_setup.conn_active = true;
    s_setup.conn_handle = conn_handle;
    s_setup.last_activity_us = now_us;
    if (!s_setup.deadline_started) {
      s_setup.deadline_started = true;
      s_setup.deadline_start_us = now_us;
      ESP_LOGI(TAG, "Setup deadline started at first accepted connection");
    }
    /* armed_at_us is deliberately left alone: once deadline_started is set
     * the arm window stops governing (see poll()), so resetting it here
     * would only obscure when the phase was armed. */
  }
}

void sdf_services_setup_phase_notify_disconnected_at(int64_t now_us) {
  (void)now_us;
  if (s_setup.conn_active) {
    /* Advertising resumes openly, bounded by the still-running deadline.
     * The arm window is NOT restarted: disconnecting must not buy a fresh
     * open-air window, which is what "the deadline is never extended by
     * disconnection or reconnection" means in practice. */
    s_setup.conn_active = false;
  }
}

void sdf_services_setup_phase_notify_gatt_activity_at(int64_t now_us) {
  if (s_setup.conn_active) {
    s_setup.last_activity_us = now_us;
  }
}

void sdf_services_setup_phase_notify_connected(uint16_t conn_handle) {
  sdf_services_setup_phase_notify_connected_at(conn_handle,
                                               esp_timer_get_time());
}

void sdf_services_setup_phase_notify_disconnected(uint16_t conn_handle) {
  (void)conn_handle;
  sdf_services_setup_phase_notify_disconnected_at(esp_timer_get_time());
}

void sdf_services_setup_phase_notify_gatt_activity(void) {
  sdf_services_setup_phase_notify_gatt_activity_at(esp_timer_get_time());
}

sdf_services_setup_poll_result_t sdf_services_setup_phase_poll(int64_t now_us) {
  if (!s_setup.armed) {
    return SDF_SERVICES_SETUP_POLL_NONE;
  }

  if (s_setup.deadline_started &&
      now_us - s_setup.deadline_start_us >= (int64_t)SDF_SETUP_DEADLINE_MS * 1000LL) {
    return SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP;
  }

  if (s_setup.conn_active &&
      now_us - s_setup.last_activity_us >= (int64_t)SDF_SETUP_CONN_IDLE_MS * 1000LL) {
    return SDF_SERVICES_SETUP_POLL_DROP_IDLE_CONN;
  }

  /* The arm window governs only until the first connection starts the
   * deadline; after that the deadline checked above is the sole bound, so a
   * disconnect or idle drop cannot hand out a fresh open-air window. */
  if (!s_setup.deadline_started &&
      now_us - s_setup.armed_at_us >= (int64_t)SDF_SETUP_ARM_WINDOW_MS * 1000LL) {
    return SDF_SERVICES_SETUP_POLL_WIPE_AND_STOP;
  }
  return SDF_SERVICES_SETUP_POLL_NONE;
}

void sdf_services_setup_phase_idle_drop(void) {
  uint16_t conn_handle = s_setup.conn_handle;
  /* Advertising resumes openly, bounded by the still-running deadline; the
   * arm window is not restarted (see notify_disconnected_at). */
  s_setup.conn_active = false;
  ESP_LOGI(TAG, "Setup connection idle timeout - dropping conn_handle=%u",
           (unsigned)conn_handle);
  sdf_setup_emit(SDF_EVENT_ROUTER_SETUP_PHASE_ACTION_IDLE_DROP, conn_handle);
}

void sdf_services_setup_phase_timeout_wipe(void) {
  ESP_LOGW(TAG, "Setup phase expired - wiping partial setup state");

  sdf_fingerprint_op_result_t fp_result = fp_delete_all_users();
  if (fp_result != SDF_FINGERPRINT_OP_OK) {
    /* Non-fatal by requirement: a sensor fault must not leave the device
     * advertising openly forever. */
    ESP_LOGE(TAG, "Template erase failed during timeout wipe: %d",
             (int)fp_result);
  }

  esp_err_t err = sdf_storage_web_user_clear_all();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Web account erase failed during timeout wipe: %s",
             esp_err_to_name(err));
  }

  err = sdf_storage_admission_clear_all();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Admission record erase failed during timeout wipe: %s",
             esp_err_to_name(err));
  }

  err = sdf_storage_nuki_clear();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Nuki credential erase failed during timeout wipe: %s",
             esp_err_to_name(err));
  }

  sdf_services_reset_enrolled_user_cache();

  sdf_services_setup_phase_disarm();

  /* BLE-side half: clear persisted bonds, terminate the setup connection,
   * stop advertising. */
  sdf_setup_emit(SDF_EVENT_ROUTER_SETUP_PHASE_ACTION_TIMEOUT, 0);
}

void sdf_services_setup_phase_button_reclaim(void) {
  ESP_LOGI(TAG, "Setup-phase button press: reclaim and re-arm");
  sdf_services_setup_phase_arm_at(esp_timer_get_time());
  /* Terminate the current setup connection, if any, and resume unfiltered
   * advertising. No pending admin action is set. */
  sdf_setup_emit(SDF_EVENT_ROUTER_SETUP_PHASE_ACTION_RECLAIM, s_setup.conn_handle);
}

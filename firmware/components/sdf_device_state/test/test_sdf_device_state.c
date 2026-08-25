#include "unity.h"

#include <string.h>

#include "sdf_config.h"
#include "sdf_device_state.h"

static char s_report[512];

void setUp(void) {
  sdf_device_state_reset();
  memset(s_report, 0, sizeof(s_report));
}

void tearDown(void) {}

/* --- Fresh boot: nothing reported -> unknown everywhere ---------------- */

void test_fresh_boot_reports_unknown_everywhere(void) {
  sdf_config_get_mutable()->zigbee_enabled = true;

  sdf_device_state_snapshot_t snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_UNKNOWN, snap.lock.condition);
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_UNKNOWN, snap.battery.condition);
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_UNKNOWN,
                    snap.fingerprint.condition);
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_UNKNOWN, snap.nuki.condition);
  /* Zigbee is enabled by configuration here but has not reported a join. */
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_UNKNOWN, snap.zigbee.condition);

  size_t n = sdf_device_state_format_health_report(s_report, sizeof(s_report),
                                                   1000u);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"lock\":{\"state\":\"unknown\"}"));
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"battery\":{\"state\":\"unknown\"}"));
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"fingerprint\":{\"state\":\"unknown\"}"));
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"nuki\":{\"state\":\"unknown\"}"));
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"zigbee\":{\"state\":\"unknown\"}"));
}

/* --- Lock provenance and age ------------------------------------------- */

void test_assumed_lock_state_replaced_by_confirmation(void) {
  sdf_device_state_record_lock(SDF_DEVICE_STATE_LOCK_LOCKED,
                               SDF_DEVICE_STATE_LOCK_SOURCE_ASSUMED, 1000u);
  sdf_device_state_snapshot_t snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_MEASURED, snap.lock.condition);
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_LOCK_LOCKED, snap.lock.state);
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_LOCK_SOURCE_ASSUMED, snap.lock.source);

  /* A keyturner report (confirmation) overwrites the assumption. */
  sdf_device_state_record_lock(SDF_DEVICE_STATE_LOCK_UNLOCKED,
                               SDF_DEVICE_STATE_LOCK_SOURCE_REPORTED, 1100u);
  snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_MEASURED, snap.lock.condition);
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_LOCK_UNLOCKED, snap.lock.state);
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_LOCK_SOURCE_REPORTED, snap.lock.source);

  size_t n = sdf_device_state_format_health_report(s_report, sizeof(s_report),
                                                   1200u);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(
      s_report, "\"lock\":{\"state\":\"unlocked\",\"source\":\"reported\","));
}

/* --- Cache records with the reporting transport disabled --------------- */

void test_cache_records_with_zigbee_disabled(void) {
  sdf_config_get_mutable()->zigbee_enabled = false;
  sdf_device_state_record_lock(SDF_DEVICE_STATE_LOCK_LOCKED,
                               SDF_DEVICE_STATE_LOCK_SOURCE_REPORTED, 500u);
  sdf_device_state_snapshot_t snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_MEASURED, snap.lock.condition);
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_LOCK_LOCKED, snap.lock.state);

  /* Zigbee absent by configuration is distinct from unknown. */
  size_t n = sdf_device_state_format_health_report(s_report, sizeof(s_report),
                                                   600u);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(
      strstr(s_report, "\"zigbee\":{\"state\":\"not_applicable\"}"));
}

/* --- Ages advance ------------------------------------------------------- */

void test_ages_advance(void) {
  sdf_device_state_record_battery(63, 5000u);
  sdf_device_state_record_lock(SDF_DEVICE_STATE_LOCK_LOCKED,
                               SDF_DEVICE_STATE_LOCK_SOURCE_REPORTED, 5000u);

  size_t n1 = sdf_device_state_format_health_report(s_report, sizeof(s_report),
                                                    6000u);
  TEST_ASSERT_TRUE(n1 > 0);
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"age_ms\":1000"));

  size_t n2 = sdf_device_state_format_health_report(s_report, sizeof(s_report),
                                                    9000u);
  TEST_ASSERT_TRUE(n2 > 0);
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"age_ms\":4000"));
}

/* --- Battery unavailability --------------------------------------------- */

void test_unavailable_battery_clears_previous_reading_and_is_never_100(void) {
  sdf_device_state_record_battery(63, 1000u);
  sdf_device_state_snapshot_t snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_MEASURED, snap.battery.condition);

  sdf_device_state_record_battery(SDF_DEVICE_STATE_BATTERY_UNAVAILABLE,
                                  2000u);
  snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_UNKNOWN, snap.battery.condition);

  size_t n = sdf_device_state_format_health_report(s_report, sizeof(s_report),
                                                   2100u);
  TEST_ASSERT_TRUE(n > 0);
  /* Unknown battery carries no number at all - in particular never 100. */
  TEST_ASSERT_NULL(strstr(s_report, "\"battery\":{\"percent\":"));
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"battery\":{\"state\":\"unknown\"}"));
}

void test_low_battery_warning_gates_on_measured_reading(void) {
  /* Unavailable measurement: never low. */
  TEST_ASSERT_FALSE(sdf_device_state_battery_is_low(-1));
  TEST_ASSERT_FALSE(sdf_device_state_battery_is_low(-100));
  /* Above threshold: not low. */
  TEST_ASSERT_FALSE(sdf_device_state_battery_is_low(21));
  TEST_ASSERT_FALSE(sdf_device_state_battery_is_low(100));
  /* At and below the threshold: low. */
  TEST_ASSERT_TRUE(sdf_device_state_battery_is_low(20));
  TEST_ASSERT_TRUE(sdf_device_state_battery_is_low(0));
}

/* --- Fingerprint / Nuki / alarm conditions ------------------------------ */

void test_fingerprint_readiness_three_conditions(void) {
  sdf_device_state_snapshot_t snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_UNKNOWN,
                    snap.fingerprint.condition);

  sdf_device_state_record_fingerprint(false, 100u);
  snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_MEASURED,
                    snap.fingerprint.condition);
  TEST_ASSERT_FALSE(snap.fingerprint.ready);

  sdf_device_state_record_fingerprint(true, 200u);
  snap = sdf_device_state_snapshot();
  TEST_ASSERT_TRUE(snap.fingerprint.ready);
}

void test_nuki_link_measured_and_unknown(void) {
  sdf_device_state_record_nuki_link(true, false, 300u);
  sdf_device_state_snapshot_t snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_MEASURED, snap.nuki.condition);
  TEST_ASSERT_TRUE(snap.nuki.paired);
  TEST_ASSERT_FALSE(snap.nuki.connected);

  size_t n = sdf_device_state_format_health_report(s_report, sizeof(s_report),
                                                   400u);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(
      strstr(s_report, "\"nuki\":{\"paired\":true,\"connected\":false,"));
}

void test_alarm_mask_recorded(void) {
  sdf_device_state_record_alarm_mask(0x0006u);
  sdf_device_state_snapshot_t snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL_UINT16(0x0006u, snap.alarm_mask);

  size_t n = sdf_device_state_format_health_report(s_report, sizeof(s_report),
                                                   100u);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"alarms\":{\"mask\":6}"));
}

/* --- Report sourced from owning components; no I/O ---------------------- */

void test_serializer_sources_version_ota_setup_and_is_stable(void) {
  sdf_device_state_record_battery(50, 0u);

  size_t n1 = sdf_device_state_format_health_report(s_report, sizeof(s_report),
                                                    10u);
  TEST_ASSERT_TRUE(n1 > 0);
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"firmware\":\""));
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"ota\":\"idle\""));
  TEST_ASSERT_NOT_NULL(strstr(s_report, "\"setup\":\""));

  /* Repeated production performs no work beyond reading recorded state:
   * the output for an unchanged cache and clock is byte-identical, and
   * nothing in the path mutates the cache. */
  char again[sizeof(s_report)];
  size_t n2 =
      sdf_device_state_format_health_report(again, sizeof(again), 10u);
  TEST_ASSERT_EQUAL_size_t(n1, n2);
  TEST_ASSERT_EQUAL_MEMORY(s_report, again, n1);
}

void test_oversized_buffer_reports_does_not_fit(void) {
  char tiny[8];
  TEST_ASSERT_EQUAL_size_t(0, sdf_device_state_format_health_report(
                                  tiny, sizeof(tiny), 0u));
}

/* --- Change callback: fires on a real change, stays silent otherwise ----
 *
 * The Status characteristic coalesces on this callback, so a recorder that
 * reports a change it did not make notifies every subscriber with a
 * byte-identical report. */

static unsigned s_change_calls;

static void count_change(void *ctx) {
  (void)ctx;
  s_change_calls++;
}

static void arm_change_counter(void) {
  s_change_calls = 0;
  sdf_device_state_set_change_cb(count_change, NULL);
}

static void disarm_change_counter(void) {
  sdf_device_state_set_change_cb(NULL, NULL);
}

void test_change_cb_fires_once_per_actual_change(void) {
  arm_change_counter();

  sdf_device_state_record_battery(63, 1000u);
  TEST_ASSERT_EQUAL_UINT(1, s_change_calls);

  /* Same value again: nothing changed, nobody is told. */
  sdf_device_state_record_battery(63, 2000u);
  TEST_ASSERT_EQUAL_UINT(1, s_change_calls);

  sdf_device_state_record_battery(62, 3000u);
  TEST_ASSERT_EQUAL_UINT(2, s_change_calls);

  disarm_change_counter();
}

void test_repeated_unavailable_battery_notifies_once(void) {
  arm_change_counter();

  sdf_device_state_record_battery(63, 1000u);
  TEST_ASSERT_EQUAL_UINT(1, s_change_calls);

  /* First unavailable reading is a change: the report loses its number. */
  sdf_device_state_record_battery(SDF_DEVICE_STATE_BATTERY_UNAVAILABLE, 2000u);
  TEST_ASSERT_EQUAL_UINT(2, s_change_calls);

  /* Every subsequent failed measurement produces the same report and must
   * not notify again - the raw sentinel is not the stored value. */
  sdf_device_state_record_battery(SDF_DEVICE_STATE_BATTERY_UNAVAILABLE, 3000u);
  sdf_device_state_record_battery(SDF_DEVICE_STATE_BATTERY_UNAVAILABLE, 4000u);
  TEST_ASSERT_EQUAL_UINT(2, s_change_calls);

  disarm_change_counter();
}

void test_change_cb_covers_every_recorder(void) {
  arm_change_counter();

  sdf_device_state_record_lock(SDF_DEVICE_STATE_LOCK_LOCKED,
                               SDF_DEVICE_STATE_LOCK_SOURCE_REPORTED, 100u);
  TEST_ASSERT_EQUAL_UINT(1, s_change_calls);
  /* Provenance is part of the value: the same state newly only assumed is
   * a different report. */
  sdf_device_state_record_lock(SDF_DEVICE_STATE_LOCK_LOCKED,
                               SDF_DEVICE_STATE_LOCK_SOURCE_REPORTED, 200u);
  TEST_ASSERT_EQUAL_UINT(1, s_change_calls);

  sdf_device_state_record_fingerprint(true, 300u);
  TEST_ASSERT_EQUAL_UINT(2, s_change_calls);
  sdf_device_state_record_fingerprint(true, 400u);
  TEST_ASSERT_EQUAL_UINT(2, s_change_calls);

  sdf_device_state_record_nuki_link(true, true, 500u);
  TEST_ASSERT_EQUAL_UINT(3, s_change_calls);
  sdf_device_state_record_nuki_link(true, true, 600u);
  TEST_ASSERT_EQUAL_UINT(3, s_change_calls);

  sdf_device_state_record_alarm_mask(0x0002u);
  TEST_ASSERT_EQUAL_UINT(4, s_change_calls);
  sdf_device_state_record_alarm_mask(0x0002u);
  TEST_ASSERT_EQUAL_UINT(4, s_change_calls);

  disarm_change_counter();
}

void test_no_change_cb_registered_is_safe(void) {
  sdf_device_state_set_change_cb(NULL, NULL);
  sdf_device_state_record_battery(41, 1000u);
  sdf_device_state_snapshot_t snap = sdf_device_state_snapshot();
  TEST_ASSERT_EQUAL(SDF_DEVICE_STATE_CONDITION_MEASURED, snap.battery.condition);
}

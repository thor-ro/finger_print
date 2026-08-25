#ifndef SDF_DEVICE_STATE_H
#define SDF_DEVICE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transport-independent last-known device state cache and health-report
 * producer (companion-device-health).
 *
 * One place holds the last-known values every reporting transport reads, so
 * two transports cannot report different numbers and disabling a transport
 * does not stop the state being recorded.
 *
 * Three-valued vocabulary: every reported field is exactly one of
 *   measured        - the system holds a reading,
 *   unknown         - the system holds no reading (failed measurement or
 *                     nothing has reported since boot),
 *   not applicable  - the subsystem is absent by build or configuration.
 * No field is ever populated with a substitute for a value the system does
 * not have.
 *
 * Recording performs no sensor or bus I/O and never probes any subsystem on
 * behalf of a reader. Fingerprint readiness is published by the fingerprint
 * path when it performs I/O for its own reasons (via the sdf_services
 * readiness callback), never pulled by a reader.
 */

/* Battery driver contract (battery.h): SDF_BATTERY_UNAVAILABLE means no
 * reading. Kept here as well so the cache module does not have to include
 * the driver header to express unavailability. */
#define SDF_DEVICE_STATE_BATTERY_UNAVAILABLE (-1)

typedef enum {
  SDF_DEVICE_STATE_CONDITION_MEASURED = 0,
  SDF_DEVICE_STATE_CONDITION_UNKNOWN,
  SDF_DEVICE_STATE_CONDITION_NOT_APPLICABLE,
} sdf_device_state_condition_t;

typedef enum {
  SDF_DEVICE_STATE_LOCK_UNKNOWN = 0,
  SDF_DEVICE_STATE_LOCK_LOCKED,
  SDF_DEVICE_STATE_LOCK_UNLOCKED,
  SDF_DEVICE_STATE_LOCK_NOT_FULLY_LOCKED,
} sdf_device_state_lock_state_t;

/* Provenance of a lock state: REPORTED means the lock itself confirmed it
 * (keyturner report); ASSUMED means it was derived from a lock/unlock
 * command this system sent and the lock has not yet confirmed it. */
typedef enum {
  SDF_DEVICE_STATE_LOCK_SOURCE_NONE = 0,
  SDF_DEVICE_STATE_LOCK_SOURCE_REPORTED,
  SDF_DEVICE_STATE_LOCK_SOURCE_ASSUMED,
} sdf_device_state_lock_source_t;

typedef void (*sdf_device_state_change_cb)(void *ctx);

/* Clears every cached value back to "nothing recorded since boot". Called
 * from sdf_app_init(). */
void sdf_device_state_reset(void);

/* Registers the change callback, invoked after a recorded VALUE actually
 * changed (re-recording an identical value notifies nobody). The callback
 * runs in the recorder's context and must not block; the Status
 * characteristic uses it to coalesce and notify subscribers. */
void sdf_device_state_set_change_cb(sdf_device_state_change_cb cb, void *ctx);

/* --- Recorders (all cheap, non-blocking, no I/O) ----------------------- */

/* source REPORTED marks a lock-confirmed state; ASSUMED a command-derived
 * one. SDF_DEVICE_STATE_LOCK_UNKNOWN resets the field to unknown. */
void sdf_device_state_record_lock(sdf_device_state_lock_state_t state,
                                  sdf_device_state_lock_source_t source,
                                  uint32_t now_ms);

/* percent 0..100 records a measurement; a negative value (e.g.
 * SDF_BATTERY_UNAVAILABLE) records that no measurement is currently
 * available and clears any earlier reading. */
void sdf_device_state_record_battery(int percent, uint32_t now_ms);

/* readiness published by the fingerprint path after its own I/O. */
void sdf_device_state_record_fingerprint(bool ready, uint32_t now_ms);

void sdf_device_state_record_nuki_link(bool paired, bool connected,
                                       uint32_t now_ms);
void sdf_device_state_record_zigbee_joined(bool joined, uint32_t now_ms);
void sdf_device_state_record_alarm_mask(uint16_t mask);

/* --- Readers ------------------------------------------------------------ */

typedef struct {
  sdf_device_state_condition_t condition;
  sdf_device_state_lock_state_t state;
  sdf_device_state_lock_source_t source;
  uint32_t recorded_ms;
} sdf_device_state_lock_entry_t;

typedef struct {
  sdf_device_state_condition_t condition;
  int32_t percent;
  uint32_t recorded_ms;
} sdf_device_state_battery_entry_t;

typedef struct {
  sdf_device_state_condition_t condition;
  bool ready;
  uint32_t recorded_ms;
} sdf_device_state_fingerprint_entry_t;

typedef struct {
  sdf_device_state_condition_t condition;
  bool paired;
  bool connected;
  uint32_t recorded_ms;
} sdf_device_state_nuki_entry_t;

typedef struct {
  sdf_device_state_condition_t condition;
  bool joined;
  uint32_t recorded_ms;
} sdf_device_state_zigbee_entry_t;

typedef struct {
  sdf_device_state_lock_entry_t lock;
  sdf_device_state_battery_entry_t battery;
  sdf_device_state_fingerprint_entry_t fingerprint;
  sdf_device_state_nuki_entry_t nuki;
  sdf_device_state_zigbee_entry_t zigbee;
  uint16_t alarm_mask;
} sdf_device_state_snapshot_t;

sdf_device_state_snapshot_t sdf_device_state_snapshot(void);

/* Serializes the health report as JSON into buf (NUL-terminated). Returns
 * the length written excluding the terminator, or 0 if it did not fit.
 *
 * Fields sourced live so they cannot disagree with their other surfaces:
 * firmware version / OTA state from sdf_ota_get_version()/sdf_ota_get_state()
 * and setup state from sdf_services_get_setup_state() - the same sources the
 * SetupState characteristic and the OTA status use. Producing the report
 * performs no sensor or bus I/O. */
size_t sdf_device_state_format_health_report(char *buf, size_t cap,
                                             uint32_t now_ms);

/* The low-battery warning decision: true only for a MEASURED reading at or
 * below the low-battery threshold. An unavailable measurement (negative,
 * e.g. SDF_DEVICE_STATE_BATTERY_UNAVAILABLE) is never low - a failed
 * measurement must not be treated as either healthy or empty. */
#define SDF_DEVICE_STATE_BATTERY_LOW_PERCENT 20
bool sdf_device_state_battery_is_low(int percent);

#ifdef __cplusplus
}
#endif

#endif /* SDF_DEVICE_STATE_H */

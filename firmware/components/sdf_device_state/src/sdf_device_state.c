#include "sdf_device_state.h"

#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>

#include "sdf_ota.h"
#include "sdf_protocol_zigbee.h"
#include "sdf_services.h"

/* Every field is a small set of atomics, mirroring s_zigbee_alarm_mask in
 * sdf_app.c: recorders run on several tasks (app task, BLE receive context,
 * match/enroll tasks via the readiness callback), and a reader's snapshot may
 * straddle two recordings slightly - acceptable for an advisory report whose
 * every stale-able value carries its age. Recording performs no I/O and takes
 * no lock, so a reader cannot affect any subsystem by requesting the report.
 */

typedef struct {
  atomic_uint packed; /* state | (source << 8); 0 == unknown/no source */
  atomic_uint recorded_ms;
} lock_entry_t;

typedef struct {
  atomic_bool has;     /* a measurement is held */
  atomic_int percent;  /* 0..100 when has */
  atomic_uint recorded_ms;
} battery_entry_t;

typedef struct {
  atomic_uint packed; /* bit0 valid, bit1 ready */
  atomic_uint recorded_ms;
} flag_entry_t;

static lock_entry_t s_lock;
static battery_entry_t s_battery;
static flag_entry_t s_fingerprint;
static flag_entry_t s_nuki;               /* bit0 valid, bit1 paired, bit2 connected */
static flag_entry_t s_zigbee;             /* bit0 valid, bit1 joined */
static atomic_uint s_alarm_mask;

static sdf_device_state_change_cb s_change_cb;
static void *s_change_cb_ctx;

#define FP_VALID_BIT 0x1u
#define FP_READY_BIT 0x2u
#define NUKI_VALID_BIT 0x1u
#define NUKI_PAIRED_BIT 0x2u
#define NUKI_CONNECTED_BIT 0x4u
#define ZB_VALID_BIT 0x1u
#define ZB_JOINED_BIT 0x2u

void sdf_device_state_reset(void) {
  atomic_store(&s_lock.packed, 0u);
  atomic_store(&s_lock.recorded_ms, 0u);
  atomic_store(&s_battery.has, false);
  atomic_store(&s_battery.percent, 0);
  atomic_store(&s_battery.recorded_ms, 0u);
  atomic_store(&s_fingerprint.packed, 0u);
  atomic_store(&s_fingerprint.recorded_ms, 0u);
  atomic_store(&s_nuki.packed, 0u);
  atomic_store(&s_nuki.recorded_ms, 0u);
  atomic_store(&s_zigbee.packed, 0u);
  atomic_store(&s_zigbee.recorded_ms, 0u);
  atomic_store(&s_alarm_mask, 0u);
}

void sdf_device_state_set_change_cb(sdf_device_state_change_cb cb, void *ctx) {
  s_change_cb_ctx = ctx;
  s_change_cb = cb;
}

static void notify_changed(void) {
  if (s_change_cb != NULL) {
    s_change_cb(s_change_cb_ctx);
  }
}

void sdf_device_state_record_lock(sdf_device_state_lock_state_t state,
                                  sdf_device_state_lock_source_t source,
                                  uint32_t now_ms) {
  unsigned packed = (unsigned)state & 0xFFu;
  if (state == SDF_DEVICE_STATE_LOCK_UNKNOWN) {
    packed = 0u;
  } else {
    packed |= ((unsigned)source & 0xFFu) << 8;
    if (source == SDF_DEVICE_STATE_LOCK_SOURCE_NONE) {
      /* A concrete state with no provenance is not a recording we can
       * stand behind; keep it honest rather than guessing a source. */
      packed = 0u;
    }
  }

  unsigned old = atomic_exchange(&s_lock.packed, packed);
  atomic_store(&s_lock.recorded_ms, now_ms);
  if (old != packed) {
    notify_changed();
  }
}

void sdf_device_state_record_battery(int percent, uint32_t now_ms) {
  bool new_has = percent >= 0 && percent <= 100;
  /* Compare the stored form, not the raw argument: an unavailable reading
   * is stored as 0, so comparing it against the raw -1 reported a change
   * on every re-record of the same unavailability and notified subscribers
   * with a byte-identical report. */
  int new_percent = new_has ? percent : 0;
  bool old_has = atomic_load(&s_battery.has);
  int old_percent = atomic_load(&s_battery.percent);

  atomic_store(&s_battery.percent, new_percent);
  atomic_store(&s_battery.has, new_has);
  atomic_store(&s_battery.recorded_ms, now_ms);

  if (old_has != new_has || old_percent != new_percent) {
    notify_changed();
  }
}

void sdf_device_state_record_fingerprint(bool ready, uint32_t now_ms) {
  unsigned packed = FP_VALID_BIT | (ready ? FP_READY_BIT : 0u);
  unsigned old = atomic_exchange(&s_fingerprint.packed, packed);
  atomic_store(&s_fingerprint.recorded_ms, now_ms);
  if (old != packed) {
    notify_changed();
  }
}

void sdf_device_state_record_nuki_link(bool paired, bool connected,
                                       uint32_t now_ms) {
  unsigned packed =
      NUKI_VALID_BIT | (paired ? NUKI_PAIRED_BIT : 0u) |
      (connected ? NUKI_CONNECTED_BIT : 0u);
  unsigned old = atomic_exchange(&s_nuki.packed, packed);
  atomic_store(&s_nuki.recorded_ms, now_ms);
  if (old != packed) {
    notify_changed();
  }
}

void sdf_device_state_record_zigbee_joined(bool joined, uint32_t now_ms) {
  unsigned packed = ZB_VALID_BIT | (joined ? ZB_JOINED_BIT : 0u);
  unsigned old = atomic_exchange(&s_zigbee.packed, packed);
  atomic_store(&s_zigbee.recorded_ms, now_ms);
  if (old != packed) {
    notify_changed();
  }
}

void sdf_device_state_record_alarm_mask(uint16_t mask) {
  unsigned old = atomic_exchange(&s_alarm_mask, mask);
  if (old != mask) {
    notify_changed();
  }
}

sdf_device_state_snapshot_t sdf_device_state_snapshot(void) {
  sdf_device_state_snapshot_t snap = {0};

  unsigned lock_packed = atomic_load(&s_lock.packed);
  snap.lock.recorded_ms = atomic_load(&s_lock.recorded_ms);
  snap.lock.state = (sdf_device_state_lock_state_t)(lock_packed & 0xFFu);
  snap.lock.source =
      (sdf_device_state_lock_source_t)((lock_packed >> 8) & 0xFFu);
  snap.lock.condition = snap.lock.state == SDF_DEVICE_STATE_LOCK_UNKNOWN
                            ? SDF_DEVICE_STATE_CONDITION_UNKNOWN
                            : SDF_DEVICE_STATE_CONDITION_MEASURED;

  snap.battery.condition = atomic_load(&s_battery.has)
                               ? SDF_DEVICE_STATE_CONDITION_MEASURED
                               : SDF_DEVICE_STATE_CONDITION_UNKNOWN;
  snap.battery.percent = atomic_load(&s_battery.percent);
  snap.battery.recorded_ms = atomic_load(&s_battery.recorded_ms);

  unsigned fp_packed = atomic_load(&s_fingerprint.packed);
  snap.fingerprint.condition = (fp_packed & FP_VALID_BIT)
                                   ? SDF_DEVICE_STATE_CONDITION_MEASURED
                                   : SDF_DEVICE_STATE_CONDITION_UNKNOWN;
  snap.fingerprint.ready = (fp_packed & FP_READY_BIT) != 0u;
  snap.fingerprint.recorded_ms = atomic_load(&s_fingerprint.recorded_ms);

  unsigned nuki_packed = atomic_load(&s_nuki.packed);
  snap.nuki.condition = (nuki_packed & NUKI_VALID_BIT)
                            ? SDF_DEVICE_STATE_CONDITION_MEASURED
                            : SDF_DEVICE_STATE_CONDITION_UNKNOWN;
  snap.nuki.paired = (nuki_packed & NUKI_PAIRED_BIT) != 0u;
  snap.nuki.connected = (nuki_packed & NUKI_CONNECTED_BIT) != 0u;
  snap.nuki.recorded_ms = atomic_load(&s_nuki.recorded_ms);

  unsigned zb_packed = atomic_load(&s_zigbee.packed);
  /* Zigbee absent by configuration is distinct from present-but-unread:
   * the enabled check decides applicability, the cache decides the value. */
  if (!sdf_protocol_zigbee_is_enabled()) {
    snap.zigbee.condition = SDF_DEVICE_STATE_CONDITION_NOT_APPLICABLE;
  } else {
    snap.zigbee.condition = (zb_packed & ZB_VALID_BIT)
                                ? SDF_DEVICE_STATE_CONDITION_MEASURED
                                : SDF_DEVICE_STATE_CONDITION_UNKNOWN;
  }
  snap.zigbee.joined = (zb_packed & ZB_JOINED_BIT) != 0u;
  snap.zigbee.recorded_ms = atomic_load(&s_zigbee.recorded_ms);

  snap.alarm_mask = (uint16_t)atomic_load(&s_alarm_mask);

  return snap;
}

/* --- JSON serializer ---------------------------------------------------- */

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
  bool truncated;
} json_writer_t;

static void jw_puts(json_writer_t *w, const char *s) {
  while (*s != '\0') {
    if (w->len + 1 >= w->cap) {
      w->truncated = true;
      return;
    }
    w->buf[w->len++] = *s++;
  }
}

static void jw_printf(json_writer_t *w, const char *fmt, ...) {
  char tmp[64];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
  va_end(args);
  if (n < 0 || (size_t)n >= sizeof(tmp)) {
    /* A fragment that did not fit the scratch is a truncated report, not a
     * shorter one: say so, so the caller returns 0 rather than emitting
     * JSON that ends mid-value. */
    w->truncated = true;
    return;
  }
  size_t i = 0;
  while (tmp[i] != '\0' && i < (size_t)n) {
    if (w->len + 1 >= w->cap) {
      w->truncated = true;
      return;
    }
    w->buf[w->len++] = tmp[i++];
  }
}

/* Writes `s` as a quoted JSON string. Used for any value this report does
 * not itself spell - the firmware version comes from the app description
 * and is not guaranteed to be free of characters JSON reserves. */
static void jw_json_str(json_writer_t *w, const char *s) {
  jw_puts(w, "\"");
  for (; s != NULL && *s != '\0'; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') {
      char esc[3] = {'\\', (char)c, '\0'};
      jw_puts(w, esc);
    } else if (c < 0x20) {
      char esc[7];
      snprintf(esc, sizeof(esc), "\\u%04x", c);
      jw_puts(w, esc);
    } else {
      char lit[2] = {(char)c, '\0'};
      jw_puts(w, lit);
    }
  }
  jw_puts(w, "\"");
}

static const char *lock_state_name(sdf_device_state_lock_state_t state) {
  switch (state) {
  case SDF_DEVICE_STATE_LOCK_LOCKED:
    return "locked";
  case SDF_DEVICE_STATE_LOCK_UNLOCKED:
    return "unlocked";
  case SDF_DEVICE_STATE_LOCK_NOT_FULLY_LOCKED:
    return "not_fully_locked";
  case SDF_DEVICE_STATE_LOCK_UNKNOWN:
  default:
    return "unknown";
  }
}

static const char *lock_source_name(sdf_device_state_lock_source_t source) {
  switch (source) {
  case SDF_DEVICE_STATE_LOCK_SOURCE_REPORTED:
    return "reported";
  case SDF_DEVICE_STATE_LOCK_SOURCE_ASSUMED:
    return "assumed";
  case SDF_DEVICE_STATE_LOCK_SOURCE_NONE:
  default:
    return "none";
  }
}

static const char *ota_state_name(sdf_ota_state_t state) {
  switch (state) {
  case SDF_OTA_STATE_IDLE:
    return "idle";
  case SDF_OTA_STATE_DOWNLOADING:
    return "downloading";
  case SDF_OTA_STATE_VERIFYING:
    return "verifying";
  case SDF_OTA_STATE_COMMITTING:
    return "committing";
  case SDF_OTA_STATE_COMPLETE:
    return "complete";
  case SDF_OTA_STATE_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

static const char *setup_state_name(sdf_services_setup_state_t state) {
  switch (state) {
  case SDF_SERVICES_SETUP_STATE_NOT_STARTED:
    return "not_started";
  case SDF_SERVICES_SETUP_STATE_ADMIN_ENROLLED:
    return "admin_enrolled";
  case SDF_SERVICES_SETUP_STATE_REGISTERED:
    return "registered";
  case SDF_SERVICES_SETUP_STATE_NUKI_PAIRED:
    return "nuki_paired";
  case SDF_SERVICES_SETUP_STATE_COMPLETE:
    return "complete";
  default:
    return "unknown";
  }
}

size_t sdf_device_state_format_health_report(char *buf, size_t cap,
                                             uint32_t now_ms) {
  if (buf == NULL || cap < 2) {
    return 0;
  }

  json_writer_t w = {.buf = buf, .cap = cap, .len = 0, .truncated = false};
  sdf_device_state_snapshot_t snap = sdf_device_state_snapshot();

  jw_puts(&w, "{");

  /* Lock: state, provenance and age. A confirmation replaces an assumption
   * because it simply overwrites the cache entry. */
  if (snap.lock.condition == SDF_DEVICE_STATE_CONDITION_MEASURED) {
    jw_printf(&w, "\"lock\":{\"state\":\"%s\",\"source\":\"%s\",\"age_ms\":%u}",
              lock_state_name(snap.lock.state),
              lock_source_name(snap.lock.source),
              (unsigned)(now_ms - snap.lock.recorded_ms));
  } else {
    jw_puts(&w, "\"lock\":{\"state\":\"unknown\"}");
  }

  /* Battery: a measurement or unknown - never the configured default, never
   * a substitute constant. */
  if (snap.battery.condition == SDF_DEVICE_STATE_CONDITION_MEASURED) {
    jw_printf(&w, ",\"battery\":{\"percent\":%d,\"age_ms\":%u}",
              (int)snap.battery.percent,
              (unsigned)(now_ms - snap.battery.recorded_ms));
  } else {
    jw_puts(&w, ",\"battery\":{\"state\":\"unknown\"}");
  }

  /* Alarms: current mask, not a stale-able reading - no fabricated age. */
  jw_printf(&w, ",\"alarms\":{\"mask\":%u}", (unsigned)snap.alarm_mask);

  /* Fingerprint readiness: only what the fingerprint path published from
   * its own I/O - never probed on behalf of a reader. */
  if (snap.fingerprint.condition == SDF_DEVICE_STATE_CONDITION_MEASURED) {
    jw_printf(&w, ",\"fingerprint\":{\"ready\":%s,\"age_ms\":%u}",
              snap.fingerprint.ready ? "true" : "false",
              (unsigned)(now_ms - snap.fingerprint.recorded_ms));
  } else {
    jw_puts(&w, ",\"fingerprint\":{\"state\":\"unknown\"}");
  }

  if (snap.nuki.condition == SDF_DEVICE_STATE_CONDITION_MEASURED) {
    jw_printf(&w,
              ",\"nuki\":{\"paired\":%s,\"connected\":%s,\"age_ms\":%u}",
              snap.nuki.paired ? "true" : "false",
              snap.nuki.connected ? "true" : "false",
              (unsigned)(now_ms - snap.nuki.recorded_ms));
  } else {
    jw_puts(&w, ",\"nuki\":{\"state\":\"unknown\"}");
  }

  switch (snap.zigbee.condition) {
  case SDF_DEVICE_STATE_CONDITION_NOT_APPLICABLE:
    jw_puts(&w, ",\"zigbee\":{\"state\":\"not_applicable\"}");
    break;
  case SDF_DEVICE_STATE_CONDITION_MEASURED:
    jw_printf(&w, ",\"zigbee\":{\"joined\":%s,\"age_ms\":%u}",
              snap.zigbee.joined ? "true" : "false",
              (unsigned)(now_ms - snap.zigbee.recorded_ms));
    break;
  default:
    jw_puts(&w, ",\"zigbee\":{\"state\":\"unknown\"}");
    break;
  }

  /* Version, OTA state and setup state come straight from their owning
   * components so this report and their other surfaces cannot disagree.
   * None can go stale - no age is fabricated for them. */
  const char *version = sdf_ota_get_version();
  jw_puts(&w, ",\"firmware\":");
  jw_json_str(&w, version != NULL ? version : "");
  jw_printf(&w, ",\"ota\":\"%s\"", ota_state_name(sdf_ota_get_state()));
  jw_printf(&w, ",\"setup\":\"%s\"",
            setup_state_name(sdf_services_get_setup_state()));

  jw_puts(&w, "}");

  if (w.truncated) {
    return 0;
  }
  buf[w.len] = '\0';
  return w.len;
}

bool sdf_device_state_battery_is_low(int percent) {
  return percent >= 0 && percent <= SDF_DEVICE_STATE_BATTERY_LOW_PERCENT;
}

#include "sdf_zigbee_attr_cache.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Matches the timeout the rest of the component uses for its state mutex. The
 * mutex is only ever held for a struct copy, so reaching this is a bug rather
 * than contention. */
#define SDF_ZIGBEE_ATTR_CACHE_LOCK_MS 250

/* Power-on defaults, matching the ZCL "unknown" encodings: 0xFF for lock state
 * and 200 (i.e. 100% in half-percent units) for battery. */
#define SDF_ZIGBEE_ATTR_LOCK_STATE_UNDEFINED 0xFF
#define SDF_ZIGBEE_ATTR_BATTERY_UNKNOWN 200

static SemaphoreHandle_t s_lock;
static sdf_zigbee_attr_snapshot_t s_cache = {
    .lock_state = SDF_ZIGBEE_ATTR_LOCK_STATE_UNDEFINED,
    .battery_percent_remaining = SDF_ZIGBEE_ATTR_BATTERY_UNKNOWN,
    .alarm_mask = 0,
    .user_list_valid = false,
    .user_list = {0},
};

esp_err_t sdf_zigbee_attr_cache_init(void) {
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }
  return ESP_OK;
}

/* A plain (non-recursive) mutex on purpose: if the apply path ever regresses
 * to holding this across a writer that records, the re-entrant take times out
 * and the host lock-order test fails, rather than quietly succeeding. */
static esp_err_t sdf_zigbee_attr_cache_take(void) {
  esp_err_t err = sdf_zigbee_attr_cache_init();
  if (err != ESP_OK) {
    return err;
  }
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(SDF_ZIGBEE_ATTR_CACHE_LOCK_MS)) !=
      pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

void sdf_zigbee_attr_cache_reset(void) {
  if (sdf_zigbee_attr_cache_take() != ESP_OK) {
    return;
  }
  s_cache.lock_state = SDF_ZIGBEE_ATTR_LOCK_STATE_UNDEFINED;
  s_cache.battery_percent_remaining = SDF_ZIGBEE_ATTR_BATTERY_UNKNOWN;
  s_cache.alarm_mask = 0;
  s_cache.user_list_valid = false;
  s_cache.user_list[0] = '\0';
  xSemaphoreGive(s_lock);
}

esp_err_t sdf_zigbee_attr_cache_record_lock_state(uint8_t lock_state) {
  esp_err_t err = sdf_zigbee_attr_cache_take();
  if (err != ESP_OK) {
    return err;
  }
  s_cache.lock_state = lock_state;
  xSemaphoreGive(s_lock);
  return ESP_OK;
}

esp_err_t sdf_zigbee_attr_cache_record_battery_remaining(uint8_t remaining) {
  esp_err_t err = sdf_zigbee_attr_cache_take();
  if (err != ESP_OK) {
    return err;
  }
  s_cache.battery_percent_remaining = remaining;
  xSemaphoreGive(s_lock);
  return ESP_OK;
}

esp_err_t sdf_zigbee_attr_cache_record_alarm_mask(uint16_t alarm_mask) {
  esp_err_t err = sdf_zigbee_attr_cache_take();
  if (err != ESP_OK) {
    return err;
  }
  s_cache.alarm_mask = alarm_mask;
  xSemaphoreGive(s_lock);
  return ESP_OK;
}

esp_err_t sdf_zigbee_attr_cache_record_user_list(const char *json_array) {
  if (json_array == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  /* Checked before taking the mutex so an over-long list costs nothing and
   * leaves the previously recorded list intact. */
  size_t len = strlen(json_array);
  if (len >= SDF_ZIGBEE_USER_LIST_MAX) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = sdf_zigbee_attr_cache_take();
  if (err != ESP_OK) {
    return err;
  }
  memcpy(s_cache.user_list, json_array, len + 1U);
  s_cache.user_list_valid = true;
  xSemaphoreGive(s_lock);
  return ESP_OK;
}

esp_err_t sdf_zigbee_attr_cache_snapshot(sdf_zigbee_attr_snapshot_t *out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = sdf_zigbee_attr_cache_take();
  if (err != ESP_OK) {
    return err;
  }
  *out = s_cache;
  xSemaphoreGive(s_lock);
  return ESP_OK;
}

esp_err_t sdf_zigbee_attr_cache_apply(const sdf_zigbee_attr_writer_t *writer,
                                      void *ctx) {
  if (writer == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  sdf_zigbee_attr_snapshot_t snapshot;
  esp_err_t err = sdf_zigbee_attr_cache_snapshot(&snapshot);
  if (err != ESP_OK) {
    return err;
  }

  /* From here the mutex is released. Every call below may block for as long as
   * the Zigbee stack lock takes, and none of it may run with the cache mutex
   * held - see the file comment. Do not hoist any of this above the snapshot. */
  if (writer->write_u8 != NULL) {
    writer->write_u8(ctx, SDF_ZIGBEE_ATTR_LOCK_STATE, snapshot.lock_state);
    writer->write_u8(ctx, SDF_ZIGBEE_ATTR_BATTERY_PERCENT_REMAINING,
                     snapshot.battery_percent_remaining);
  }
  if (writer->write_u16 != NULL) {
    writer->write_u16(ctx, SDF_ZIGBEE_ATTR_ALARM_MASK, snapshot.alarm_mask);
  }
  if (snapshot.user_list_valid && writer->write_string != NULL) {
    writer->write_string(ctx, SDF_ZIGBEE_ATTR_ACTIVE_USER_LIST,
                         snapshot.user_list);
  }

  return ESP_OK;
}

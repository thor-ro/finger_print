/**
 * @file sdf_protocol_zigbee_mock_linux.c
 * @brief Linux host mock implementations for the Zigbee protocol component.
 */
#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_LINUX

#include "sdf_config.h"
#include "sdf_mock_linux_zigbee.h"
#include "sdf_protocol_zigbee.h"
#include "sdf_zigbee_attr_cache.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Only the ZCL half of the component is mocked. The recording and apply
 * ordering live in sdf_zigbee_attr_cache.c, which builds on both targets, so
 * the host suite drives the same code the device runs - including the
 * over-long user list bound and the coalescing rule.
 *
 * What the host cannot model is the applier task: there is nothing here to
 * consume the cache, so tests observe the recorded values through the
 * accessors below rather than assuming a synchronous write. */

/* Mirrors the target implementation so the enable semantics are identical on
 * both targets and the host suite can exercise the runtime kill switch. */
bool sdf_protocol_zigbee_is_enabled(void) {
  return sdf_config_get()->zigbee_enabled;
}

esp_err_t sdf_protocol_zigbee_init(void) {
  if (!sdf_protocol_zigbee_is_enabled()) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  return sdf_zigbee_attr_cache_init();
}

esp_err_t
sdf_protocol_zigbee_set_command_handler(sdf_protocol_zigbee_command_cb cb,
                                        void *ctx) {
  (void)cb;
  (void)ctx;
  return ESP_OK;
}

esp_err_t sdf_protocol_zigbee_update_lock_state(
    sdf_protocol_zigbee_lock_state_t lock_state) {
  if (lock_state != SDF_PROTOCOL_ZIGBEE_LOCK_STATE_NOT_FULLY_LOCKED &&
      lock_state != SDF_PROTOCOL_ZIGBEE_LOCK_STATE_LOCKED &&
      lock_state != SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNLOCKED &&
      lock_state != SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!sdf_protocol_zigbee_is_enabled()) {
    return ESP_OK;
  }

  return sdf_zigbee_attr_cache_record_lock_state((uint8_t)lock_state);
}

esp_err_t sdf_protocol_zigbee_report_lock_state(uint8_t state) {
  (void)state;
  return ESP_OK;
}

esp_err_t sdf_protocol_zigbee_update_user_list(const char *json_array) {
  if (!sdf_protocol_zigbee_is_enabled()) {
    return ESP_OK;
  }

  return sdf_zigbee_attr_cache_record_user_list(json_array);
}

esp_err_t sdf_protocol_zigbee_permit_join(void) {
  return ESP_OK;
}

esp_err_t sdf_protocol_zigbee_factory_reset(void) {
  return sdf_protocol_zigbee_is_enabled() ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sdf_protocol_zigbee_set_checkin_interval_ms(uint32_t interval_ms) {
  (void)interval_ms;
  return ESP_OK;
}

uint32_t sdf_protocol_zigbee_get_checkin_interval_ms(void) { return 0; }

esp_err_t sdf_protocol_zigbee_update_alarm_mask(uint16_t alarm_mask) {
  if (!sdf_protocol_zigbee_is_enabled()) {
    return ESP_OK;
  }

  return sdf_zigbee_attr_cache_record_alarm_mask(alarm_mask);
}

bool sdf_protocol_zigbee_is_ready(void) {
  return sdf_protocol_zigbee_is_enabled();
}

esp_err_t sdf_protocol_zigbee_update_battery_percent(uint8_t battery_percent) {
  if (battery_percent > 100U) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!sdf_protocol_zigbee_is_enabled()) {
    return ESP_OK;
  }

  /* ZCL reports battery in half-percent units. */
  return sdf_zigbee_attr_cache_record_battery_remaining(
      (uint8_t)(battery_percent * 2U));
}

/* Test-only view of what an apply would push. The cache enforces the
 * coalescing rule, so only the latest recorded value per attribute shows up. */
static sdf_zigbee_attr_snapshot_t s_mock_snapshot;

static const sdf_zigbee_attr_snapshot_t *sdf_protocol_zigbee_mock_snapshot(void) {
  if (sdf_zigbee_attr_cache_snapshot(&s_mock_snapshot) != ESP_OK) {
    return NULL;
  }
  return &s_mock_snapshot;
}

uint8_t sdf_protocol_zigbee_mock_get_lock_state(void) {
  const sdf_zigbee_attr_snapshot_t *snapshot =
      sdf_protocol_zigbee_mock_snapshot();
  return snapshot != NULL ? snapshot->lock_state
                          : SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED;
}

uint8_t sdf_protocol_zigbee_mock_get_battery_percent_remaining(void) {
  const sdf_zigbee_attr_snapshot_t *snapshot =
      sdf_protocol_zigbee_mock_snapshot();
  return snapshot != NULL ? snapshot->battery_percent_remaining : 200;
}

uint16_t sdf_protocol_zigbee_mock_get_alarm_mask(void) {
  const sdf_zigbee_attr_snapshot_t *snapshot =
      sdf_protocol_zigbee_mock_snapshot();
  return snapshot != NULL ? snapshot->alarm_mask : 0;
}

const char *sdf_protocol_zigbee_mock_get_user_list(void) {
  const sdf_zigbee_attr_snapshot_t *snapshot =
      sdf_protocol_zigbee_mock_snapshot();
  if (snapshot == NULL || !snapshot->user_list_valid) {
    return NULL;
  }
  return snapshot->user_list;
}

void sdf_protocol_zigbee_mock_reset(void) { sdf_zigbee_attr_cache_reset(); }

esp_err_t sdf_protocol_zigbee_trigger_ota_query(void) {
  return sdf_protocol_zigbee_is_ready() ? ESP_OK : ESP_ERR_INVALID_STATE;
}

#endif /* CONFIG_IDF_TARGET_LINUX */

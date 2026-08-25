/**
 * @file sdf_zigbee_attr_cache.h
 * @brief Component-owned cache of the ZCL attribute values, and the one path
 *        that applies them.
 *
 * This module exists to make one invariant testable: **the cache mutex is
 * never held while a ZCL write runs.** Inbound ZCL command handling arrives as
 * a Zigbee callback with the stack lock already held and then takes component
 * state; if the outbound path ever took the cache mutex and then the stack
 * lock, the two would form an AB-BA inversion. Both sides use timeouts, so the
 * symptom is a dropped update and a multi-hundred-millisecond stall rather
 * than a hang - which is exactly why it needs a test rather than a comment.
 *
 * The Zigbee SDK cannot be linked on the Linux host, so the writes themselves
 * are indirected through ::sdf_zigbee_attr_writer_t. Everything in this file
 * builds on both targets, which lets a host test drive the real
 * ::sdf_zigbee_attr_cache_apply() with a fake writer and assert the ordering
 * directly.
 */
#ifndef SDF_ZIGBEE_ATTR_CACHE_H
#define SDF_ZIGBEE_ATTR_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* A Zigbee character string is a one-byte length prefix followed by the data,
 * with 0xFF reserved to mean "invalid", so 254 bytes is the most the type can
 * carry. Values longer than this are rejected, never truncated: a truncated
 * JSON array is malformed, which serves a central worse than no update. */
#define SDF_ZIGBEE_ZCL_CHAR_STRING_MAX 254

/* Bound on the cached active-user list, including the NUL.
 *
 * Two derivations apply and the smaller one binds:
 *   - Capacity: sdf-services-tasks caps users at 10, and cJSON emits roughly
 *     {"id":10,"perm":255,"name":"<name>"} per entry - 31 fixed bytes plus a
 *     name of up to SDF_STORAGE_WEB_USER_NAME_MAX-1 = 31 chars. That is about
 *     620 bytes for a full list.
 *   - Transport: SDF_ZIGBEE_ZCL_CHAR_STRING_MAX is 254.
 *
 * Transport binds, so a full ten-user list with long names does not fit and is
 * rejected with its length logged. Carrying such a list needs a wider ZCL type
 * (long character string) rather than a bigger buffer here. */
#define SDF_ZIGBEE_USER_LIST_MAX (SDF_ZIGBEE_ZCL_CHAR_STRING_MAX + 1)

/** Which attribute a writer callback is being asked to push. Deliberately not
 *  ZCL cluster/attribute IDs - those live only on the target side, so this
 *  module stays free of the Zigbee SDK. */
typedef enum {
  SDF_ZIGBEE_ATTR_LOCK_STATE = 0,
  SDF_ZIGBEE_ATTR_BATTERY_PERCENT_REMAINING,
  SDF_ZIGBEE_ATTR_ALARM_MASK,
  SDF_ZIGBEE_ATTR_ACTIVE_USER_LIST
} sdf_zigbee_attr_id_t;

/** The ZCL side of an apply. Every callback is invoked with the cache mutex
 *  released, and may block for as long as the stack lock takes. */
typedef struct {
  void (*write_u8)(void *ctx, sdf_zigbee_attr_id_t attr, uint8_t value);
  void (*write_u16)(void *ctx, sdf_zigbee_attr_id_t attr, uint16_t value);
  void (*write_string)(void *ctx, sdf_zigbee_attr_id_t attr, const char *value);
} sdf_zigbee_attr_writer_t;

typedef struct {
  uint8_t lock_state;
  uint8_t battery_percent_remaining;
  uint16_t alarm_mask;
  bool user_list_valid;
  char user_list[SDF_ZIGBEE_USER_LIST_MAX];
} sdf_zigbee_attr_snapshot_t;

/** Idempotent; safe to call from several component entry points. */
esp_err_t sdf_zigbee_attr_cache_init(void);

/** Restores the power-on defaults. Test support. */
void sdf_zigbee_attr_cache_reset(void);

/**
 * @name Recording
 * Each records the latest value and returns promptly. None calls a writer, so
 * none can block on the Zigbee stack lock. A later value silently supersedes
 * an earlier one - that is the coalescing the contract promises.
 * @{
 */
esp_err_t sdf_zigbee_attr_cache_record_lock_state(uint8_t lock_state);
esp_err_t sdf_zigbee_attr_cache_record_battery_remaining(uint8_t remaining);
esp_err_t sdf_zigbee_attr_cache_record_alarm_mask(uint16_t alarm_mask);

/**
 * @return @c ESP_ERR_INVALID_ARG for NULL or a string that does not fit
 *         SDF_ZIGBEE_USER_LIST_MAX. Nothing is recorded in that case.
 */
esp_err_t sdf_zigbee_attr_cache_record_user_list(const char *json_array);
/** @} */

/** Copies the current values out under the mutex and releases it. */
esp_err_t sdf_zigbee_attr_cache_snapshot(sdf_zigbee_attr_snapshot_t *out);

/**
 * @brief Push the currently cached values through @p writer.
 *
 * Snapshots under the mutex, releases it, and only then invokes @p writer.
 * That ordering is the invariant this module exists to hold; a writer is free
 * to call back into the recording functions, and doing so must not deadlock.
 */
esp_err_t sdf_zigbee_attr_cache_apply(const sdf_zigbee_attr_writer_t *writer,
                                      void *ctx);

#endif /* SDF_ZIGBEE_ATTR_CACHE_H */

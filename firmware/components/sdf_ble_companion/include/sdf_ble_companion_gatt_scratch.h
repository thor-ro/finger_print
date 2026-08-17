#ifndef SDF_BLE_COMPANION_GATT_SCRATCH_H
#define SDF_BLE_COMPANION_GATT_SCRATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single-owner staging storage for inbound GATT writes.
 *
 * A GATT access callback has to hand an inbound payload to an application
 * callback *after* s_lock is released - callbacks must never run under
 * s_lock, since they may call back into sdf_ble_companion.c. That payload
 * therefore needs storage that outlives the locked region, and it cannot be
 * a 512-byte stack frame: every GATT access callback runs on the NimBLE host
 * task, whose stack is CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE (4096) - the
 * original four stack-local buffers were audit finding A14.
 *
 * The buffer used to be a plain `static uint8_t[512]` inside
 * sdf_ble_companion.c, safe only because a comment asserted that the host
 * task serialises all its users. Nothing enforced that, and the same file
 * touches connection state from the event-router and esp_timer tasks a few
 * hundred lines below, where the array was equally in scope and looked idle.
 * It lives here instead so the array is not a nameable symbol from those
 * paths, and so ownership is checked rather than assumed. See
 * openspec/changes/guard-ble-gatt-scratch-ownership/design.md.
 *
 * Contract: bind the owner once from the task that services GATT writes,
 * then acquire/release strictly on that task, released before returning from
 * the operation that acquired it. A violation is refused and reported - it
 * never returns storage another caller is holding, and it never aborts. */

/* Capacity of the staging buffer: the characteristic-wide attribute value
 * limit. sdf_ble_companion.c derives SDF_BLE_COMPANION_ATTR_MAX_LEN from
 * this so the two cannot drift. */
#define SDF_BLE_COMPANION_GATT_SCRATCH_LEN 512

/* Records the calling task as the owner of GATT write staging. Called once
 * from the NimBLE host task before advertising starts, so no client can be
 * connected - and therefore no acquire can happen - before ownership exists.
 * Re-binding from the same task (NimBLE resync re-enters the sync hook) is a
 * silent no-op. Re-binding from a *different* task is a contract violation:
 * it is refused and counted, and the original owner stays in effect. */
void sdf_ble_companion_gatt_scratch_bind_owner(void);

/* Returns the staging buffer (SDF_BLE_COMPANION_GATT_SCRATCH_LEN bytes) on
 * success, or NULL when staging is unbound, already held, or requested from
 * a task other than the owner. A NULL return is a contract violation, not a
 * client error: it is logged at error level and counted. The caller fails
 * just that GATT operation with an ATT error - the device keeps running and
 * advertising. */
uint8_t *sdf_ble_companion_gatt_scratch_acquire(void);

/* Releases staging. Idempotent: releasing when nothing is held is a no-op
 * and is not counted, so a caller may release unconditionally on its error
 * paths. Releasing from a non-owner task is refused and counted, and leaves
 * the buffer held by its owner. */
void sdf_ble_companion_gatt_scratch_release(void);

/* Number of refused acquires, refused releases and refused re-binds since
 * boot. Zero across a clean session; non-zero means the single-owner
 * contract was broken somewhere and the corresponding GATT operation failed. */
uint32_t sdf_ble_companion_gatt_scratch_violation_count(void);

#ifdef CONFIG_IDF_TARGET_LINUX
/* Test-only: clears the owner, the held flag and the violation counter so a
 * host test can start from the unbound state. Mirrors
 * sdf_event_router_reset_for_test(); not built for the device target. */
void sdf_ble_companion_gatt_scratch_reset_for_test(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SDF_BLE_COMPANION_GATT_SCRATCH_H */

## Why

`fingerprint.c` serializes all fingerprint sensor I/O with an internal mutex (`s_state.lock`) that only waits `FP_MUTEX_WAIT_MS` (250ms) to acquire, but the operations it guards are legitimately long-running UART round-trips (up to `CONFIG_SDF_FP_RESPONSE_TIMEOUT_MS` = 12000ms). Any real overlap between the tasks that call into the driver (match, enroll, admin, CLI console, factory reset) causes the losing caller to fail with `ESP_ERR_TIMEOUT` after 250ms instead of correctly waiting its turn - the mutex acquire window is two orders of magnitude shorter than the operation it protects, so under legitimate concurrent use it does not serialize, it just fails fast. Separately, `fp_set_power()` bypasses the lock entirely and is called directly (unguarded) by `sdf_match_task` during suspend/resume, so sensor power can be toggled while another task's UART transaction is in flight.

## What Changes

- Replace `fingerprint.c`'s internal `s_state.lock` mutex with a single dedicated owner task that exclusively holds the UART port and power-enable GPIO.
- Every fingerprint operation (match, enroll step, delete user, delete all users, query permission, change permission, query users, probe, set-keep-power-on, power on/off, deinit) becomes a request enqueued to the owner task; the calling task blocks on an indexed direct-to-task notification for the reply, with the result written into a caller-owned buffer before the notify.
- **BREAKING (internal only):** `FP_MUTEX_WAIT_MS` and `s_state.lock` are removed from `fingerprint.c`. No public API signature changes - `fp_match_1n()`, `fp_enroll_step()`, `fp_delete_user()`, etc. keep their existing signatures as thin client stubs over the request/response queue, so none of the ~10 existing call sites across `sdf_services_match.c`, `sdf_services_enroll.c`, `sdf_services.c`, `sdf_cli_commands.c`, and `sdf_app.c` need to change.
- `fp_set_power()` / `fp_set_keep_power_on()` are routed through the same owner-task queue as every other operation, closing the existing race where power could be toggled mid-transaction.
- Per-call-site `esp_task_wdt_reset()` calls scattered around `fp_*` invocations collapse into the owner task's single receive/dispatch loop.
- Add `configTASK_NOTIFICATION_ARRAY_ENTRIES` >= 2 (or equivalent indexed-notification config) so the owner task's reply notification does not collide with `sdf_match_task`'s existing use of the default notification index for its wake-ISR signal.

## Capabilities

### New Capabilities
- `fingerprint-io`: Fingerprint sensor I/O is exclusively owned by a single FreeRTOS task; all other tasks reach the sensor only via a request/response queue with bounded, operation-appropriate waits, with no internal mutex and no unguarded power-control side channel.

### Modified Capabilities
(none - existing `fp_*` call sites and their observable behavior toward callers are unchanged; only the internal serialization mechanism changes)

## Impact

- **Code:** `firmware/components/sdf_drivers/src/fingerprint.c` (internal rewrite: owner task, request/response types, removal of `s_state.lock`/`FP_MUTEX_WAIT_MS`), `firmware/components/sdf_drivers/include/fingerprint.h` (unchanged public signatures; possibly a new `fp_owner_task` internal declaration), `sdkconfig`/`Kconfig` (notification array size, and owner task priority/stack if configurable).
- **Callers (no code changes expected):** `sdf_services_match.c`, `sdf_services_enroll.c`, `sdf_services.c`, `sdf_cli_commands.c`, `sdf_app.c` - continue calling the same `fp_*` functions.
- **Concurrency model:** removes a mutex-based critical section in favor of a single-consumer queue; owner task priority must be >= the highest-priority caller (currently 5, `sdf_match_task`/`sdf_admin_task`) to avoid inversion.
- **Tests:** existing `fingerprint.c` unit/host tests (Linux mock driver path) need to account for the new task-based dispatch instead of direct synchronous calls.

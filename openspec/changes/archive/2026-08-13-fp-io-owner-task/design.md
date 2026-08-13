## Context

See proposal.md - Why. Today `fingerprint.c` guards its blocking UART operations with an internal FreeRTOS mutex (`s_state.lock`, `FP_MUTEX_WAIT_MS` = 250ms), while the operations themselves can legitimately run for up to `CONFIG_SDF_FP_RESPONSE_TIMEOUT_MS` (default 12000ms). Five independent call paths reach `fp_*`: `sdf_match_task` (prio 5), `sdf_enroll_task` (prio 4), `sdf_admin_task`/`sdf_services.c` (prio 5), CLI console commands, and `sdf_app.c` factory reset. `sdf_services.c`'s own service-state lock is already, correctly, released before calling into `fp_*` (see comments at `sdf_services.c:752`, `:774`, `:796`) - the bug is entirely inside `fingerprint.c`'s own serialization. Separately, `fp_set_power()` is a raw `gpio_set_level()` call with no locking at all, and `sdf_match_task` calls it directly during suspend/resume, so power can be toggled while another task's transaction is mid-flight.

`sdf_match_task` already uses the default FreeRTOS task-notification index (`ulTaskNotifyTake()`) for its wake-ISR signal (`sdf_services_match.c:369`); no indexed task notifications are configured anywhere in the codebase today.

## Goals / Non-Goals

**Goals:**
- Eliminate the internal mutex/timeout mismatch that causes spurious `ESP_ERR_TIMEOUT` under legitimate concurrent access.
- Preserve every existing `fp_*` public function signature so none of the ~10 call sites need to change.
- Close the unguarded `fp_set_power()` race as part of the same mechanism, at no extra design cost.
- Centralize the per-call-site watchdog bookkeeping that's currently scattered across callers.

**Non-Goals:**
- No change to the UART wire protocol, command framing, or sensor response parsing in `fingerprint.c`.
- No change to caller-visible error codes/result enums (`sdf_fingerprint_op_result_t`, `esp_err_t`).
- No change to enrollment's one-step-per-call state machine (`sdf_enrollment_sm`) - it keeps driving steps externally, one `fp_enroll_step()` per call.
- Not attempting a general-purpose task-notification framework; the indexed-notification usage here is scoped to this one owner/caller relationship.

## Decisions

### 1. Single owner task + FreeRTOS queue, not a smarter mutex
A mutex with a longer timeout would still be a mutex - the goal is mutual exclusion that's structural rather than timed. A single task exclusively owning the UART port and power-enable GPIO means only one execution context ever touches sensor state; no lock is needed at all. This matches the existing idiom in this codebase (`sdf_event_router` + per-task `QueueHandle_t`, used by match/enroll tasks already).

**Alternative considered:** raise `FP_MUTEX_WAIT_MS` to ~12s. Rejected - it doesn't fix the underlying problem (a blocked caller still burns its own task context spin-waiting on a semaphore instead of a proper queue rendezvous), doesn't fix the `fp_set_power()` race, and papering over a 250ms/12s mismatch with a bigger number just moves the mismatch rather than removing it.

### 2. Request/response via direct-to-task notification, not per-request reply queues
Each caller passes `xTaskGetCurrentTaskHandle()` and a pointer to a result buffer it owns (on its own stack) in the request. The owner task writes the result into that buffer, then calls `xTaskNotifyGiveIndexed()` on the caller's handle as a pure "you're done" doorbell. The caller was blocked in `ulTaskNotifyTakeIndexed()` and reads its own buffer once woken.

This avoids creating/managing N reply queues (one per possible caller) or dynamically allocating a queue per call. It also matches precedent already in this codebase (`sdf_match_task`'s wake-ISR notification).

**Collision with existing notification use:** `sdf_match_task` already consumes the default notification index for its wake-ISR signal. Fix: use `xTaskNotifyGiveIndexed()` / `ulTaskNotifyTakeIndexed()` with a reserved index (e.g. index 1) for "fingerprint request complete," leaving index 0 free for whatever a task already uses. Requires `configTASK_NOTIFICATION_ARRAY_ENTRIES >= 2` in sdkconfig.

**Alternative considered:** a per-caller static binary semaphore (mirroring the `TaskHandle_t match_task/enroll_task/admin_task/button_task` fields already in `sdf_services_internal.h`). Rejected as the primary mechanism because CLI console commands and `sdf_app.c`'s factory-reset path don't have a persistent per-task state struct to hang a semaphore off of, and indexed notifications generalize to any caller task without per-caller bookkeeping.

### 3. Existing `fp_*` functions become client stubs
`fp_match_1n()`, `fp_enroll_step()`, `fp_delete_user()`, `fp_delete_all_users()`, `fp_query_user_permission()`, `fp_change_user_permission()`, `fp_query_users()`, `fp_probe()`, `fp_set_power()`, `fp_set_keep_power_on()` keep their current signatures. Internally each builds an `fp_request_t`, enqueues it, blocks on the indexed notification, and returns the result the owner task wrote. `fp_init()`/`fp_deinit()` bring the owner task up/down and are not themselves routed through the queue (there's nothing to serialize against before the task exists, and deinit needs to stop the task).

### 4. Owner task priority and WDT
Owner task priority is set >= 5 (the highest current caller priority, shared by `sdf_match_task` and `sdf_admin_task`) so a high-priority caller is never left waiting behind the owner task being preempted by unrelated lower-priority work. FreeRTOS queues have no priority inheritance the way a mutex does, so this is a static choice rather than something the runtime corrects for.

The owner task registers with the task watchdog and resets it once per dispatched request in its own receive/process/reply loop, replacing the scattered `esp_task_wdt_reset()` calls currently placed around individual `fp_*` call sites in `sdf_services_match.c`, `sdf_services_enroll.c`, and `sdf_services.c:246`. Callers still reset their own WDT entry while blocked waiting for the reply notification, exactly as they do today while blocked inside a direct `fp_*` call - no change in caller-side WDT risk.

### 5. Request struct shape (illustrative, not final field layout)
```c
typedef enum {
  FP_OP_MATCH_1N, FP_OP_ENROLL_STEP, FP_OP_DELETE_USER, FP_OP_DELETE_ALL_USERS,
  FP_OP_QUERY_USER_PERMISSION, FP_OP_CHANGE_USER_PERMISSION, FP_OP_QUERY_USERS,
  FP_OP_PROBE, FP_OP_SET_POWER, FP_OP_SET_KEEP_POWER_ON,
} fp_op_type_t;

typedef struct {
  fp_op_type_t op;
  TaskHandle_t caller;
  union { /* per-op input args, e.g. enroll step/user_id/permission */ } args;
  void *result_out;   /* pointer into caller's stack frame, written before notify */
} fp_request_t;
```

## Risks / Trade-offs

- **[Risk]** Owner task itself becomes a single point of failure - if it stalls (e.g. stuck sensor read), every caller blocks. → **Mitigation:** this is no worse than today, where the same blocking UART call already runs on whichever task invoked it; the failure mode moves but doesn't get larger. Owner task's own WDT registration ensures a stuck owner still triggers a device-level watchdog panic rather than hanging silently forever.
- **[Risk]** Indexed task notifications are a new pattern in this codebase; incorrect index reservation could silently reintroduce the collision this design is meant to avoid (e.g. a future caller task picks the same index for something else). → **Mitigation:** define the reserved index as a single named constant (e.g. `FP_REPLY_NOTIFY_IDX`) in `fingerprint.h` or an internal header, documented as reserved.
- **[Risk]** CLI console commands and `sdf_app.c`'s factory-reset path run on tasks with no persistent state struct - need to confirm they have a `TaskHandle_t` and stack margin to safely block on `ulTaskNotifyTakeIndexed()` for up to ~12s. → **Mitigation:** verify during implementation; these call sites already block for up to 12s today inside direct `fp_*` calls, so this is a wait-shape change, not a new blocking duration.
- **[Trade-off]** Losing the mutex's priority inheritance means owner task priority must be chosen and maintained correctly by hand rather than being automatic. Acceptable given the small, fixed set of caller priorities (4 and 5) in this codebase.

## Migration Plan

1. Add `fp_request_t`/`fp_op_type_t` and the owner task skeleton to `fingerprint.c`, gated so it doesn't yet replace the mutex path (owner task created but unused).
2. Convert `fp_*` functions one at a time to enqueue-and-wait client stubs, removing their direct `xSemaphoreTake(s_state.lock, ...)` blocks as each is converted.
3. Route `fp_set_power()` through the same queue; update `sdf_match_task`'s suspend/resume path to call it the same way (no other change expected there).
4. Remove `s_state.lock`, `FP_MUTEX_WAIT_MS`, and the semaphore create/delete in `fp_init()`/`fp_deinit()` once all call sites are converted.
5. Bump `configTASK_NOTIFICATION_ARRAY_ENTRIES` and reserve `FP_REPLY_NOTIFY_IDX`.
6. Update the Linux host-test mock driver path (`sdf_mock_linux_drivers.h` / test runner) so unit tests still exercise the new dispatch without needing a real UART.
7. No feature flag or staged rollout needed beyond normal review - this is an internal concurrency mechanism swap with unchanged public behavior; rollback is a straight revert if issues surface.

## Open Questions

- Exact stack size needed for the owner task once it's the one performing all UART framing/parsing work directly (currently spread across caller task stacks) - to be confirmed by `uxTaskGetStackHighWaterMark()` during implementation, not a spec/design-affecting unknown.

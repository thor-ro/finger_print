## 1. Owner task scaffolding

- [ ] 1.1 Define `fp_op_type_t`, `fp_request_t` (op, `TaskHandle_t caller`, args union, `result_out` pointer) in `fingerprint.c` (internal, not exposed in `fingerprint.h`)
- [ ] 1.2 Add the owner task's request `QueueHandle_t`, create it in `fp_init()`
- [ ] 1.3 Implement the owner task loop: `xQueueReceive` → dispatch on `op` → write result via `result_out` → `xTaskNotifyGiveIndexed(caller, FP_REPLY_NOTIFY_IDX)` → `esp_task_wdt_reset()`
- [ ] 1.4 Register the owner task with the task watchdog; set its priority >= 5 (>= `sdf_match_task`/`sdf_admin_task`)
- [ ] 1.5 Define `FP_REPLY_NOTIFY_IDX` as a named, documented-reserved constant
- [ ] 1.6 Bump `configTASK_NOTIFICATION_ARRAY_ENTRIES` to >= 2 in sdkconfig so the reserved index doesn't collide with existing default-index notification use (e.g. `sdf_match_task`'s wake-ISR signal)

## 2. Convert call sites to enqueue-and-wait stubs

- [ ] 2.1 Convert `fp_match_1n()` to build a request, enqueue it, `ulTaskNotifyTakeIndexed()` for the reply, and return the result written by the owner task; remove its `s_state.lock` usage
- [ ] 2.2 Convert `fp_enroll_step()` the same way
- [ ] 2.3 Convert `fp_delete_user()` and `fp_delete_all_users()` the same way
- [ ] 2.4 Convert `fp_query_user_permission()` and `fp_change_user_permission()` the same way
- [ ] 2.5 Convert `fp_query_users()` the same way, including its output array/count arguments
- [ ] 2.6 Convert `fp_probe()` the same way
- [ ] 2.7 Convert `fp_set_power()` to route through the owner-task queue instead of calling `gpio_set_level()` directly; update `sdf_match_task`'s suspend/resume path if its call signature or blocking behavior changes
- [ ] 2.8 Convert `fp_set_keep_power_on()` the same way

## 3. Remove the old locking mechanism

- [ ] 3.1 Remove `s_state.lock` (mutex create/delete) from `fp_init()`/`fp_deinit()`
- [ ] 3.2 Remove `FP_MUTEX_WAIT_MS` and all remaining `xSemaphoreTake`/`xSemaphoreGive` calls in `fingerprint.c`
- [ ] 3.3 Update `fp_deinit()` to stop the owner task cleanly (drain or reject in-flight requests, then delete the task and queue) without racing an in-flight request

## 4. Watchdog consolidation

- [ ] 4.1 Remove the per-call-site `esp_task_wdt_reset()` calls in `sdf_services_match.c`, `sdf_services_enroll.c`, and `sdf_services.c:246` that were compensating for long blocking `fp_*` calls, now that the owner task resets its own watchdog entry per dispatched request
- [ ] 4.2 Confirm callers still reset their own watchdog entry while blocked on `ulTaskNotifyTakeIndexed()`, matching current behavior while blocked inside a direct `fp_*` call

## 5. Test coverage

- [ ] 5.1 Update the Linux host-test mock driver path (`sdf_mock_linux_drivers.h` / test runner) so existing `fingerprint.c` unit tests exercise the owner-task dispatch instead of direct synchronous calls
- [ ] 5.2 Add a test that issues two overlapping fingerprint operations from different tasks and asserts the second completes correctly after the first, instead of failing with `ESP_ERR_TIMEOUT`
- [ ] 5.3 Add a test that requests a power-off while an operation is in flight and asserts the power change is deferred until the operation completes
- [ ] 5.4 Run the full existing `fingerprint.c` and `sdf_services` test suites and confirm no regressions

## 6. Verification on hardware

- [ ] 6.1 Confirm owner task stack size is sufficient via `uxTaskGetStackHighWaterMark()` on real ESP32-C6 hardware (per design.md Open Questions)
- [ ] 6.2 Manually verify a concurrent-use scenario on hardware (e.g. trigger enrollment via button while a match cycle is in progress) completes correctly instead of erroring

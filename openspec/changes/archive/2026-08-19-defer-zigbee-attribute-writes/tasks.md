## 1. Add the applier task

- [x] 1.1 Add to `s_state` in `firmware/components/sdf_protocol_zigbee/src/sdf_protocol_zigbee.c`: a `TaskHandle_t attr_task` handle, plus the user-list cache fields from task 3.1. Initialize `attr_task = NULL` in the static initializer alongside the existing `.stack_started = false`.
- [x] 1.2 Define `SDF_ZIGBEE_ATTR_TASK_NAME`, `SDF_ZIGBEE_ATTR_TASK_STACK` and `SDF_ZIGBEE_ATTR_TASK_PRIORITY` next to the existing `SDF_ZIGBEE_TASK_*` constants. Set the priority at or below `SDF_ZIGBEE_TASK_PRIORITY`; start the stack at a provisional size and correct it in task 5.2 from measurement.
- [x] 1.3 Implement `sdf_zigbee_attr_task()`: loop on `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` then call the existing `sdf_zigbee_apply_cached_attributes()`. `pdTRUE` (clear-on-exit) is what gives burst coalescing — do not use `pdFALSE`.
- [x] 1.4 Create the applier task in `sdf_protocol_zigbee_init()` **before** `xTaskCreate(sdf_zigbee_task, ...)` (currently line 1008), so a notify can never target a null handle. On creation failure, fail init the same way the existing task-creation failure path does.
- [x] 1.5 Delete the applier task and null its handle in the `fail:` teardown block of `sdf_zigbee_task()`, alongside the existing `s_state.task = NULL` cleanup.
- [x] 1.6 Add a `sdf_zigbee_notify_attr_task()` static helper that reads `attr_task` under `s_state.lock`, and calls `xTaskNotifyGive()` only if it is non-NULL.

## 2. Convert the three scalar updaters

- [x] 2.1 In `sdf_protocol_zigbee_update_lock_state()` (line 1049): keep the argument validation, the `is_enabled` early return, the `s_state.lock == NULL` check, and the `s_state.lock_state = ...; ready = s_state.stack_started;` block. Replace the trailing `return sdf_zigbee_set_attr_u8(...)` with: if `ready`, call `sdf_zigbee_notify_attr_task()`; return `ESP_OK` either way.
- [x] 2.2 Apply the same transformation to `sdf_protocol_zigbee_update_battery_percent()`, preserving the `battery_percent > 100U` rejection and the `* 2U` conversion.
- [x] 2.3 Apply the same transformation to `sdf_protocol_zigbee_update_alarm_mask()`.
- [x] 2.4 Confirm no Zigbee SDK symbol (`esp_zb_*`) remains reachable from any of the three functions.

## 3. Convert the user-list updater and fix the lock inversion

- [x] 3.1 Add a fixed-size user-list cache to `s_state`: a `char` buffer plus a `bool user_list_valid`. Derive the buffer size from the user-capacity requirement in `openspec/specs/sdf-services-tasks/spec.md` and the JSON produced at `sdf_app.c:1074` — not from a currently observed string length. Define the size as a named constant with a comment recording the derivation.
- [x] 3.2 Rewrite `sdf_protocol_zigbee_update_user_list()` (line 313): reject `NULL`, reject a string that does not fit the buffer with `ESP_ERR_INVALID_ARG` and a log line stating the rejected length and the limit (never truncate — a truncated JSON array is malformed). Otherwise copy into the cache under `s_state.lock`, set `user_list_valid`, read `stack_started`, release the mutex, then notify if ready.
- [x] 3.3 Verify the rewritten function no longer holds `s_state.lock` across any `esp_zb_*` call — this is the AB-BA inversion against `sdf_zigbee_action_handler()` → `sdf_zigbee_dispatch_command_event()` and is the primary correctness fix in this group.
- [x] 3.4 Extend `sdf_zigbee_apply_cached_attributes()` (line 334) to also push the user list when `user_list_valid`: copy the string out under the mutex into a local, release the mutex, then call `sdf_zigbee_set_attr_string()`. Do not call `sdf_zigbee_set_attr_string()` while holding `s_state.lock`.

## 4. Update the public contract

- [x] 4.1 In `firmware/components/sdf_protocol_zigbee/include/sdf_protocol_zigbee.h`, document on all four update functions that `ESP_OK` means "accepted for asynchronous application", that ZCL write failures are logged rather than returned, and that argument/lifecycle errors are still synchronous.
- [x] 4.2 Document that updates are coalesced to the latest value, so an intermediate value may never appear as its own attribute report.
- [x] 4.3 Grep every caller (`sdf_app.c:248`, `253`, `1074`, `1398`, `1871`, and `sdf_app_set_alarm_mask_bits()`) and confirm none treats `ESP_OK` as proof the attribute reached the stack. Expected outcome: no call sites change.
- [x] 4.4 Mirror the four functions' new behavior in `sdf_protocol_zigbee_mock_linux.c` so host tests exercise the same contract.

## 5. Verify

- [x] 5.1 Build the firmware clean; run the host test suite.
- [x] 5.2 Measure the applier task's stack with `uxTaskGetStackHighWaterMark` after exercising all four attribute types, and set `SDF_ZIGBEE_ATTR_TASK_STACK` from the measurement with margin. Record the measured figure in the commit message.
- [x] 5.3 Add a host test for the coalescing requirement: record several lock-state updates before the applier runs, then confirm the attribute converges to the last value.
- [x] 5.4 Add a host test for the lock-order requirement that exercises inbound ZCL dispatch (stack lock → state mutex) concurrently with a user-list update, asserting neither times out. This is the regression guard for a deadlock that timeouts currently mask.
- [x] 5.5 Audit host tests and `sdf_cli_commands.c` for read-after-write patterns that assume an attribute is readable immediately after an update call returns; convert any to poll-with-timeout.
- [x] 5.6 Run under `esp-emu` through boot, a Nuki lock action, and a user-list change. Confirm the Zigbee `LockState` attribute still tracks the lock and that no `esp_zb_*` frame appears on the NimBLE host task's stack. Treat any panic as a real defect, not an emulator fidelity artifact.
- [x] 5.7 Confirm the "update before stack start" path still works: values cached before `stack_started` flips true must be pushed by the existing `sdf_zigbee_apply_cached_attributes()` call in `sdf_zigbee_task()` (line 941), with no caller retry.

## Why

`sdf_protocol_zigbee_update_lock_state()` and its siblings push ZCL attributes synchronously on whatever task called them. That path is:

```
xSemaphoreTake(s_state.lock, 250 ms)   →   esp_zb_lock_acquire(1000 ms)
```

up to **~1250 ms of blocking on the caller**. The dominant caller is `sdf_app_on_message()`, which runs on the **NimBLE host task** — so every keyturner-states notification from the Nuki lock can stall the BLE host inside a GATT notify callback for over a second. That is how connections get dropped.

While tracing the locking, a second and more serious problem surfaced: a lock-order inversion.

- `sdf_protocol_zigbee_update_user_list()` holds `s_state.lock` **while** acquiring the Zigbee stack lock (`sdf_protocol_zigbee.c:322-330`).
- `sdf_zigbee_action_handler()` is registered via `esp_zb_core_action_handler_register()`, so it runs as a Zigbee callback with the stack lock already held — and it reaches `sdf_zigbee_dispatch_command_event()`, which takes `s_state.lock`.

That is a textbook AB-BA inversion between an inbound ZCL command and an outbound user-list update. Both sides have timeouts, so it degrades to a timeout rather than a permanent hang — but the failure mode is a silently dropped attribute update plus a 250-1000 ms stall on both tasks.

The component already has everything needed to fix both: `s_state` caches `lock_state`, `battery_percent_remaining` and `alarm_mask`, and `sdf_zigbee_apply_cached_attributes()` (`sdf_protocol_zigbee.c:334`) already reads that cache and pushes all three attributes. The updaters just also do a synchronous push on the caller's stack when `stack_started` happens to be true.

## What Changes

- Add a small dedicated **attribute applier task** owned by `sdf_protocol_zigbee`. It blocks on a task notification and calls the existing `sdf_zigbee_apply_cached_attributes()`.
  - It cannot be `sdf_zigbee_task`: that task ends in `esp_zb_stack_main_loop()`, which never returns, so it can never service a notification.
- Change `sdf_protocol_zigbee_update_lock_state()`, `..._update_battery_percent()` and `..._update_alarm_mask()` to: validate → take `s_state.lock` briefly → write the cache → release → notify the applier task → return. No Zigbee SDK call on the caller's context.
- Change `sdf_protocol_zigbee_update_user_list()` to cache the JSON string and let the applier task push it, removing the nested `s_state.lock` → `esp_zb_lock` acquisition and with it the lock-order inversion.
- **BREAKING (semantic)**: these four functions no longer report ZCL write failure through their return value. `ESP_OK` changes meaning from "attribute written" to "update accepted; it will be applied". Argument validation and `ESP_ERR_INVALID_STATE` still return synchronously. Write failures are logged by the applier task, as `sdf_zigbee_set_attr_u8()` already does.
- Burst coalescing falls out of the design: the applier always reads current cache state, so N updates arriving during one apply collapse into one push of the latest values. This is a behavior change worth stating — an intermediate value may never be pushed as its own attribute report.

Explicitly **not** in scope: routing these updates through `sdf_event_router`. That was considered and rejected — it would relocate the stack-lock stall onto the shared router task and head-of-line block `BIOMETRIC_MATCH`, `ENROLLMENT_*`, `ADMIN_*` and `AUDIT` dispatch. The stall belongs on a task that owns nothing else.

## Capabilities

### New Capabilities

- `zigbee-attribute-reporting`: the contract for how firmware state (lock state, battery, alarm mask, active-user list) reaches Zigbee ZCL attributes — specifically that update calls never block on the Zigbee stack lock, that they are applied asynchronously and coalesced, and that no call path holds `s_state.lock` across a Zigbee SDK call.

### Modified Capabilities

None. No existing spec has requirements covering `sdf_protocol_zigbee`'s attribute-update API (verified by grep across `openspec/specs/`); `zigbee-commissioning` covers only network steering and commissioning state.

## Impact

**Code** — all in `firmware/components/sdf_protocol_zigbee/src/sdf_protocol_zigbee.c`:
- New applier task + notification handle in `s_state`; new cached user-list buffer.
- `sdf_protocol_zigbee_update_lock_state()` (line 1049), `..._update_battery_percent()` (~1090), `..._update_alarm_mask()` (~1120), `..._update_user_list()` (line 313).
- `sdf_zigbee_apply_cached_attributes()` (line 334) extended to push the user list.
- Lifecycle: the applier task must start before `stack_started` flips true, and be torn down on the `fail:` path of `sdf_zigbee_task`.

**Callers** — no signature changes, so no call sites need editing. `sdf_app.c:248`, `253`, `1398`, `1871`, `1074` and `sdf_app_set_alarm_mask_bits()` keep working. Any caller treating `ESP_OK` as proof the attribute reached the stack is now wrong; grep confirms none does — every call site ignores the return value or only logs it.

**RAM** — one additional task. Stack can be small (it calls only `apply_cached_attributes`), but on ESP32-C6 this is a real cost and the exact figure should be measured, not guessed.

**Timing** — attribute reports become asynchronous. A Zigbee central may observe a state change a few milliseconds later than today. Against a Nuki round-trip measured in hundreds of milliseconds this is not observable, but it does mean tests must not assert that an attribute is readable immediately after an update call returns.

**Constraint verified** — `esp_zb_scheduler_alarm()` / `esp_zb_scheduler_user_alarm()` are **not** usable as the wake mechanism from a foreign task. `esp_zigbee_core.h:363` states the lock is mandatory before any Zigbee SDK API except from within Zigbee callbacks, and both existing call sites in this file (lines 209, 1225) hold the lock. The wake must be a plain FreeRTOS notification.

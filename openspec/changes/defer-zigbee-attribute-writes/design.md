## Context

See proposal.md — Why for motivation, and specs/zigbee-attribute-reporting/spec.md for the contract. Constraints that shape the approach, all verified against the tree:

- **`sdf_zigbee_task` cannot be the applier.** It ends in `esp_zb_stack_main_loop()` (`sdf_protocol_zigbee.c:943`), which does not return. It can never service a notification. This rules out the obvious "wake the existing Zigbee task" design.
- **There is no lock-free way to post work to the Zigbee stack.** `esp_zigbee_core.h:363` states the stack lock is "mandatory before calling any Zigbee SDK APIs, except that the call site is in Zigbee callbacks." That covers `esp_zb_scheduler_alarm()` and `esp_zb_scheduler_user_alarm()` too; both existing call sites in this file (lines 209, 1225) hold the lock. So the wake mechanism must be plain FreeRTOS.
- **The cache and the applier body already exist.** `s_state` holds `lock_state`, `battery_percent_remaining` and `alarm_mask`; `sdf_zigbee_apply_cached_attributes()` (line 334) already reads all three under the state mutex, releases it, and pushes them. It is already called once, from `sdf_zigbee_task` right after `stack_started` flips true.
- **The updaters already have the fast path.** Each writes the cache, reads `stack_started`, and returns `ESP_OK` early when the stack is not up. Only the trailing `sdf_zigbee_set_attr_*()` call is being removed from the caller's context.
- **`sdf_protocol_zigbee_update_user_list()` is the odd one out.** It holds `s_state.lock` across `sdf_zigbee_set_attr_string()` (lines 322-330) and has no cache field, so it needs new state, not just a moved call.

## Goals / Non-Goals

**Goals:**
- No Zigbee SDK call on any caller's context.
- Eliminate the AB-BA inversion between inbound ZCL dispatch and outbound user-list update.
- Reuse `sdf_zigbee_apply_cached_attributes()` rather than write a parallel push path.
- No signature changes, so no call sites move.

**Non-Goals:**
- Reducing how long the stack lock is held. The applier may block for the full `esp_zb_lock_acquire(1000 ms)`; that is now harmless because it blocks only itself.
- Per-attribute dirty tracking. Pushing all four attributes on every wake is a handful of ZCL writes at human-scale frequency; the complexity is not earned.
- Delivery guarantees or retry on ZCL write failure. Logging matches today's behavior.
- Any change to how BLE state reaches `sdf_app` — that is the direct-call bridge, untouched here.

## Decisions

### D1: A dedicated applier task, not a timer or an existing task

**Chosen:** a small task owned by `sdf_protocol_zigbee` that blocks on `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` and calls `sdf_zigbee_apply_cached_attributes()`.

**Alternatives considered:**
- *Wake `sdf_zigbee_task`* — impossible, see Context.
- *`esp_timer` callback* — esp_timer callbacks must not block, and this work needs a 1 s lock acquisition. Non-starter.
- *`esp_zb_scheduler_alarm()` from the caller* — requires the stack lock, which is exactly what we are refusing to take. Non-starter.
- *Periodic poll of a dirty flag from an existing housekeeping task* — adds latency proportional to the poll interval and couples an unrelated task to Zigbee's blocking behavior.

**Rationale:** the applier task exists precisely to be the thing that is allowed to block. Its only job is to absorb the stack-lock wait, so stalling it has no consequence. `ulTaskNotifyTake` with `xClearCountOnExit = pdTRUE` gives burst coalescing for free.

**Cost:** one task's stack and TCB. It calls only `apply_cached_attributes()` → `esp_zb_lock_acquire` → `esp_zb_zcl_set_attribute_val`, so the stack can be modest — but the figure must be **measured** with a high-water-mark check, not guessed. On ESP32-C6 this RAM is not free.

### D2: Notify unconditionally; let the applier decide whether the stack is ready

**Chosen:** updaters write the cache and always notify. The applier calls `apply_cached_attributes()`, which is already a no-op-ish path when the stack is not started (`sdf_zigbee_set_attr_*` will fail and log).

**Alternative considered:** keep the `ready = s_state.stack_started` read in each updater and skip the notify when the stack is down.

**Rationale:** keeping the `stack_started` check is worth it — it avoids pointless wakes and, more importantly, avoids log spam from failed attribute writes during boot before the stack is up. So: **keep the existing `ready` read, and notify only when `ready` is true.** The existing post-start `apply_cached_attributes()` call in `sdf_zigbee_task` (line 941) remains the mechanism that flushes everything cached before startup — that is why the "update before stack start" scenario in the spec holds without any extra work.

### D3: Give the user list a cache slot rather than a separate path

**Chosen:** add a fixed-size buffer in `s_state` for the user-list JSON; `update_user_list()` copies into it under the state mutex and notifies; `apply_cached_attributes()` pushes it alongside the other three.

**Alternative considered:** leave `update_user_list()` synchronous and only reorder its locking to fix the inversion.

**Rationale:** reordering alone fixes the deadlock but leaves the caller blocking on the stack lock, so it would satisfy the second requirement and violate the first. Uniform treatment is also simpler to reason about: after this change, *every* attribute reaches the stack through exactly one path.

**Trade-off:** a fixed buffer imposes a maximum user-list length, where today the caller's string is used directly. The size must be derived from the existing caller (`sdf_app.c:1074`) and the user-capacity requirement in `sdf-services-tasks`, and an over-long list must be rejected with `ESP_ERR_INVALID_ARG` (synchronous, per the spec) rather than silently truncated — a truncated JSON array would be malformed and worse than no update.

### D4: Return value means "accepted", and that is a real semantic break

Documented as **BREAKING** in the proposal. Grep of every call site (`sdf_app.c:248`, `253`, `1074`, `1398`, `1871`, and `sdf_app_set_alarm_mask_bits()`) confirms none inspects the return value for write success — they ignore it or log it. So no caller changes, but the header doc comments must be updated to say so explicitly, or the next caller will assume the old contract.

### D5: Task lifecycle is tied to the component, not the stack

**Chosen:** create the applier task in `sdf_protocol_zigbee_init()`, before `sdf_zigbee_task` is created, and delete it on the `fail:` path of `sdf_zigbee_task` alongside the other state teardown.

**Rationale:** creating it first means a notify can never target a null handle, including from an update that races startup. Guard the notify on a non-null handle read under the state mutex regardless — cheap, and it keeps the "Zigbee disabled" no-op path honest.

## Risks / Trade-offs

- **The applier task's stack is undersized and it overflows inside the Zigbee SDK** → Measure with `uxTaskGetStackHighWaterMark` under the emulator after exercising all four attribute types, and size with margin. Do not copy a number from another task.
- **A test or CLI command asserts an attribute is readable immediately after an update returns** → Such an assertion is now wrong by contract. Audit host tests and `sdf_cli_commands.c` for read-after-write patterns and convert them to poll-with-timeout.
- **User-list buffer sizing is wrong and valid lists get rejected** → Derive the bound from the documented user capacity rather than the current observed string length, and log the rejected length so a too-small bound is diagnosable rather than mysterious.
- **Coalescing hides a transient state a central wanted to see** → Accepted, and specified. Door lock state transitions of interest (LOCKED/UNLOCKED/NOT_FULLY_LOCKED) arrive hundreds of milliseconds apart over BLE; they will not collide. Coalescing only merges bursts that the stack lock would have serialized anyway.
- **The deadlock being fixed is currently masked by timeouts, so there is no failing test to prove the fix** → Add a targeted host test that exercises the two lock orders concurrently, so the inversion cannot be reintroduced silently.

## Migration Plan

No persisted state, no wire-format change, no OTA compatibility concern. Deploy is an ordinary firmware build; rollback is `git revert`.

Recommended landing order: **after `remove-dead-protocol-adapter-events`**. That change deletes lines from `sdf_zigbee_dispatch_command_event()` in this same file; landing it first keeps this change's edits conflict-free.

Verification that the original problem is actually gone: run under `esp-emu`, exercise a Nuki lock action end to end, and confirm no Zigbee SDK call appears on the NimBLE host task's stack. Per project history, treat any emulator panic as a real defect rather than a fidelity artifact.

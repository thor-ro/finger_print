## Context

Smart Door Finger (SDF) is an ESP32-C6 firmware bridge translating Zigbee commands and fingerprint matches to BLE lock actions via Nuki Smart Lock 3 Pro. The power management, event routing, fingerprint matching, and app-layer state handling are performance-critical paths running on a resource-constrained microcontroller with a single CPU core, limited RAM, and aggressive sleep requirements for battery life.

A code review identified 14+ optimization opportunities spanning code duplication, correctness risks, unnecessary locking, polling inefficiency, and a variable-declaration bug. These are organized into 7 capabilities covering power management, event routing, match task, and app-layer improvements.

## Goals / Non-Goals

**Goals:**
- Eliminate duplicated light-sleep logic between `sdf_power_sleep_once()` and `sdf_power_task()`
- Fix deep sleep retention to properly preserve and CRC-protect state across sleep cycles
- Reduce lock contention in the match task cycle by batching config reads and state writes
- Replace the heavy WDT delete/recreate + semaphore-block power transition with a lightweight suspend/resume mechanism
- Fix the duplicate variable declaration bug in `sdf_app.c`
- Prevent resource leaks by cleaning up event subscriptions on init failure
- Reduce unnecessary Zigbee traffic by guarding alarm mask updates
- Improve event dispatch latency with type-indexed subscriber lookup

**Non-Goals:**
- Changing the BLE/Nuki protocol implementation
- Modifying the fingerprint sensor UART protocol or driver internals
- Altering the security default values (lockout threshold, fail window, etc.)
- Changing the build system or CI configuration
- Refactoring non-critical paths (e.g., CLI command parsing)

## Decisions

### 1. Light-sleep helper: free function returning esp_err_t

The duplicated sleep-entry code (~40 lines) will be extracted into `sdf_power_enter_light_sleep(const sdf_power_manager_config_t *config)`. This returns `esp_err_t` and encapsulates the full sequence: emit sleep event, disable wake sources, configure timer+GPIO wake, gate BLE radio, call `sdf_platform_sleep_light()`, restore BLE, map wake reason, notify wake callback. Both `sdf_power_sleep_once()` and the `sdf_power_task()` light sleep path call this helper.

**Rationale:** A free function is simpler than a macros or inline wrapper, allows proper error handling, and keeps the function testable.

**Alternative considered:** Macro-based dedup — rejected because macros cannot handle error handling or the BLE-gate conditional cleanly.

### 2. Deep sleep retention: use `sdf_power_save_retention()` in the task path

The deep sleep path in `sdf_power_task()` (lines 311–316) manually writes a `sdf_power_retention_t` struct without CRC. This will be replaced with a call to `sdf_power_save_retention()`, which correctly computes CRC16 and writes the full struct. This also fixes the fact that the stub fields in `sdf_power_prepare_deep_sleep()` are never populated before being saved.

**Rationale:** `sdf_power_save_retention()` already handles CRC computation and proper struct formatting. Reusing it ensures consistency between the prepare and task-path retention writes.

### 3. Event router: simple type-indexed dispatch

Subscriber indexing will use a fixed-size array of `sdf_event_router_subscriber_t *` indexed by `sdf_event_router_type_t`. Since `sdf_event_router_type_t` is an enum with a known set of values, a simple array lookup replaces the O(n) linked-list walk. Each slot points to the head of a subscriber list for that type.

**Rationale:** The number of event types is bounded (~20), so a fixed-size array is trivial memory overhead (~160 bytes on 64-bit pointer) and provides O(1) dispatch.

**Alternative considered:** Hash map — rejected as overengineering for a fixed, small set of event types.

### 4. Match task: batch lock acquisitions

` sdf_match_task_run_match_cycle()` will reduce lock acquisitions from 6+ to 2: one at the start to read all config and state, one at the end to write state updates. The intermediate lock acquisitions for individual field updates will be combined.

**Rationale:** Each `xSemaphoreTake`/`xSemaphoreGive` pair involves a potential context switch (~1–5 µs). While small individually, reducing 6 pairs to 2 per 400ms cycle reduces context-switch overhead and lock contention with other tasks.

### 5. Match task suspend/resume: suspend flag + poll interval increase

Replace the WDT delete/recreate + `portMAX_DELAY` semaphore-block with a suspend flag (`s_match_state.suspended = true`) and a longer poll interval (e.g., 10 seconds) when no match activity is pending. The existing `POWER_WAKE` and `POWER_SLEEP` event subscriptions already handle the suspend/resume transition via the event router.

**Rationale:** Deleting and recreating the WDT is unnecessary overhead and introduces a window where the system is unprotected. A suspend flag with extended polling is simpler, safer, and integrates naturally with the existing event subscriptions.

### 6. App layer: delete duplicate declarations, add cleanup on failure

The duplicate `s_pairing_active` and `s_pairing_requested` declarations at lines 52–62 will be removed (keeping the second set at lines 60–61). Event subscription failures in `sdf_app_init()` will trigger cleanup of already-subscribed handlers before returning the error.

**Rationale:** Duplicate declarations are a latent bug — the first set of variables is never read after the second shadows them. Subscription cleanup prevents resource leaks on re-initialization.

### 7. Alarm mask guard: diff-based update

` sdf_app_set_alarm_mask_bits()` will compute the new mask and compare it to the old mask before calling `sdf_protocol_zigbee_update_alarm_mask()`.

**Rationale:** Zigbee alarm mask updates are sent over the Zigbee network and are unnecessary when the mask hasn't changed. This reduces Zigbee traffic and the associated power cost.

## Risks / Trade-offs

1. **Light-sleep helper extraction** — If the helper function grows too large (>60 lines), it may become harder to follow than the inline code. Mitigation: Keep the helper focused on sleep entry only; error handling can be minimal (log warnings, continue).

2. **Type-indexed event dispatch** — Adding new event types requires updating the array size. Mitigation: Use the enum count or a configured maximum; the array is small enough that this is trivial.

3. **Match task batched locking** — Holding the lock for the entire config-read + fingerprint-match cycle increases lock hold time. Mitigation: Release the lock before calling `fp_match_1n()` (which can take 12s) and only re-acquire for state updates. This is actually an improvement over the current 6 acquisitions.

4. **Suspend/resume via suspend flag** — The match task will continue polling at 10s intervals when suspended, consuming slightly more power than the current WDT-delete approach. Mitigation: The difference is negligible compared to the current approach's overhead, and the safety benefit (WDT running, no context-switch storm) outweighs the cost.

5. **Free-ID search optimization** — The O(count) scan assumes user IDs are roughly contiguous. If many user IDs are allocated with gaps, the scan could be less efficient than the current approach. Mitigation: For typical deployments (1–50 users), both approaches are effectively O(n) with small constants. The O(count) approach avoids scanning 4096 slots for every enrollment.

## Migration Plan

1. All changes are backward-compatible at the API level (no function signature changes for public APIs).
2. Changes can be deployed as a single firmware flash.
3. No data migration is required.
4. Rollback: flash the previous firmware binary.

## Open Questions

- Should `SDF_EVENT_ROUTER_POWER_BATTERY` be added as a new enum value, or should battery events be a sub-payload of the existing `POWER_SLEEP` event? The proposal adds a new enum value for clarity, but this does require updating the event router type enum.
- The `emit_async` priority bypass: should this be documented as intentional (for high-throughput, non-critical logging), or should it be fixed to respect priority? The proposal documents it as intentional for now.
- How aggressively should the match task poll interval be increased when suspended? The proposal suggests 10 seconds, but the power manager's check-in interval (default 15s) may be a more appropriate value.
## Context

See `proposal.md` — Why. The constraints that shape the approach:

- **Boot sequence.** `sdf_app_init()` calls `sdf_event_router_init()` at `sdf_app.c:1692`, registers its own 9 subscriptions at `:1702–1788`, then `sdf_services_init()` at `:1810` (which internally calls `sdf_services_start_tasks()` at `sdf_services.c:825`), then `sdf_ble_companion_init()` at `:1827`, then Zigbee/BLE/CLI. The last subscriber registers inside `sdf_ble_companion_init()`.
- **Self-registering tasks.** `sdf_services_match`, `_admin`, and `_enroll` currently subscribe from inside their own task bodies (`:245`, `:182`, `:268`). This is the only source of concurrent registration in the firmware and the reason the dispatch lock cannot simply be deleted.
- **Synchronous critical dispatch.** `PRIO_CRITICAL` events dispatch on the emitting task's context, so dispatch can run on any task, at any time, including during boot.
- **Two build targets.** ESP-IDF (device) and `CONFIG_IDF_TARGET_LINUX` (host test runner under `firmware/test_runner/build_linux`). Anything exotic — linker-section registration, custom `linker.lf` fragments — needs a second mechanism for the host build, which is why that option was rejected in exploration.
- **Existing router unit tests** subscribe in 4 cases and unsubscribe in 2, and `sdf_event_router_init()` early-returns when already initialized, so it never re-zeroes state between cases.

## Goals / Non-Goals

**Goals:**

- A subscriber table that is fully constructed before the first dispatch and immutable thereafter, so dispatch needs no lock, no snapshot, and no fan-out cap.
- Zero heap allocation in the router.
- Late registration becomes a loud, deterministic startup failure rather than a silent race.
- The router component keeps no compile-time knowledge of its subscribers.

**Non-Goals:**

- Changing dispatch ordering semantics. Ordering within a type is unspecified today and stays unspecified.
- Adding a deinit/restart path for the router. There is none today and this change makes the absence explicit rather than filling it in.
- Reworking `min_prio` ergonomics beyond documentation (see proposal — Out of scope).
- Touching `sdf_event_router_emit_nonblocking()` or the spec drift around it.

## Decisions

### D1: Flat static pool with per-type index chains

```
  s_pool[SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY]      s_head_by_type[TYPE_COUNT]
  ┌────┬──────────┬─────┬─────┬──────┐              ┌──────────────────┬─────┐
  │ #0 │ min_prio │ cb  │ ctx │ next │◀─────────────│ BIOMETRIC_MATCH  │  0  │
  ├────┼──────────┼─────┼─────┼──────┤              ├──────────────────┼─────┤
  │ #1 │ min_prio │ cb  │ ctx │ 0xFF │              │ POWER_WAKE       │  1  │
  ├────┼──────────┼─────┼─────┼──────┤              ├──────────────────┼─────┤
  │ #2 │  …       │     │     │      │              │ …                │0xFF │
  └────┴──────────┴─────┴─────┴──────┘              └──────────────────┴─────┘
        next = uint8_t index, 0xFF = end            0xFF = no subscribers
```

`type` and `min_prio` shrink to `uint8_t`, `next` is a `uint8_t` index rather than a pointer. At ~12 bytes per slot, a 32-slot pool is 384 B of `.bss` plus a 23-byte head table — *less* than the ~590 B of heap the current 21 `calloc(1, sizeof)` nodes consume once allocator overhead is counted, and it is deterministic.

*Alternatives considered.* A 2-D `[TYPE_COUNT][MAX_PER_TYPE]` array wastes ~1.5 KB for a table whose fullest type holds 3 subscribers. A flat array linearly scanned per dispatch is the simplest possible code and the scan cost is negligible at this event rate, but it makes dispatch cost proportional to *total* subscribers rather than matching ones, and the index chain costs only a few lines more.

### D2: Registration moves to service init, tasks keep only their queues

`sdf_services_init()` registers the match/enroll/admin subscriptions before `sdf_services_start_tasks()` creates the tasks. The `*_init_subscriptions()` helpers move out of the task bodies and are called from init; the `*_deinit_subscriptions()` helpers are deleted.

The `ctx` for these subscriptions is already a pointer to file-scope static state (`&s_match_state` and friends), so it is valid before the task exists — registration does not need the task. The one ordering detail: `state->event_queue` is assigned inside `sdf_match_task_init_subscriptions()` from `sdf_services_state()->match_task_queue`, so registration must run after the shared queues are created in `sdf_services_init()`.

Callbacks already guard on `state->event_queue != NULL` before `xQueueSend`. Task shutdown keeps setting `event_queue = NULL` as it unwinds — that null-check, not deregistration, is what makes a post-exit dispatch harmless (see the `sdf-services-tasks` spec delta).

### D3: `sdf_event_router_start()` is called immediately after the last subscriber, not at the end of init

Placed right after `sdf_ble_companion_init()` (`sdf_app.c:1827`), not at the end of `sdf_app_init()`.

Between `sdf_event_router_init()` and `start()` the dispatch task does not exist, so non-critical events accumulate in the router queue and are only drained once `start()` runs. Deferring `start()` to the end of `sdf_app_init()` would leave that queue absorbing events across Zigbee, BLE, and CLI init — slow, and a queue overflow there drops events silently. Starting as soon as the last subscriber has registered keeps the buffering window to the width of BLE companion init.

The cost of this placement is that a subsystem initialised later (Zigbee, CLI) cannot subscribe. That is deliberate: it now fails with `ESP_ERR_INVALID_STATE` and a log line instead of racing. If such a subscriber is ever needed, the fix is to register it earlier, not to move `start()`.

*Alternative considered.* Auto-starting on first emit removes the ordering question but makes the freeze point implicit and data-dependent — the worst property for a boot-order invariant.

### D4: Capacity is declared per component in a shared header

```c
/* sdf_event_router_capacity.h */
#define SDF_EVENT_ROUTER_SUBS_APP            9
#define SDF_EVENT_ROUTER_SUBS_SERVICES_MATCH 3
#define SDF_EVENT_ROUTER_SUBS_SERVICES_ADMIN 4
#define SDF_EVENT_ROUTER_SUBS_SERVICES_ENROLL 3
#define SDF_EVENT_ROUTER_SUBS_BLE_COMPANION  2
#define SDF_EVENT_ROUTER_SUBS_DECLARED_TOTAL (…sum…)   /* 21 */
```

The pool is sized from `SDF_EVENT_ROUTER_SUBS_DECLARED_TOTAL` plus a small headroom constant, with `static_assert` that the total fits the pool and that the pool size fits the `uint8_t` index type.

This cannot catch a developer who adds a subscription without bumping the count — no cross-translation-unit mechanism can, short of the linker-section approach rejected above. That residual gap is closed at runtime: rejected registrations increment a counter, and `sdf_event_router_start()` refuses to start if it is non-zero, logging registered-vs-capacity either way. A silently truncated subscriber list on a door lock becomes a boot failure, which is the correct direction to fail.

### D5: Tests get an explicit reset hook

`test_sdf_event_router.c` is rewritten around init → subscribe → start → emit. Because there is no unsubscribe and `init()` is idempotent, a test-only `sdf_event_router_reset_for_test()` (compiled under the host test target) clears the pool, head table, and started flag between cases. Exposing this only to the test build keeps the production API free of a teardown path nothing calls — the very thing this change removes.

## Risks / Trade-offs

- **A subscription ends up registered after `start()` and now hard-fails.** → This is the intended failure mode, but it must be found on hardware, not only in the host runner: verify the full boot log shows the expected registered count and no `INVALID_STATE` rejections. The registered-vs-capacity log line at start is the check.
- **Events emitted between `init()` and `start()` overflow the router queue and are dropped silently.** → D3 minimises the window. Worth confirming `event_router_queue_depth` covers the boot burst; if boot-time emissions approach the depth, raise it rather than moving `start()` later.
- **Critical events emitted during the registration window dispatch against a partially built table.** → Registration is single-threaded on the init context before any task exists, so the table is not *mutating* concurrently; but a critical emit at `sdf_services_init()` time reaches only the subscribers registered so far. Same exposure as today; specified explicitly rather than left implicit.
- **`sdf_services_stop_tasks()` and `sdf_ble_companion_deinit()` remain as dead code with their teardown weakened.** → Left in place to keep this change scoped; they were already uncalled. If they are ever revived, the frozen-table invariant means they cannot restore a clean subscriber state and will need redesign — worth a comment at both sites saying so.
- **Static capacity is reserved whether or not it is used.** → 384 B of `.bss` against ~590 B of heap freed. Net positive, and it removes a fragmentation source on a device that stays up for months.

## Migration Plan

Single atomic change — the router API and all call sites must move together, since removing `unsubscribe()` and the handle out-param breaks compilation everywhere at once.

1. Router internals and API (pool, `start()`, lock-free dispatch, capacity header).
2. Service task registration relocation, then `sdf_app` and `sdf_ble_companion` call sites, including the `SECURITY_LOCKOUT` `min_prio` fix.
3. Router and services tests.
4. Host test runner green, then hardware boot verification of the registered-vs-capacity log line and a lockout enter/expire cycle confirming the alarm clears.

Rollback is a straight revert; nothing here changes persisted state, NVS layout, or any external protocol.

## Open Questions

- Headroom above the declared total of 21 — 32 slots is proposed as a round number with room for roughly a third more subscribers. If `.bss` is tight, sizing the pool exactly at the declared total is equally valid and makes an un-bumped count fail at boot immediately rather than after headroom is consumed.

## Context

`sdf_event_router.c` exposes `emit()` and `emit_async()`. Both currently do exactly the same thing: dispatch synchronously for `PRIO_CRITICAL` events, otherwise queue for the router task with a 100 ms send timeout. This split was intentional at the time `emit()` was hardened (archived `event-router-indexing` change) to guarantee critical events can't be starved behind a full queue regardless of which entry point a caller uses — but it left `emit_async()` with no remaining reason to exist as a separate function. A repo-wide search confirms zero production call sites and zero test coverage for it.

Separately, `sdf_event_router_dispatch_sync()` validates `event->type < SDF_EVENT_ROUTER_TYPE_COUNT` before indexing `subscribers_by_type[]` (read side, added by the same archived change). `sdf_event_router_subscribe()` performs the same array index (`s_state.subscribers_by_type[sub->type] = sub`) with no equivalent check (write side). Every current call site passes a literal `sdf_event_router_type_t` enum constant, so this is not reachable today, but it's an unguarded out-of-bounds write, not just a read, if that ever changes.

## Goals / Non-Goals

**Goals:**
- Remove `emit_async()` as dead, duplicate API surface.
- Close the bounds-check gap on `subscribe()`'s write path so it matches `dispatch_sync()`'s read-path guard.
- Give the event router component a spec of its own, since none exists today (the archived `add-event-router` change's spec never made it into `openspec/specs/`, and `sdf-services-tasks/spec.md` documents payload shapes, not the API contract).

**Non-Goals:**
- No change to dispatch semantics, priority handling, locking, or the subscriber snapshot-then-invoke pattern in `dispatch_sync()` — none of that is in question here.
- No `sdf_event_router_deinit()`. Investigated during exploration: production never tears the router down at runtime, and the host test runner runs every suite in one process with a single `exit()` at the end — no test today calls `sdf_services_start_tasks()` or `sdf_ble_companion_init()` (the only subscribers) in-process, so there's no live leak to fix. Revisit if a future Linux-target test starts exercising either.
- No change to `sdf_event_router_emit()`'s behavior — it already has the correct CRITICAL-sync / else-queue split; `emit_async()` is what's being removed, not merged into `emit()` with new behavior.

## Decisions

**Delete `emit_async()` outright rather than keep it as a thin alias for `emit()`.** An alias would preserve source compatibility for hypothetical external callers, but there are none in this repo, and keeping a same-name-different-word alternative around invites exactly the confusion this change exists to remove ("why are there two functions that do the same thing?"). Alternatives considered: keep both, add a comment on `emit_async()` marking it as an alias — rejected, it doesn't answer why a caller would ever pick one over the other.

**Bounds check in `subscribe()` returns `ESP_ERR_INVALID_ARG`, folded into the existing `cb == NULL || handle == NULL` guard.** Matches the error code convention already used by that same guard, and by `sdf_event_router_unsubscribe()`'s `handle == NULL` check. Alternatives considered: `ESP_ERR_OUT_OF_RANGE` — rejected, not used elsewhere in this component or its siblings (`sdf_services`, `sdf_config` setters all use `ESP_ERR_INVALID_ARG` for bad-argument rejection).

**New capability `sdf-event-router` rather than modifying `sdf-services-tasks`.** `sdf-services-tasks/spec.md` documents event *payload* shapes as part of the services-task refactor narrative, not the router's own init/subscribe/unsubscribe/emit contract, and isn't in clean Requirement/Scenario form for that surface. A small, focused capability spec (matching the size/style of `ota-key-autogen`, `cli-console-build-gating`) is more useful going forward as the canonical reference for this component's API, and gives this and future event-router changes something to write deltas against.

## Risks / Trade-offs

- [Removing `emit_async()` is a **BREAKING** API change] → Mitigated: confirmed zero call sites via repo-wide search across firmware components and tests before proposing removal. No call-site migration needed.
- [Writing a new capability spec for existing behavior, not just the two changes, risks the spec drifting from implementation later if not kept in sync] → Same risk any spec carries; scoped narrowly (four public functions) to keep it cheap to keep current.

## Migration Plan

1. Remove `sdf_event_router_emit_async()` declaration from `sdf_event_router.h` and definition from `sdf_event_router.c`.
2. Add the bounds check to `sdf_event_router_subscribe()`.
3. Add a test case asserting `subscribe()` rejects `type >= SDF_EVENT_ROUTER_TYPE_COUNT` with `ESP_ERR_INVALID_ARG`.
4. Build `test_runner` for `IDF_TARGET=linux`, confirm all suites still pass.
5. Rollback is a plain revert — no data migration, no persisted state affected.

## Open Questions

None outstanding — the deinit question raised during exploration is resolved as a Non-Goal above, not deferred.

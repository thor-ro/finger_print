## Why

`sdf_event_router_emit_async()` has been byte-for-byte identical to `sdf_event_router_emit()` since the priority-aware dispatch fix landed (both now route `PRIO_CRITICAL` events through synchronous dispatch and queue everything else) — it has zero production call sites and zero test coverage, so the "async" name promises a fire-and-forget semantic the implementation doesn't provide. Separately, `sdf_event_router_dispatch_sync()` bounds-checks `event->type` before indexing `subscribers_by_type[]` (read side), but `sdf_event_router_subscribe()` performs the same array index with no equivalent check (write side) — every current caller passes a literal enum value so this is latent, not exploited, but it's the more dangerous half of the pair to leave unguarded.

## What Changes

- **BREAKING**: Remove `sdf_event_router_emit_async()` from the public API (`sdf_event_router.h`) and its implementation (`sdf_event_router.c`). No production callers exist; this is dead, duplicate code.
- Add a `type >= SDF_EVENT_ROUTER_TYPE_COUNT` bounds check to `sdf_event_router_subscribe()`, returning `ESP_ERR_INVALID_ARG`, matching the guard `sdf_event_router_dispatch_sync()` already applies on the read path.

## Capabilities

### New Capabilities
- `sdf-event-router`: Public API contract for the event router component (init/subscribe/unsubscribe/emit) — captures the current, correct behavior (including this change's two fixes) since no capability spec exists for this component today.

### Modified Capabilities
(none — no existing spec documents the event router's API contract; see New Capabilities)

## Impact

- `firmware/components/sdf_event_router/include/sdf_event_router.h` — remove `sdf_event_router_emit_async()` declaration.
- `firmware/components/sdf_event_router/src/sdf_event_router.c` — remove `sdf_event_router_emit_async()` definition; add bounds check to `sdf_event_router_subscribe()`.
- `firmware/components/sdf_event_router/test/test_sdf_event_router.c` — add a test asserting out-of-range `type` is rejected by `subscribe()`.
- No other files reference `sdf_event_router_emit_async()` (confirmed via repo-wide search of production and test code), so no call-site changes are needed elsewhere despite the **BREAKING** API removal.

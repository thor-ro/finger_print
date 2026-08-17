## Context

See `proposal.md` — Why. Constraints that shape the approach:

- The router task (`sdf_evt_router`) runs at FreeRTOS priority 5, the same priority as `sdf_match` and `sdf_admin`, above `sdf_enroll` (4). Moving dispatch off emitter stacks therefore costs at most one tick of scheduling latency for the tasks that emit, not a priority inversion.
- Queue depth is `CONFIG_SDF_EVENT_ROUTER_QUEUE_DEPTH = 32`, validated by `sdf_config.c:301` to stay within 8–64. Each slot is a `sdf_event_router_event_t` by value (~28 B).
- Exactly one production call site emits `PRIO_CRITICAL`: lockout entered, `sdf_services_match.c:226`. Its rate is bounded by the failed-attempt threshold times the per-attempt cooldown — human-scale, not burst.
- There are ~17 production `sdf_event_router_emit()` call sites across 8 files, plus 31 in tests. The subscriber table, capacity accounting, and init/subscribe/start lifecycle are untouched.

## Goals / Non-Goals

**Goals:**

- Subscriber callbacks execute only on the router task, for every priority.
- One emit entry point whose blocking behaviour is visible at the call site.
- Make the API change compiler-enforced rather than silently source-compatible, so no call site is missed.

**Non-Goals:**

- Reworking the priority enum, the `min_prio >= event->priority` filter, or the subscriber pool.
- Guaranteeing delivery of `PRIO_CRITICAL` events under queue exhaustion (see Risks).
- Resolving the `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` two-priority split (see Risks) — that is a security-model decision, not an emit-API decision.
- Fixing the unlocked read-modify-write on `s_zigbee_alarm_mask` in `sdf_app_set_alarm_mask_bits()`. This change removes one concurrent writer (the match task) but the lock-flow callbacks still race it; the underlying data race is pre-existing and separately scoped.

## Decisions

**Enqueue `PRIO_CRITICAL` at the front of the existing queue rather than adding a second high-priority queue.** `xQueueSendToFront()` gives critical events precedence over pending work using the queue the router already owns and the receive loop already drains. A second queue would require the dispatch task to wait on both — either a queue set (extra RAM, extra ESP-IDF surface) or a poll loop (latency and CPU) — to buy precedence the single queue already provides. Alternative considered: plain FIFO with no precedence at all. Rejected because it discards the one property the synchronous path was originally introduced to guarantee, and front-insertion costs nothing to keep.

**Express blocking as `uint32_t send_timeout_ms` on `emit()` rather than a `bool sync` flag or a retained second function.** The dimension callers actually vary is how long they may block; `sdf_services_button.c`'s GPIO callbacks need zero, everything else tolerates 100 ms. A `bool sync` would leave callbacks on emitter stacks — the actual defect — while making the hazard opt-in rather than removing it, and booleans at call sites are exactly the argument that gets copy-pasted wrong. Keeping `emit_nonblocking()` was rejected because the `sdf-event-router` spec already forbids a second entry point, and the function's existence is why that requirement was being violated unnoticed.

**Change the signature rather than add an overload-style wrapper.** Adding a parameter breaks every call site at compile time, which is the desired behaviour: each of the ~17 production sites gets looked at once and given an explicit timeout. A wrapper preserving the old one-argument form would let sites keep an invisible 100 ms block, reproducing the problem the change exists to make visible.

**Rename `sdf_event_router_dispatch_sync()` to `sdf_event_router_dispatch()` and keep it `static`.** "sync" described its relationship to `emit()`, which no longer exists. It retains its event-type validation: events arrive from the queue, which does not re-validate, and the router task is the last place to catch a malformed type before indexing `head_by_type[]`.

**No reserved queue slots or separate capacity for `PRIO_CRITICAL`.** With depth 32, a single critical producer at human scale, and every emitter now able to specify its own send timeout, reserving slots adds accounting for a scenario that requires the router task to be starved while 32 events pile up. Alternative considered: refuse non-critical enqueue above a high-water mark to keep headroom. Rejected as premature — it trades a guaranteed drop of low-priority events for a hypothetical drop of critical ones. Revisit if boot-log queue-full warnings ever appear.

## Risks / Trade-offs

- [`PRIO_CRITICAL` events become droppable — a full queue previously could not drop them because they bypassed it entirely] → Mitigated by front-of-queue precedence (a critical event only fails if all 32 slots are occupied at the instant of emit, not merely if the queue is busy) and by the caller choosing its own timeout. The spec now states this explicitly rather than implying critical delivery is guaranteed. `emit()` already returns `ESP_ERR_NO_MEM` and logs on failure; the lockout call site currently ignores the return value, which stays as-is under this change.

- [Multiple `PRIO_CRITICAL` events emitted back-to-back dispatch in reverse order, since each is pushed to the front] → Not reachable today: there is one critical producer and one critical event type. Flagged rather than solved because solving it means scanning the queue on insert, which is not worth it for a case that does not exist. If a second critical producer is ever added, this must be revisited.

- [`SDF_EVENT_ROUTER_SECURITY_LOCKOUT` is emitted at CRITICAL when entered (`sdf_services_match.c:226`) and NORMAL when cleared (`sdf_services_match.c:108`), so an entered event can still overtake a queued cleared event and leave the Zigbee alarm bit set when it should be clear] → Narrowed but not eliminated by this change: previously the sync path let entered overtake cleared by unbounded stack precedence, now it is bounded to queue reordering. The window remains gated by the failed-attempt threshold plus cooldown, which is orders of magnitude larger than queue drain time. Collapsing the type to one priority is the structural fix and is deliberately deferred — it changes `sdf_app.c:1722`'s subscription rationale and touches `openspec/specs/security-event-emission/spec.md`, which pins both priorities in scenario text.

- [`PRIO_CRITICAL` emitted between `init()` and `start()` is now deferred rather than delivered immediately] → Behaviourally correct (the spec already permitted queueing in that window for other priorities) and no production site emits CRITICAL before `start()`. Verify during implementation that no boot path depends on the old immediacy.

- [~17 mechanical call-site edits risk a wrong timeout being pasted] → The default is `100` everywhere except the two `sdf_services_button.c` sites, which take `0`. The compiler catches omissions; a reviewer only needs to confirm that exactly two sites use `0`.

## Migration Plan

Single atomic change — the signature break means the tree does not build in an intermediate state, so there is no staged rollout. Order: router header and implementation first, then call sites (compiler-driven), then tests. Rollback is a revert; no persisted state, NVS layout, or wire protocol is touched.

Verification beyond unit tests: run under `esp-emu` and confirm boot completes with the event router's `started: N/M subscribers registered` line, then exercise the lockout path and confirm the Zigbee alarm mask still updates. Prior boot panics under the emulator on this project have reproduced on hardware, so an emulator panic here is a blocker, not a fidelity artifact.

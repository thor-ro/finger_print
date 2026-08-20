## Context

See `proposal.md` — Why. Constraints that shape the approach:

- The subscriber table is frozen before dispatch begins, and dispatch runs on exactly one task (`sdf_evt_router`, priority 5, 3072-byte stack). Only that task calls `xQueueReceive()` on the router queue; every other participant is a producer. This is what makes an emit from dispatch a guaranteed-failure wait rather than merely a contended one.
- Ten of the twenty-one subscriptions already follow the trampoline pattern — `xQueueSend(own_queue, &copy, 0)` and return, as in `sdf_services_match.c`, `_admin.c`, `_enroll.c`. The pattern, the queue depth (10), and the zero timeout are all established; `sdf_app` is adopting them, not inventing them.
- `sdf_app_init()` registers all nine `sdf_app` subscriptions and then calls `sdf_event_router_start()` at `sdf_app.c:1840`, which freezes the table. The comment there already asserts the 9/4/3/3/2 split; the task must be created inside that window.
- `sdf_app` has no start/stop lifecycle — `sdf_app_init()` is its only entry point, unlike `sdf_services_start_tasks()` / `_stop_tasks()`. There is no cooperative shutdown to hook into and none is being invented.
- The TWDT is reconfigured in `sdf_app_init()` (`sdf_app.c:1656`) with `idle_core_mask = 0` and `trigger_panic = true`, deliberately: only tasks that subscribe via `sdf_platform_time_wdt_add()` are watched. Adding a task that does not subscribe adds no coverage.
- `sdf_protocol_zigbee_update_alarm_mask()` is already non-blocking and coalescing (see `zigbee-attribute-reporting`). The alarm-mask race is entirely on the caller side, in the mask `sdf_app` composes before calling it.
- The host test target (`CONFIG_IDF_TARGET_LINUX`) runs the router with a real FreeRTOS-on-POSIX task, and `sdf_event_router_reset_for_test()` tears it down between cases.

## Goals / Non-Goals

**Goals:**
- Remove every instance of application work executing on the router's dispatch task, in one structural move rather than case by case.
- Make emit-from-dispatch impossible to do by accident, detectable in a unit test, and loud in logs if attempted.
- Make the alarm-mask read-modify-write atomic without introducing a wait on any caller.
- Set the new task's stack from a measurement, not an estimate.

**Non-Goals:**
- Enforcing "no long-running work in a callback" generally. The ban is a runtime check on one function; callback *duration* cannot be enforced the same way. A dispatch-duration watchdog is sketched under Open Questions but is not part of this change.
- Changing the audit transport. `SDF_EVENT_ROUTER_AUDIT` and `sdf_app_emit_audit()` are untouched — see `proposal.md` — Unchanged Capabilities.
- Changing `SDF_EVENT_ROUTER_EMIT_TIMEOUT_DEFAULT_MS` or any caller's timeout argument. With emit-from-dispatch banned, the 100 ms default is an ordinary producer-side timeout again.
- Reducing `SDF_EVENT_ROUTER_TASK_STACK` once the deep path leaves the dispatch task. It becomes over-provisioned and that is a follow-on with the probe's numbers as input, not a change to make blind in the same pass that adds a new stack.

## Decisions

### D1: One `sdf_app` task, not a fast task plus a slow worker

```
   TODAY                          CHOSEN (one task)          REJECTED (split)
   ─────                          ─────────────────          ────────────────
   sdf_evt_router                 sdf_evt_router             sdf_evt_router
    ├─ on_event ──┐                └─ all 9 subs               ├─ 8 subs ───▶ sdf_app (P5)
    │   unlatch   │  all share         trampoline               │             unlatch, audit,
    │   audit ×6  │  one context         │                      │             alarm, admin
    ├─ web_reg ───┤                      ▼                      └─ 1 sub ────▶ sdf_app_slow (P4)
    │   PBKDF2    │                  sdf_app (P5)                              web reg:
    │   NVS write │                   unlatch can queue                        PBKDF2, NVS
    └─ admin_cplt ┘                   behind PBKDF2
```

Both shapes free the router completely. They differ only in whether an unlatch can queue behind a web-registration PBKDF2 — roughly 100-300 ms for 10,000 iterations on this part.

**One task.** The exposed concurrency is narrow and the cost is proportionally small:

- A `WEB_REG_AUTH_RESULT` arrives only moments after an admin presented a fingerprint to authorize a web registration. A second person unlatching inside that window is rare.
- `sdf_app_lock_action()` → `sdf_lock_flow_begin()` only *starts* a Nuki BLE challenge/response handshake that itself runs hundreds of milliseconds. Adding up to 300 ms to a multi-hundred-millisecond path is not the difference between working and not.
- The split costs a second task stack sized for PBKDF2 plus NVS — the expensive one — to protect a rare case, on a part that already runs nine tasks.

Revisit if the probe shows PBKDF2 costs materially more than estimated, or if field logs show app-queue drops correlated with web registration. Splitting later is cheap: the slow handler is one function with one subscription, so the change is repointing that subscription and adding a second queue.

### D2: The services task model — bounded wait plus watchdog — not the router's

| | Router model (`sdf_evt_router`) | Services model (`sdf_match`, `sdf_enroll`, `sdf_admin`) |
|---|---|---|
| Wait | `portMAX_DELAY` | bounded, `*_IDLE_WAIT_CAP_MS` = 1000 |
| Watchdog | not registered | `sdf_platform_time_wdt_add()`, reset each iteration |
| Light sleep | never wakes on its own | wakes at the cap |
| Wedged task | silent | panics and reboots |

**Services model.** The app task will own the unlatch path. `sdf-services-tasks` already requires watchdog participation for every long-running service task, and the requirement text calls out exactly this failure: a wedged task that stops servicing its work "with no reboot and no diagnostic, which is indistinguishable from a hardware button fault". The app task can wedge — it inherits the `portMAX_DELAY` semaphore take in `sdf_ble_companion_reply_auth()` that this change does not fix — and a door lock that silently stops unlatching is the worst version of that failure.

The 1000 ms wait cap is already accepted practice in this codebase and `sdf-services-tasks` explicitly permits it against the light-sleep constraint ("no service task in this capability wakes on a sub-second fixed interval"). One more task waking at 1 Hz matches three that already do.

`sdf_app` has no stop path, so the deregistration half of the services watchdog contract does not apply: the task runs from `sdf_app_init()` until reboot.

### D3: One trampoline callback for all nine subscriptions

The three current subscriber functions differ only in which handler body runs. Registering one trampoline for all nine subscriptions and switching on `event->type` in the task loop keeps the subscription count at nine — no capacity change, no `sdf_event_router_capacity.h` edit, no startup-count risk — and puts the type switch in one place instead of two.

The existing handler bodies move into the task loop unchanged in logic. `sdf_app_on_event()` already switches on `event->type`; it becomes the task-side handler for its seven types rather than a callback.

### D4: The trampoline preserves CRITICAL ordering

The router orders its own queue by priority: `xQueueSendToFront()` for `PRIO_CRITICAL`, `xQueueSendToBack()` otherwise. A plain FIFO app queue would discard that on the second hop — a `SECURITY_LOCKOUT` emitted at `PRIO_CRITICAL` could sit behind a `NORMAL` enrollment-step event that the router had already ranked below it.

The trampoline mirrors the router: `xQueueSendToFront()` when `event->priority == SDF_EVENT_ROUTER_PRIO_CRITICAL`, `xQueueSendToBack()` otherwise, both with timeout 0. It is three lines and it preserves a property the system already establishes rather than silently dropping it at the component boundary.

The service trampolines do not do this. That is a defect in them, not a precedent to copy — but it is out of scope here and only a follow-on note.

### D5: Zero timeout on the trampoline is what keeps the two-queue cycle safe

Under this change there is a cycle in the queue graph, and it is worth being explicit about why it cannot deadlock:

```
      app task ──emit(AUDIT, 100 ms)──▶  router queue  ──dispatch──▶  trampoline
         ▲                                    │                          │
         │                                    │ consumer: router task    │ xQueueSend(..., 0)
         └────────────  app queue  ◀──────────┴──────────────────────────┘
                     consumer: app task
```

- The app task may block up to 100 ms pushing onto the router queue. That wait is **satisfiable**: the router task is a different task and is free to drain.
- The router task never blocks pushing onto the app queue, because the trampoline passes timeout 0. It drops and moves on.

A cycle is only a deadlock if every edge can block. This one has a non-blocking edge by construction, and the non-blocking edge is on the router side — the side whose stalling would starve every other subscriber. **The zero timeout in the trampoline is load-bearing, not stylistic.** Any future change that gives it a non-zero timeout reintroduces the possibility of a cycle in which both parties wait.

Note the resulting cost, which is real: an audit event now travels app task → router queue → app queue → app task to reach a log line and a counter in the module that produced it, and it occupies a slot in the app queue that could hold an unlatch. See Open Questions.

### D6: Ban emit from dispatch with a dispatch-scoped flag, rejecting rather than coercing

Set a `bool in_dispatch` in the router's static state around the `sdf_event_router_dispatch()` call in the task loop; `sdf_event_router_emit()` rejects when it is set **and** the caller is the dispatch task.

Both conjuncts are required. The flag alone would reject legitimate emits from producer tasks that happen to run while a dispatch is in flight — a common case, not an edge case. Two ways to add the second conjunct:

1. `in_dispatch && xTaskGetCurrentTaskHandle() == s_state.task` — pays the handle lookup only on the rare path where the flag is already set.
2. Store the dispatching handle instead of a bool: `s_state.dispatch_ctx = xTaskGetCurrentTaskHandle()` on entry, `NULL` on exit; reject when it equals the caller.

**Take option 1.** The common case short-circuits on a bool load; option 2 pays a handle lookup on every emit. The flag is written only by the dispatch task, and a stale `false` read on another task is the correct answer for a producer anyway, so no synchronization is needed on it.

**Reject, do not coerce the timeout to zero.** Coercing would remove the stall but keep the silent drop, and would leave the pattern in the codebase for the next subscriber to copy. `ESP_ERR_INVALID_STATE` plus an `ESP_LOGE` naming the event type and priority makes the violation visible in boot and emulator logs and assertable in a host test.

After D3 lands there is no caller to break. That is the point: the guard is installed against a clean codebase, so its first firing is a genuine regression rather than a known-issue log line.

### D7: Alarm mask as an atomic compare-exchange loop

`sdf_app_set_alarm_mask_bits(set, clear)` becomes a CAS loop over an `_Atomic uint16_t`:

```
do {
    old = atomic_load(&s_zigbee_alarm_mask);
    new = (old | set) & ~clear;
    if (new == old) return;               /* no change, nothing to push */
} while (!atomic_compare_exchange_weak(&s_zigbee_alarm_mask, &old, new));
sdf_protocol_zigbee_update_alarm_mask(new);   /* outside the loop */
```

Rationale over a mutex: the function is still called from the NimBLE host task via `sdf_app_on_message()`, which may not block, and a mutex would nest under that caller's existing lock context. The CAS is wait-free in practice — bounded contention from at most three producers, none of which can be preempted indefinitely while holding anything.

Moving the `sdf_app_on_event()` call sites onto the app task removes one *pair* of racing contexts, not the race: app task versus NimBLE host task remains, and so does the lock-flow callback context.

The redundant-update suppression (`new == old`) must be **inside** the loop, evaluated on the same load that composed `new`. Hoisting it outside reintroduces the race in a subtler form: a producer could observe a value a concurrent update has already superseded and suppress a real change.

`sdf_protocol_zigbee_update_alarm_mask()` is called after the CAS succeeds, not under it. Two concurrent producers may therefore call it out of CAS order — harmless, because that component coalesces to the latest recorded value and both callers pass a mask that already includes the other's bits. The *values* are monotonically consistent even if the *calls* interleave. The `sdf_protocol_zigbee_is_enabled()` gate stays where it is, outside the CAS.

### D8: The probe runs first and sizes the new stack

Under the earlier shape of this change the stack probe was an audit of an existing 3072-byte allocation and could run last. It cannot now: its output is `SDF_APP_TASK_STACK`, an input to the code being written. It runs **first, against current `main`**, where the deep path still executes on the router task and the measurement is therefore of the same call graph in its current form.

The deep path — `WEB_REG_AUTH_RESULT` → 10k-iteration PBKDF2 → NVS write → BLE lock — is unreachable organically under `esp-emu`. Per findings recorded in `archive/2026-08-17-guard-ble-gatt-scratch-ownership` task 7.4 and `archive/2026-08-19-remove-dead-protocol-adapter-events` task 3.4: no fingerprint sensor is modelled, no GPIO button press can be injected, the device advertises allow-list-filtered with an empty list so no central can connect, and Zigbee steering fails.

Both halves have public entry points, so instrumentation can reach it:

```
  main task (temporary instrumentation, reverted before merge)
     │
     ├─ sdf_services_set_web_reg_auth("probe", hash, permission)
     │        seeds web_reg_auth_pending so the handler does not
     │        early-return at its sdf_services_get_web_reg_auth() guard
     │
     └─ sdf_event_router_emit({ .type = WEB_REG_AUTH_RESULT,
                                .priority = PRIO_HIGH,
                                .payload.web_reg_auth_result.authorized = true }, 100)
              │
              ▼
        sdf_evt_router task  (pre-change)  /  sdf_app task  (post-change)
              └─ sdf_app_on_web_reg_auth_result()
                    ├─ services lock ×2
                    ├─ PBKDF2-HMAC-SHA256 × 10000     ← deepest frames
                    ├─ sdf_storage_web_user_load() ×≤5 + _save()   ← NVS write
                    └─ sdf_ble_companion_reply_auth() → ESP_ERR_NOT_FOUND
                          (no connected authenticated peer under the emulator;
                           tail truncated here — see Risks)
```

`authorized = true` is required — the `false` branch skips PBKDF2 entirely and measures nothing.

Measurement: `uxTaskGetStackHighWaterMark(NULL)` logged at the end of each `sdf_event_router_dispatch()` call, tagged with the dispatched event type. Sampling at dispatch boundaries rather than periodically matters — the high-water mark is monotonic per task, so logging it per dispatch is what attributes the minimum to a specific event type. Baseline after boot settles, then inject, then compare; the delta is the number of interest.

`SDF_APP_TASK_STACK` is then set to the measured depth of the deep path plus the margin the project already applies elsewhere, and the figure and its derivation are recorded in `doc/rtos_tasks.md` rather than only in a commit message.

## Risks / Trade-offs

- **The probe's tail is truncated.** `sdf_ble_companion_reply_auth()` returns `ESP_ERR_NOT_FOUND` before `sdf_ble_companion_set_authenticated()` and the GATT notify, so the measured mark omits those frames. → PBKDF2 and the NVS write are the deep frames and both execute; treat the result as a **lower bound** on worst-case depth and size the stack accordingly, with the truncation recorded next to the number. A lower bound is a sound basis for a floor, which is what a stack size is.
- **A new task costs stack that is never reclaimed.** The app task's stack must cover PBKDF2, so it is one of the larger ones. → It is a transfer, not an addition: the same frames run on the router's stack today. The router becomes over-provisioned by roughly the same amount, and reclaiming that is the follow-on named under Non-Goals. Net RAM after both steps should be near neutral.
- **Events can now be dropped between the router and the app task.** The trampoline's zero timeout means a full app queue discards the event. Today the handler always runs. → This is the same trade the three service tasks already make, and D5 explains why the alternative is worse. Mitigation is a logged, counted drop so the condition is diagnosable rather than silent, plus a queue depth chosen against the observed burst size. A dropped `BIOMETRIC_MATCH` is a failed unlatch the user will retry; a stalled dispatch task is every subsystem failing at once.
- **The audit round-trip gets longer, not shorter.** Two queue hops for a log line and a counter, competing with unlatch for app-queue slots. → Accepted deliberately as the price of leaving `security-event-emission` intact; see Open Questions for the lever if it proves to matter.
- **Handler bodies move to a new execution context.** Anything in them that implicitly depended on running under the dispatch task — reentrancy assumptions, incidental serialization against other subscribers — changes meaning. → The alarm-mask CAS in D7 is one such dependency and is handled. The task list includes an explicit audit of the moved bodies for others rather than assuming this is the only one.
- **`sdf_app` gains a task but no shutdown path.** It cannot be stopped, so the watchdog-deregistration half of the services contract is untestable for it. → Stated as a scoped exception in the new capability rather than left as an implicit gap.
- **The ban is enforced only for `emit`.** A subscriber can still block dispatch by other means, and after this change `sdf_ble_companion`'s two handlers still do. → Explicitly a non-goal; tracked as a follow-on.

## Migration Plan

1. **Probe** (D8), against current `main`. Produces `SDF_APP_TASK_STACK`. Instrumentation reverted before anything else lands.
2. **App task and trampoline** (D1-D4). Firmware is fully working at each end of this step; after it, no emit occurs from dispatch.
3. **Ban** (D6) plus host tests. A no-op in production, which is the point — if it is not a no-op, step 2 was incomplete and the log says so.
4. **Alarm mask CAS** (D7). Independent of 2 and 3; can land in any order relative to them.
5. **`doc/rtos_tasks.md`** (drift repair plus the new task), after step 2 fixes the numbers it must record.

Steps 2 and 3 are ordered: installing the guard before the trampoline would log an error on every biometric match in between. Rollback: step 3 is a safe standalone revert at any time; step 4 is independent; step 2 reverts to the current behaviour with no API change, since no producer signature changes anywhere in this change.

## Open Questions

- **Should the audit transport be revisited after all?** D5 notes audit now round-trips app task → router → app task, and competes with unlatch for app-queue slots. Collapsing it to a direct call would fix that but reverses `security-event-emission`, the deliverable of the archived `security-events-unify` change. Not proposed here — but if the drop counter added in step 2 shows audit events crowding the app queue, that is evidence, and reversing a specified decision on evidence is a different proposition from reversing it on argument. Deliberately left for data.
- **Should `SDF_EVENT_ROUTER_TASK_STACK` come down?** The probe measures the router carrying the deep path; after this change it no longer does. The follow-on is a re-measure and a reduction, not a guess. Named under Non-Goals so it is not silently forgotten.
- **Should dispatch carry a duration watchdog?** Timing each `slot->cb()` with `esp_timer_get_time()` and `ESP_LOGW`-ing past a budget would make "no long work in dispatch" observable without being enforceable. It does not affect this change's specs, approach, or tasks. Most naturally lands with the `sdf_ble_companion` follow-on, which is the remaining offender it would catch.
- **Do the service trampolines need the CRITICAL ordering from D4?** They have the same second-hop reordering. Not fixed here — three components, three queues, and no evidence yet that it bites. Worth a look when someone next touches them.

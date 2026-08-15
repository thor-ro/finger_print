## Context

The watchdog abstraction in `sdf_platform` is not merely unused — it is unlinked, and one
of its dependencies does not exist. Verified against the current tree:

```
$ nm .../sdf_platform_time.c.o          # IDF_TARGET=linux object
                 U _esp_timer_get_time_mock     ← undefined
0000000000000244 T _sdf_platform_time_wdt_reset  ← empty body on linux
0000000000000258 T _sdf_platform_time_wdt_feed

$ nm test_runner/build_linux/sdf_test_runner.elf | grep -i "platform_time_wdt|timer_get_time_mock"
(nothing)                                ← object never pulled into the link
```

`sdf_mock_linux_time.h` declares `esp_timer_get_time_mock()`, `esp_task_wdt_reset_mock()`
and `esp_task_wdt_reconfigure_mock()`, and macro-renames the real IDF symbols onto them
(lines 15-17). **None of the three is implemented anywhere in the tree.** The host build
survives only because nothing references `sdf_platform_time.c`'s symbols, so the linker
drops the whole object and never resolves `esp_timer_get_time_mock`.

The consequence is sharp and affects the *predecessor* change as much as this one: **the
first caller to invoke any `sdf_platform_time_*` function from host-target code turns
`esp_timer_get_time_mock` into a link error.** `register-admin-task-watchdog` is that
first caller. Its task 2.3 (observable Linux registration state) cannot land without
implementing the missing mock, and that is not currently written down in its tasks. Fixing
it is cheap — `sdf_platform_time.c:13` is the only consumer — but it must be discovered
before implementation starts, not during.

The second constraint is the macro-override style itself. `sdf_mock_linux_time.h` redirects
`esp_task_wdt_reset` to a mock via `#define`. That is a *different* portability strategy
from the one `sdf_platform_time.c` actually uses for the same symbol three lines later
(`#ifndef CONFIG_IDF_TARGET_LINUX` around the call). Both mechanisms are present, aimed at
the same function, and neither is reachable. Any migration has to pick one.

## Goals / Non-Goals

**Goals:**

- No file under `sdf_services/` references `esp_task_wdt` directly.
- One place in the tree owns the `#ifndef CONFIG_IDF_TARGET_LINUX` decision for watchdog
  calls.
- An `esp_task_wdt_reset()` from an unregistered task is diagnosable rather than silent.
- Leave the `sdf_platform_time` host story coherent: no declared-but-unimplemented mocks,
  no unreachable macro overrides.

**Non-Goals:**

- Migrating `fingerprint.c`, `sdf_power.c` and `sdf_nuki_crypto.c`. Sketched as separable
  task group 5.
- The `sdf_task_bus_attach()` helper for the shared subscribe + queue-create + attach
  sequence. Its case is weaker than it looks: with the task count fixed at three, and the
  three attach sequences differing for real reasons (match's ISR-visible shared queue
  handle in `s->match_task_queue`, enroll's `esp_timer` retry timer, admin's computed
  `wait_ms`), a helper papering over those differences risks introducing a fourth variant
  of the bug it is meant to prevent. The watchdog calls are the part that was genuinely
  identical and genuinely caused defects; that is what this change consolidates.
- Changing what any task does, when it resets, or the TWDT configuration.

## Decisions

### Keep `#ifndef` inside the wrapper; delete the macro-override path

`sdf_mock_linux_time.h`'s `#define esp_task_wdt_reset esp_task_wdt_reset_mock` is the more
elegant mechanism in the abstract — it would let the wrapper body stay target-agnostic and
let host tests observe calls. But it is unimplemented, unreachable, and applies only inside
the single translation unit that includes the header, so it cannot generalise to the
service tasks. Adopting it would mean implementing three mocks and threading the header
into `sdf_services`, which spreads the mock surface rather than concentrating it.

Keep the established `#ifndef`-inside-the-function shape that `sdf_platform_time_wdt_reset()`
already uses. Drop the two dead `esp_task_wdt_*` macros and declarations from
`sdf_mock_linux_time.h` rather than leaving a second, unreachable strategy in place for a
future reader to adopt by mistake. Implement `esp_timer_get_time_mock()`, which is load-
bearing, and keep it.

### The not-found diagnostic is the point of centralising, not a bonus

Consolidating identical three-line blocks saves nothing by itself. The reason to route
every call through one function is that one function can then answer a question no call
site can: *was the caller actually registered?*

`esp_task_wdt_reset()` returns `ESP_ERR_NOT_FOUND` for an unsubscribed task. Every call
site in the tree discards it. `sdf_platform_time_wdt_reset()` should log once per task
handle, at warning level, naming the task — then stay quiet, because these calls sit in
hot loops (`fp_wait_for_reply()` runs one per second for the life of a blocked operation)
and an unrated log would be worse than the silence it replaces.

Rejected alternative: change the signature to return `esp_err_t` and make callers check.
That pushes the decision back to the call sites that have demonstrably ignored it twice,
and would ripple through every adopter for no gain over a one-shot log.

### Migrate match and enroll mechanically, in one commit, with no behaviour change

Both files' watchdog calls map one-to-one onto wrapper calls with identical placement.
This is deliberately a pure substitution: no reordering, no changed cadence, no
consolidation of `sdf_services_match.c`'s two extra `esp_task_wdt_reset()` calls (lines 282
and 308) into the loop. Anything beyond substitution belongs in a separate change where it
can be reviewed as a behaviour change rather than hidden inside a refactor.

## Risks / Trade-offs

**The host link error is the real risk, and it lands on the predecessor.** As above,
`register-admin-task-watchdog` becomes the first host-target caller of
`sdf_platform_time_*`, which turns the unresolved `esp_timer_get_time_mock` into a link
failure. If that change is implemented without knowing this, it will present as a confusing
test-runner link break far from anything it touched. Task 0.1 below records it; the same
note has been added to that change's tasks.

**The diagnostic is only as good as its trigger.** Logging `ESP_ERR_NOT_FOUND` once per
task helps only if an unregistered caller actually reaches a reset call. After
`register-admin-task-watchdog`, every `sdf_services` task is registered, so within this
change's scope the diagnostic should never fire — its value is entirely for future callers
and for the out-of-scope components in task group 5, where `fp_wait_for_reply()` is invoked
by arbitrary tasks. That is an argument for pulling group 5 in, not for dropping the
diagnostic.

**Temporary inconsistency shifts rather than disappears.** After this change,
`sdf_services` is uniform but `fingerprint.c`, `sdf_power.c` and `sdf_nuki_crypto.c` still
hand-roll the pattern — the same hazard, in components this change does not touch. Scoping
to `sdf_services` keeps the diff reviewable and matches where the defects were found, but
it does not eliminate the class.

**Pure refactors can still break placement.** The mapping is mechanical, but
`sdf_services_match.c`'s watchdog calls are load-bearing in ways its comments explain at
length (the bounded 100 ms queue wait at line 357 exists specifically so the loop returns
to the reset). Review should confirm placement is preserved exactly, which is why no
cleanup is bundled in.

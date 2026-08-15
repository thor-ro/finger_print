# Design: Zigbee commissioning state and idle watchdog scope

## Context

These three defects were found together because they were found in order: each
one had been hidden behind the one before it. The BLE bond-seed panic at ~1 s
(`fix-ble-bond-seed-init-order`) meant the device never reached the Zigbee
stack. With that fixed, the device reached the stack and rebooted at ~18 s from
the idle watchdog. With that fixed, it stayed up long enough for the steering
retry storm and the collapsed `if/else` to become observable.

```
   boot   ~1s                    ~18s                        100s+
    │      │                       │                            │
    ├──────X                       ·                            ·   BLE panic
    ├──────────────────────────────X                            ·   idle TWDT
    ├────────────────────────────────────────────────────────────   steering
                                                                     storm +
                                                                     if/else
```

## Decision 1: Restore the `else`, and put the join state in it

The evidence that this was a botched edit rather than intent is on the page: a
statement at column 0 inside an indented block, and two mutually exclusive log
lines emitted together. Hardware confirmed the latter verbatim.

The non-obvious part is *why* the missing `sdf_zigbee_set_network_joined(true)`
matters more than the misplaced OTA call. Tracing the two paths:

```
factory-new                      already commissioned
    │                                 │
    ├─ start steering                 ├─ (no steering runs)
    │                                 │
    ▼                                 ▼
ESP_ZB_BDB_SIGNAL_STEERING       (no signal is ever raised)
    │                                 │
    └─ set_network_joined(true)       └─ ??? ── the lost else was the only
                                                place this could happen
```

`sdf_protocol_zigbee_is_ready()` computes `stack_started && network_joined`, so
a commissioned lock that reboots is permanently unready and reports no lock
state. That is a silent field failure, and it is the one a user would actually
hit — it needs a reboot of an already-installed lock, which is exactly what
happens after an OTA.

## Decision 2: Narrow the watchdog rather than disarm it

Two ways to stop the idle watchdog reboot loop:

| Option | Effect | Verdict |
| --- | --- | --- |
| `trigger_panic = false` | Watchdog logs but never reboots. Kills the reboot loop *and* the guarantee that a wedged service task reboots the device. | **Rejected.** Directly contradicts `sdf-services-tasks`, which requires a wedged task to reboot rather than fail silently. |
| `idle_core_mask = 0` | Watchdog watches only explicit subscribers. Radio scan no longer trips it; a wedged service task still panics. | **Chosen.** |
| Lower the `sdf_zigbee` task priority below the other service tasks | Does not help: idle is priority 0, so anything above it starves idle just the same. | Rejected. |
| Feed the watchdog from a Zigbee callback | The starvation is inside `zb_mac_logic_iteration`, which does not call back out during the scan. Nothing to hook. | Rejected. |

The framing that settles it: what is the watchdog *for* here? Every long-running
task in this firmware subscribes itself via `sdf_platform_time_wdt_add()` and
reports liveness in its own loop. Idle-task monitoring is a proxy for "something
is hogging the CPU" — but on a unicore part running a closed-source radio stack,
a multi-second scan is normal behaviour, not a fault. The proxy produces false
positives and adds no coverage the direct subscriptions do not already give.

`trigger_panic` stays `true` deliberately, so the reboot guarantee survives.

## Decision 3: Geometric backoff, 1 s → 60 s

Ceiling chosen at 60 s: long enough that an out-of-range lock is not scanning
continuously, short enough that a coordinator coming back online is rejoined
within a minute without user action. The progression reaches the ceiling after
seven failures, roughly two minutes in — fast enough that a genuinely absent
network stops costing scans quickly.

Reset points are both places where "the past failures are no longer relevant":
starting steering fresh from the factory-new startup path, and a successful
join. Without the reset on success, a lock that joined, later dropped, and
retried would start from whatever delay its previous outage had reached.

State is a single `static uint32_t`, accessed only from the Zigbee stack task —
both the signal handler and the scheduler alarm callback run there — so it needs
no lock. Noted in a comment at the declaration, since that is not obvious from
the call sites.

## Verification

Hardware, 100 s continuous capture on the connected esp32c6:

```
rst:0x15 (USB_UART_HPSYS)          ◀ the flash tool's reset, t=0. The only one.
I (1147) Device started in factory-reset mode      ◀ single space
I (1157) Start network steering                    ◀ and nothing else
W ( 3407) ... retrying in 1000 ms
W ( 6657) ... retrying in 2000 ms
W (10907) ... retrying in 4000 ms
W (17157) ... retrying in 8000 ms      ◀ previously aborted at ~18 s
W (27407) ... retrying in 16000 ms
W (45657) ... retrying in 32000 ms
W (79907) ... retrying in 60000 ms     ◀ ceiling
```

No `Guru Meditation`, no `task_wdt`, no second reset in 100 s. Previously: a
reboot every ~18 s.

The board is factory-new, so the branch this change restores — reboot onto an
existing network — is *not* exercised by this capture. See tasks 4.2.

## Risks

| Risk | Assessment |
| --- | --- |
| A real CPU hog now goes unnoticed | Accepted, and narrow: it would have to be a task that neither subscribes to the watchdog nor is one of ours. Every long-running firmware task subscribes. A vendor task spinning forever would show as an unresponsive *subscriber* the moment it starved one. |
| 60 s ceiling delays rejoin after a coordinator outage | Bounded at one minute, and the reset-on-success rule keeps a single outage from compounding. |
| The restored `else` sets joined without verifying the network is reachable | Matches the stack's own contract: `DEVICE_REBOOT` with `ESP_OK` and non-factory-new means the stack restored its network parameters. Reachability is a separate concern from membership, and the pre-existing code made the same assumption on the steering path. |

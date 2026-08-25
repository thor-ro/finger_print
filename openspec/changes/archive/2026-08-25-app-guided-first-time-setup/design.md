## Context

See `proposal.md — Why` for motivation. The constraints that shape the approach:

- **Advertising has one choke point.** `sdf_ble_companion_restart_advertising()` selects between sparse-filtered and pairing-window modes and is called from host sync, from disconnect, and from the pairing-window timer. Adding a third mode and a not-advertising state fits there without new call sites.
- **The single-connection cap is currently emergent, not enforced.** Nothing re-arms advertising on `BLE_GAP_EVENT_CONNECT`, and NimBLE stops undirected connectable advertising when a link comes up, so only one companion connects today. That is a side effect, not an invariant, and the spec now requires enforcement at the connect path.
- **`sdf_services` is already a `REQUIRES` of `sdf_ble_companion`.** The connect path reading setup state introduces no new layering.
- **Bonding is unavoidable during setup.** Every companion characteristic is `_READ_ENC`/`_WRITE_ENC` and `ble_hs_cfg.sm_bonding = 1`, so a client that touches any characteristic bonds and NimBLE persists it. The wizard cannot run over an unbonded link.
- **The NimBLE bond store is not ours.** `CONFIG_BT_NIMBLE_NVS_PERSIST=y` and no `ble_store_config_init` call exists in the app — persistence happens inside the ESP-IDF port with no per-phase hook.
- **`sdf_storage` has a precedent for this record shape.** `sdf_storage_ble_target_save(uint8_t addr_type, const uint8_t addr[6])` stores exactly an addressed identity, and the component has an established "absent key reads as empty, not an error" convention with tests behind it.
- **No devices in the field.** No compatibility path is required for any persisted format introduced here.

## Goals / Non-Goals

**Goals:**
- Make allow-list membership follow from a recorded act rather than from an inference over the bond store.
- Give setup completion a single atomic point that five downstream actions can hang off.
- Bound the unclaimed device's radio exposure in both duration and concurrency.
- Keep the failure modes of a half-finished setup on the safe side: reachable-but-unclaimed rather than complete-but-unreachable.

**Non-Goals:**
- MITM protection on the setup link. Pairing stays Just Works; see Risks.
- Any offline or button-driven path for first-time setup. Removing it is the point of the change.
- Changes to Zigbee commissioning, OTA, or the post-setup dashboard.
- Multi-companion setup. Exactly one client claims the device; further companions use the existing admin-gated pairing window.

## Decisions

### Admission record instead of deferred bond persistence

The leak being fixed: `seed_allow_list()` adopts every persisted bond into the allow list at boot. That inference — *bonded therefore admitted* — held only because the admin-gated pairing window was the sole way to bond. The setup phase adds a second, ungated way, so an abandoned setup-phase connection becomes permanently trusted at the next reboot.

Two candidate fixes were considered.

*Deferring bond persistence* — keep setup-phase bonds in RAM and flush them to NVS at completion — was rejected on cost. It requires replacing the stock NimBLE store with custom `store_read_cb`/`store_write_cb` implementations, hand-managing security records and all 16 CCCD slots, and it leaves the pairing-window path's existing runtime/boot divergence unfixed.

*Persisting admission explicitly* was chosen. A separate record in `sdf_storage` names the identities that were granted trust; `seed_allow_list()` seeds from the intersection of that record and the bond store. It touches no NimBLE internals, and the intersection is robust in both directions: a bond with no admission never becomes trust, and an admission whose keys are gone cannot resurrect a peer NimBLE has forgotten.

The intersection also closes a latent divergence that exists today. In the `BLE_GAP_EVENT_ENC_CHANGE` handler, if the 100 ms `xSemaphoreTake` times out, `admit_if_window_open()` never runs and the peer is not allow-listed at runtime — but NimBLE has already persisted the bond, so the next boot allow-lists it anyway. Under the intersection rule the runtime outcome and the boot outcome agree.

### Latched completion flag instead of derived setup state

`sdf_services_get_setup_state()` derives completion from an enrolled-user count and a `sdf_storage_nuki_load()` probe. Two consequences make that untenable here.

First, there is no atomic moment at which setup becomes complete, yet completion now triggers five ordered actions: persist admission, populate the allow list, push it to the controller, switch advertising mode, and restore the ordinary connection limit. Second, derived state is reversible by unrelated operations — deleting the last admin or clearing Nuki credentials would silently reopen an unfiltered setup phase on a device that has been in service for months.

The latch is written once, at the explicit completion request, and cleared only by factory reset — except when a later part of completion fails, which rolls it back so the device does not come to rest latched-but-still-armed. `sdf_services_get_setup_state()` becomes latch-backed; the intermediate states it reports (Admin enrolled, account registered, Nuki paired) remain derived, since those drive only wizard step selection and are not security-bearing. The account-registered rung is not optional: without it, Admin-enrolled is ambiguous between "needs an account" and "needs Nuki pairing", and the completion check cannot require a single terminal pre-completion state.

### Completion ordering: admission before latch

The completion sequence writes two independent NVS facts, and interruption between them is not symmetric.

Writing the latch first and losing power leaves a device that reports setup complete, advertises allow-list-filtered, and has an empty allow list — unreachable by the app, recoverable only by a physical factory reset. Writing the admission record first and losing power leaves a device still in the setup phase with an inert admission record; the allow list is not enforced while advertising is unfiltered, the owner reconnects, and completion rewrites the record idempotently.

So: admission record, then latch. The spec states this as a requirement rather than leaving it to implementation order, because the failure is silent and only reachable by an ill-timed power loss.

### Timeout disarms the radio, and the button re-arms it

A timeout that only wipes partial state does not bound exposure — the device wipes, returns to the setup phase, and advertises openly again in a loop. Disarming advertising on expiry is what actually bounds it. A button press re-arms.

This keeps the app mandatory in the sense that matters: every setup *step* is app-guided. Pressing a button to wake the radio is not a setup step, and first boot auto-arms so the out-of-box path needs no press. In practice a device will usually have lapsed by the time its box is opened, so "press the button, then open the app" becomes the manual's first line.

The same press does double duty as the squatter tiebreaker: during an armed setup phase it terminates the current setup connection and re-arms advertising. One gesture, one meaning — *give me the setup slot* — whether the slot is occupied or the phase has lapsed. The gesture is available because the app-mandatory decision retires single-click's setup action.

### Two timers, not one

A single absolute timer from arm time was considered and rejected: it starts the clock at the button press, so a user who presses and then goes to find their phone, unlock it and read the first page of the manual can burn a third of the budget before the wizard's first step.

The bound is therefore split. An **arm window** (default 5 minutes) runs from arming and bounds how long the device advertises openly with nobody connected — the state that dominates exposure, since it needs no attacker participation to persist. A **setup deadline** (default 10 minutes) runs from the first accepted connection and bounds how long a user has. Neither is extendable by a client: the deadline survives disconnection, reconnection, and setup progress unchanged.

Ten minutes was sized against the actual steps — connect and bond, three enrolment scans, registration plus its 10-second Admin confirm scan, the walk to the Nuki and its 5-second button hold, pairing, and the finish step. That comes to roughly six minutes of active work for a user who has not read ahead, leaving about 1.6× margin for a fumbled scan or a missed hold. Below eight minutes a single retry starts costing the whole setup.

Worst-case radio-open time is therefore 15 minutes per arm, not 10. Compressing to a 10-minute ceiling would push the deadline to roughly 7 minutes, which is inside the active budget's margin. The arm window is only reachable with no client connected, so the extra 5 minutes buys an attacker nothing they could not get by connecting.

A **connection idle timer** (default 2 minutes) terminates a setup connection that has gone silent and re-arms advertising. It is redundant against the deadline for bounding exposure, but it frees the slot when a browser tab dies, so a user recovers without walking back to the device. It drops the connection only — never the deadline, never any state.

A **button press restarts both timers.** That makes the deadline extendable, but only by someone physically at the device, which is already the trust root for the reclaim gesture. Stated in the spec rather than left implicit, because it is the single exception to "not extendable."

### Connection cap enforced by terminating at connect

Rejecting the second connection in `BLE_GAP_EVENT_CONNECT` was chosen over relying on advertising not being re-armed. The existing code already terminates when no connection slot is free, so the mechanism and its log path exist. Enforcement makes the invariant testable and turns a future well-intentioned `restart_advertising()` on the connect path into a test failure rather than a silent regression.

### Setup state exposed as an unauthenticated GATT read

The wizard needs setup state before an account exists, but every companion characteristic today is gated behind LOGIN. A dedicated read-only characteristic carrying a small enumeration, readable on an encrypted but unauthenticated link, is the smallest change that does not perforate the existing auth gate.

Advertising the state in manufacturer data was considered — it would let the app show "ready to set up" in the browser's device picker before connecting — but the 31-byte advertisement already carries the name and service UUID, and Web Bluetooth's picker filtering would need the byte to be stable across the whole setup phase, which it is not. Deferred; the characteristic is sufficient for the wizard.

No meaningful disclosure is added: unfiltered advertising already announces that the device is unclaimed.

### No resume across a lapse

Within one setup phase the wizard resumes from reported setup state after a reconnect, which costs nothing since the state is already exposed. Across a timeout there is no resume at all — everything is wiped. Partial-state resume across a wipe would require persisting exactly the progress the wipe exists to destroy.

## Risks / Trade-offs

**Factory reset without a fingerprint gate is reachable by anyone with physical button access** → Accepted. It is the only recovery path for a lost Admin fingerprint, and gating it on the fingerprint it recovers from is circular. `doc/wiring.md:53` anticipates the button being routed outside an enclosure, so this may sit on the attacker's side of the door. The exposure is bounded to denial of service, not entry: a hijacker who claims a wiped device still cannot pair a Nuki without pressing the Nuki's own button, which is on the inside. Document the button-placement consequence in the user manual so integrators can choose an interior mount.

**Just Works pairing on an open setup link, carrying a registration password hash** → Unmitigated in this change. `sm_io_cap = BLE_SM_IO_CAP_NO_IO` and `sm_mitm = 0` are global on the shared host and also govern the Nuki central role, so raising them is not local to this change. Acceptable when bonding only happened inside a 60-second admin-gated window; the setup phase is longer and open. Tracked in Open Questions.

**A squatting client can repeatedly take the setup slot** → Mitigated by the physical tiebreaker: the person at the device wins every race by pressing the button. An attacker can force the owner to press it repeatedly, which is a nuisance, not a compromise.

**Timeout too short strands users mid-setup; too long widens exposure** → Addressed by the two-timer split above, sized against a measured step budget. Both values are compile-time constants and can be tuned without a spec change. Mis-tuning fails safe: the device wipes and can be re-armed with a button press.

**Wiping fingerprint templates requires a working sensor round-trip** → The wipe path must not block the disarm. Spec requires a failed template erase to be logged and non-fatal, so a sensor fault cannot leave the device advertising openly forever.

**Setup-phase wipe erases user data on a device that may not be virgin** → Only reachable while the latch is unset, i.e. before setup ever completed or after an explicit factory reset. A device in service cannot re-enter the setup phase, which is the reason the latch is not derived.

## Migration Plan

None required. There are no devices in the field, so the admission record, the setup-completion latch, and the intersection seeding rule are introduced without a compatibility path. Existing `sdf_storage` convention already reads an absent key as empty rather than as an error, so a device flashed with this firmware over an older build boots as unprovisioned and enters the setup phase — the correct outcome for a development unit.

Rollback is a firmware downgrade plus a factory reset; the new NVS keys are ignored by older builds, but an older build would re-adopt every persisted bond into the allow list, so a reset is required to avoid carrying setup-phase bonds back into a build without the intersection rule.

## Open Questions

- **Raising pairing security on the setup link.** Whether to move the shared host off Just Works — and whether the Nuki central role tolerates it — is a separate investigation. It changes no requirement here; the setup phase's security rests on the time bound, the single-connection cap, and the physical tiebreaker rather than on link authentication.
- **Setup state in advertisement data.** Would improve the device-picker experience. Deferred; purely additive to the characteristic-based approach.

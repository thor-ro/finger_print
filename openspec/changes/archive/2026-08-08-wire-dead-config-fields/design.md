## Context

`sdf_config` is a working runtime-configuration layer: fields are loaded from Kconfig, optionally overridden from a persisted NVS blob, validated, dumped at boot, and mutated through validated `sdf_config_set_*()` setters. Most fields are wired correctly — `retention_size` (`sdf_platform_sleep.c:162,175`), `event_router_queue_depth` (`sdf_event_router.c:108`), and the power intervals all read `sdf_config_get()->field` at the point of use. This change is not a repair of the config system; it closes four specific fields that fell out of sync with their consumers.

The nine fields fail in five distinct ways:

| Field | Failure | Consumer today | Action |
|---|---|---|---|
| `nonce_replay_window` | shadowed by macro | `SDF_NUKI_NONCE_REPLAY_WINDOW`, `sdf_protocol_ble.c:25-29` | wire |
| `wdt_timeout_ms` | shadowed by literal | `SDF_APP_TWDT_TIMEOUT_MS = 15000u`, `sdf_app.c:46` | wire |
| `zigbee_enabled` | shadowed by macro | `SDF_ZIGBEE_ENABLED`, `sdf_protocol_zigbee.c:51-55` | wire |
| `adaptive_checkin` | orphaned implementation | `sdf_power_calculate_checkin_interval()` exists, has zero callers | wire, ship off |
| `enable_deep_sleep` | knob and behavior wired backwards | none; `enable_deep_sleep_fallback` is hardcoded `true` and does the job | collapse |
| `fp_power_en_pin` | duplicate of a live field | none; `fp_en_gpio` carries the same Kconfig symbol | remove |
| `require_encrypted_nvs` | consumer initializes before config exists | `sdf_storage_security_status_t`, static-initialized from Kconfig | remove |
| `staged_wake` | no implementation at all | none | remove |
| `nuki_state_poll_interval_ms` | no implementation at all | none | remove |

The first four were found by inspection. The last five came out of the field-by-field sweep run as verification on the first four (task 6.5) — which is the point: the sweep is the only thing that distinguishes "we fixed the fields someone noticed" from "no field in `sdf_config_t` is dead." Folding them in makes the invariant real rather than aspirational, and it is why this design now states the sweep as a requirement instead of a one-time check.

`adaptive_checkin` is the surprise. `sdf_power.c:691` is a complete battery-tiered scaling function (`<20% → base×4`, `<40% → base×2`, `<60% → base×1.5`, else `base`), declared publicly in `sdf_power.h:85`, fed by a working battery pipeline (`sdf_drivers_battery_get_percent` → `.battery_cb` → policy → `sdf_power_set_battery_percent` → `s_state.battery_percent`). It is simply never called, and never consults the flag. Two halves that were built to meet and never did.

`staged_wake` is the opposite: nothing implements "critical → fast path (BLE) → full path (Zigbee)". The seams where it belonged are visible — `sdf_power_policy_handle_wake()` opens with `(void)reason;`, `sdf_power.c:307` hardcodes `WAKE_REASON_TIMER` for every light-sleep wake, and two parallel wake-reason enums exist — but the feature does not.

Constraint that shapes the whole change: this is a battery-powered door lock. Check-in timing governs how quickly a lock responds and how long it survives. Behavior changes here are not free.

## Goals / Non-Goals

**Goals:**
- Every remaining field in `sdf_config_t` is genuinely read by the subsystem it names.
- Default-build behavior is bit-for-bit unchanged by this work.
- The `nonce_replay_window` knob stops making a false security claim.
- Regression coverage prevents these fields from silently decaying back into shadowed constants.

**Non-Goals:**
- Implementing staged wake. It needs wake-reason plumbing repaired first (`sdf_power.c:307` hardcode, discarded `reason` parameter), a Zigbee-rejoin gating design, and resolution of the duplicated `sdf_power_wake_reason_t` / `sdf_power_policy_wake_reason_t` enums. Separate proposal.
- Enabling adaptive check-in in production. This change connects it and ships it off.
- Building a dedicated Nuki state poll cadence, or restructuring boot order so config init can precede storage init. Both are named in the decisions below as the reason a field is removed rather than wired; each deserves its own proposal.
- Changing the battery thresholds or scaling factors in `sdf_power_calculate_checkin_interval()`. Its logic is taken as-is.

## Decisions

### Wire the readers rather than delete the fields

For `nonce_replay_window` and `wdt_timeout_ms`, both directions were viable: make the config real, or delete the fields and admit the values are compile-time.

Chose wiring. Both fields are already fully plumbed through validation, dump, NVS persistence, and (for the security field) the companion config surface — deleting them discards working infrastructure and leaves the operator with strictly less control. More decisively, `nonce_replay_window` is reachable from the BLE companion config path, so an operator can already change it and watch it be validated and echoed. Deleting it silently removes a knob they may believe they are using; wiring it makes the knob honest. The cost is symmetric and small, so the tiebreaker is which end state is more truthful.

*Alternative considered:* delete both, keep compile-time constants. Rejected — smaller diff, but throws away intent and leaves the operator-facing config surface narrower than it already appears to be.

### Clamp at the consumer, not in validation

The nonce cache is `rx_nonce_cache[SDF_NUKI_NONCE_CACHE_MAX]` with `SDF_NUKI_NONCE_CACHE_MAX = 16` — statically sized, so an oversized window would index out of bounds. The existing macro already clamps at compile time; the runtime version must clamp too.

The clamp stays in `sdf_protocol_ble.c`, applied at the point of use. `sdf_config_validate()` must not learn `SDF_NUKI_NONCE_CACHE_MAX` — that constant belongs to `sdf_protocol_ble`, and teaching the config layer about it would invert the dependency (`sdf_config` does not and should not depend on `sdf_protocol_ble`). Defending at the point of use also survives any path that mutates the field after validation: `sdf_config_get_mutable()`, a persisted blob, or a future setter. Validation is advisory; the clamp is load-bearing.

Kconfig keeps its `range 1 16` so the common case is caught early with a clear error, but correctness does not depend on it.

*Alternative considered:* validate the bound in `sdf_config_validate()` and trust it downstream. Rejected — creates a dependency inversion and a bounds guarantee that `sdf_config_get_mutable()` can violate.

### Guard adaptive scaling inside the function, not at each call site

`sdf_power_calculate_checkin_interval()` takes the `adaptive_checkin` check itself and returns the unscaled base when disabled. The five call sites (`:169` light-sleep timer, `:317` deep-sleep timer, `:322` retention, `:425` Zigbee propagation, `:651` `prepare_deep_sleep()`) then call it unconditionally, plus the `:161` sleep-event payload that must report the same interval the timer is armed with.

The light-sleep site (`sdf_power_enter_light_sleep()`) was missed in the first draft of this design, which enumerated only the four deep-sleep/retention/Zigbee sites. That omission was the exact failure this decision exists to prevent: light sleep is the *primary* sleep path and deep sleep is the fallback, so wiring only the four would have left normal check-ins ignoring battery level while the fallback path scaled — a worse outcome than not wiring `adaptive_checkin` at all. Centralizing the flag check inside the function is what made the fix a one-line change at the missed site rather than a fifth place to replicate the ternary.

This keeps the flag check in one place instead of four, and makes every check-in path adaptive together. A per-call-site ternary would let the paths drift — a future site could miss the flag, or scale the wake timer without scaling the retention `next_checkin_us`, desynchronizing the deep-sleep wake from the recorded next-check-in. Since the function already reads global state (`s_state.config`, battery), reading one more field costs nothing structurally.

*Alternative considered:* `cfg->adaptive_checkin ? calculate() : cfg->checkin_interval_ms` at each site. Rejected — four places to keep in sync, and a real desync hazard between the wake timer and retention state.

### Ship adaptive check-in disabled (`default n`)

`CONFIG_SDF_POWER_ADAPTIVE_CHECKIN` currently defaults to `y`, so connecting the implementation would immediately stretch check-in intervals up to 4× on a low battery — on a door lock, that means a lock that responds noticeably slower exactly when its battery is weakest. That is a defensible power trade-off, but it is a *product* decision, and it should not ride in on a change whose stated purpose is wiring dead fields.

Flipping to `default n` separates the two: this change proves the wiring is correct and inert, and enabling the feature becomes its own decision with its own battery-life and responsiveness verification.

*Alternative considered:* leave `default y` and ship the behavior change. Rejected — conflates mechanical wiring with a user-visible power/responsiveness trade-off on a security device, and makes any resulting field regression hard to attribute.

### Remove `staged_wake` rather than leave it inert

`staged_wake` has no reader to wire and no implementation to connect. Leaving it costs nothing at runtime but keeps advertising a capability that does not exist — it is validated, dumped at boot, and persisted, exactly like the fields this change is fixing. Removing it is the same honesty argument that motivates wiring the other three.

*Alternative considered:* leave it in place pending a staged-wake implementation. Rejected — it would remain a live example of the problem this change exists to eliminate, and re-adding a `bool` when the feature is actually built is trivial.

### A field whose consumer initializes before `sdf_config_init()` does not belong in `sdf_config_t`

`require_encrypted_nvs` looks exactly like `nonce_replay_window`: a security-relevant field, populated from Kconfig, validated, dumped, shadowed by an independent copy in the consumer (`sdf_storage.c:16` static-initializes `sdf_storage_security_status_t.require_encrypted_nvs` straight from `CONFIG_SDF_SECURITY_REQUIRE_ENCRYPTED_NVS`). The obvious move is to wire it the same way.

It cannot be wired. `sdf_storage_init()` runs at `sdf_app.c:1477`; `sdf_config_init()` runs at `:1489`, and the comment above it states why the order is fixed — config loads its persisted overrides *from NVS*, which `sdf_storage_init()` brings up. The encrypted-NVS policy is consulted during that bring-up, strictly before any config that could carry it exists. Reading `sdf_config_get()->require_encrypted_nvs` there would read a zero-initialized struct: `false`, i.e. silently disabling the requirement. On a security policy, fail-open.

So the rule generalizes beyond this field: **a config field is only meaningful if its consumer runs after `sdf_config_init()`.** Where it does not, the field is not a knob — it is a copy that can only ever disagree with the real one. `sdf_storage` already owns this policy properly, exposes it through `sdf_storage_get_security_status()`, and `sdf_app.c:1511` already reads it from there. Removing the `sdf_config_t` duplicate leaves exactly one source of truth.

*Alternative considered:* split `sdf_config_init()` into a Kconfig-defaults phase that runs before storage and a persisted-overrides phase after, so early consumers get real values. Rejected here — it is a genuine improvement to the config lifecycle and would also help any future pre-NVS consumer, but it restructures boot for one field whose owner is already correct. Worth its own proposal if a second such field appears.

### Collapse `enable_deep_sleep` into `enable_deep_sleep_fallback` rather than wire it

These two are the same decision under two names. `sdf_power_policy_evaluate()` has exactly one deep-sleep entry (`sdf_power_policy.c:70`): the fallback taken when sleep is allowed and Zigbee is not ready. There is no second path that a separate "deep sleep enabled" master switch would gate. Meanwhile the Kconfig symbol's own prompt is "Enable deep sleep (vs light sleep only)" — describing precisely what the fallback flag controls.

The current state is the wiring inverted: the Kconfig-backed field (`enable_deep_sleep`, `sdf_config.c:77`) is dead, and the live field (`enable_deep_sleep_fallback`, `:76`) is hardcoded `true`, reading no Kconfig at all. So the operator-facing knob does nothing *and* the real behavior is unconfigurable. Fixing this is one line in each direction: source `enable_deep_sleep_fallback` from `CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP` and delete `enable_deep_sleep`.

Behavior is preserved because the symbol is `default y`, matching the hardcoded `true`. The knob becomes real for the first time, and the field count drops rather than rises.

*Alternative considered:* keep both, and gate the policy's deep-sleep branch on `enable_deep_sleep && enable_deep_sleep_fallback`. Rejected — two flags for one decision, where the only reachable configuration difference is "deep sleep off" expressed two ways. That is the kind of surface that produces the next dead field.

*Alternative considered:* rename `enable_deep_sleep_fallback` to `enable_deep_sleep`. Rejected — "fallback" is the accurate description of its role in the policy, and the rename would churn `sdf_power`, `sdf_power_policy`, and `sdf_app` for no semantic gain.

### Remove `nuki_state_poll_interval_ms` rather than build a poller

Same shape as `staged_wake`, and it gets the same answer. There is no reader and no shadow constant — nothing is polling Nuki state on an independent cadence, so there is no behavior to reconnect. Nuki state refresh currently rides the power check-in wake, which for a battery-powered lock is the coherent design: one wake, one radio window, one refresh.

Wiring the field would mean *building* an independent poll timer, which is a power/freshness trade-off requiring its own justification — and would arrive with a `default 15000` that happens to equal the check-in interval, i.e. a second timer that by default does exactly what the existing one does. That is a feature proposal, not field wiring. Removing the field (and its `sdf_config_set_nuki_state_poll_interval()` setter, which is not reachable from the companion config surface) stops it advertising a cadence that does not exist.

### Remove `fp_power_en_pin` as a duplicate

`sdf_config.c:44` sets `fp_power_en_pin = CONFIG_SDF_POWER_FP_EN_GPIO`; `:73` sets `fp_en_gpio` from the identical symbol. Only `fp_en_gpio` is consumed. There is no configuration a caller could express through one that it could not express through the other, and no reader that would observe a difference — the two can only ever diverge through `sdf_config_get_mutable()`, and if they did, the divergence would be invisible. Straight deletion; nothing to wire.

### Wire `zigbee_enabled` — and accept that it is now false before config init

`sdf_protocol_zigbee_is_enabled()` returning `sdf_config_get()->zigbee_enabled` is mechanically the `nonce_replay_window` fix. The difference is the failure mode: this function gates whether a radio stack comes up, and before `sdf_config_init()` the config struct is zero-initialized, so it would answer `false` where the macro answered `true`.

That is why all 15 call sites were audited rather than assumed. Every one is either a runtime path (`sdf_app` alarm-mask/battery/user-list updates, `sdf_power_push_battery_percent()`, the `sdf_config` check-in setter at `:342`, and the internal guards throughout `sdf_protocol_zigbee.c`) or `sdf_app` init after `sdf_config_init()` at `:1489` — with `sdf_protocol_zigbee_init()` itself at `:1723`. None is reachable earlier.

The `defined()` guard at `sdf_config.c:91-93` already sets the field `false` when `CONFIG_SDF_ZIGBEE_ENABLE` is unset, so the build-time-disabled case is preserved exactly. The `linux` host build links `sdf_protocol_zigbee_mock_linux.c`, which keeps its own independent implementation and is untouched.

The gain is that `sdf_config_set_zigbee_enabled()` — a public setter that exists today and silently does nothing — becomes a working runtime kill switch. Flipping it off while the stack is initialized makes the internal guards short-circuit, which is the intended semantics of a kill switch rather than a hazard.

*Alternative considered:* `SDF_ZIGBEE_ENABLED && sdf_config_get()->zigbee_enabled`, keeping the macro as a build-time capability gate. Rejected as redundant — `sdf_config_get_defaults()` already derives the field from the same symbol under the same `defined()` guard, so the conjunction adds a term that can never change the result.

### Accept the NVS blob invalidation

Removing `staged_wake` shrinks `sdf_config_t`, so `sdf_config_load_persisted()` will size-mismatch previously saved blobs and fall back to defaults. This path already exists, already warns, and is explicitly documented as the expected behavior for a layout change (`sdf_config.c:114-121`).

No migration shim is added. Writing versioned-blob migration for a one-field removal on a device whose overrides are re-settable through the companion app is disproportionate. The behavior is a documented, warned, recoverable fallback — not data loss requiring a compatibility layer.

*Alternative considered:* add a version field and a migration path. Rejected as disproportionate; worth revisiting if `sdf_config_t` starts changing shape often.

### Dependency direction for `sdf_protocol_ble` → `sdf_config`

`sdf_protocol_ble` gains `sdf_config` in `PRIV_REQUIRES`. Verified acyclic: `sdf_config`'s own `REQUIRES` are `sdf_platform` and `sdf_protocol_zigbee`, neither of which reaches `sdf_protocol_ble`. The same private-dependency shape already exists in the tree (`sdf_platform` and `sdf_protocol_zigbee` both privately require `sdf_config`), so this introduces no new pattern. `PRIV_REQUIRES` rather than `REQUIRES` because the dependency is an implementation detail — no `sdf_config` type appears in `sdf_protocol_ble.h`.

## Risks / Trade-offs

- **Out-of-bounds nonce cache access if the clamp is wrong** → The clamp is the single load-bearing safety property in this change. It must be applied to the value actually used for indexing and modulo, at every use, not merely checked once. Requirement-level scenarios cover the oversized and zero cases specifically; the zero case must short-circuit before any indexing or modulo (a `% 0` would fault).

- **Runtime config read on the BLE hot path** → `sdf_config_get()` returns a pointer to a static struct with no locking, and the nonce path runs per encrypted message. This is a plain read of a `uint8_t`, matching how `sdf_platform_sleep.c` and `sdf_event_router.c` already read config; no new synchronization concern. Torn reads are not possible for a single byte.

- **Silent regression back to shadowed constants** → Nothing structurally prevents someone reintroducing a literal later. Mitigated by host tests that assert the wired value actually reaches its consumer, so a reversion fails the build rather than going unnoticed.

- **Watchdog timeout from a persisted blob could be hostile** → A persisted `wdt_timeout_ms` now reaches `esp_task_wdt_reconfigure()`. Bounded by `sdf_config_validate()` (`5000`–`60000`), which runs on the persisted blob before it is accepted, and by Kconfig `range`. A blob failing validation is discarded entirely.

- **Adaptive check-in is wired but unexercised in production** → Shipping `default n` means the newly connected path gets no field soak time; a latent bug surfaces only when someone enables it. Accepted deliberately — it is the correct trade for not changing door-lock timing implicitly. Mitigated by host tests covering both flag states, so the enabled path is exercised in CI even though it is off in the field.

- **Operators lose persisted overrides on upgrade** → One-time fallback to Kconfig defaults, warned in the log. Overrides are re-settable via the companion app. Called out in the migration plan so it is not a surprise. None of the removed fields are themselves companion-settable, so nothing becomes permanently unreachable.

- **Kconfig omits the macro for a `bool` that is `n`** → `config->x = CONFIG_SOME_BOOL;` fails to compile when the symbol is off, because Kconfig does not define it to `0` — it does not define it at all. This already bit the `adaptive_checkin` work once (the break was masked by a stale `=y` in the checked-in sdkconfig). It applies again to `enable_deep_sleep_fallback` now that it reads `CONFIG_SDF_POWER_ENABLE_DEEP_SLEEP`, which is user-flippable to `n`. Every such assignment uses the `#if defined(...) / #else / #endif` idiom already established for `CONFIG_SDF_ZIGBEE_ENABLE` at `sdf_config.c:91-93`. Verification must build with the symbol both `y` and `n`, not just the default — a default-only build is exactly what hid the bug last time.

- **`sdf_protocol_zigbee_is_enabled()` becomes order-dependent** → It reads config, so it answers `false` before `sdf_config_init()`. Audited: no call site runs that early. The residual risk is a *future* caller added to early boot, which would see Zigbee silently disabled rather than failing loudly. Accepted; the same hazard already exists for every other config consumer in the tree, and the fail direction (radio off) is the safe one.

## Migration Plan

1. Land the wiring for `nonce_replay_window` and `wdt_timeout_ms` first — these are behavior-preserving on default builds (Kconfig defaults already equal the shadowed values: `8` and `15000`).
2. Land the `adaptive_checkin` wiring together with the `default n` flip in the same commit, so no intermediate build exists where the implementation is connected and the flag still defaults on.
3. Land the `zigbee_enabled` wiring and the `enable_deep_sleep` collapse — also behavior-preserving, since both Kconfig symbols default to `y` and already feed the fields being made authoritative.
4. Land all five field removals (`staged_wake`, `nuki_state_poll_interval_ms`, `fp_power_en_pin`, `enable_deep_sleep`, `require_encrypted_nvs`) last, since they are what invalidates persisted NVS blobs. Grouping them means one blob invalidation rather than several.
5. On first boot after upgrade, expect one `Persisted config size mismatch ... discarding` warning per device that had saved overrides. Devices fall back to Kconfig defaults; overrides can be re-applied through the companion app.

**Rollback:** Reverting restores the compile-time constants and the five removed fields. A device that saved a blob under the new layout will size-mismatch on downgrade and fall back to defaults — the same warned, recoverable path, symmetric in both directions.

## Open Questions

- Should `sdf_config_dump()` mark fields that are runtime-effective versus Kconfig-only? Out of scope here (after this change all remaining fields are effective), but relevant if any field is ever intentionally left compile-time.
- Are the battery thresholds in `sdf_power_calculate_checkin_interval()` right for this hardware? Its comments assume a 15s base. Deliberately unexamined — belongs with the decision to enable the feature, not with wiring it.

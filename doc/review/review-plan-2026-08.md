# SDF v2.0 — Full Project Review Plan

**Created:** 2026-08-26
**Scope:** `firmware/` (18 components), `web-companion/`, `tools/`, `scripts/`, build config, CI, `doc/`, `openspec/`
**Baseline:** ESP-IDF v6.0.2 / ESP32-C6, firmware version `0.2.2` (`version.md`)

---

## 1. Why this review

The last full-repo review (`todo`, root) was taken against ESP-IDF 5.5.3 and a firmware
generation that no longer exists: it references `CONFIG_SDF_OTA_SIGNATURE_VERIFY`, the
hand-rolled ECDSA footer scheme and `web-companion/app.js`, all removed in 0.2.2. Its
findings are therefore untrustworthy as a current risk picture — but they are not
worthless, because several were Critical and nobody has recorded whether they were fixed
or merely relocated. This review re-establishes a current, evidence-backed baseline for a
device that unlocks a physical door.

**Goals**

1. Confirm or refute every Critical/High finding in the stale `todo` backlog against today's code.
2. Produce a current risk picture for the security-critical paths (BLE companion auth, OTA, biometrics, storage).
3. Find correctness bugs in the concurrency, power and protocol layers that tests do not cover.
4. Quantify the test-coverage gap and turn it into a prioritised, verifiable backlog.

**Non-goals:** feature work, refactors-for-taste, hardware/PCB review (`hw_todo.md` tracks that separately).

---

## 2. Inventory (measured, not estimated)

| Area | Size | Test LOC | Notes |
|---|---|---|---|
| `sdf_services` | 4,689 | 2,198 | 8 src files, **1** test file — enrol/match/admin tasks |
| `sdf_ble_companion` | 4,244 | 853 | phone-facing GATT surface, auth, OTA transport |
| `sdf_protocol_ble` | 3,114 | 1,164 | Nuki adaptor, crypto framing, nonce replay |
| `sdf_app` | 3,038 | 685 | flows; not built for `linux` target |
| `sdf_drivers` | 2,386 | 288 | fingerprint UART, LED, battery, GPIO |
| `sdf_protocol_zigbee` | 2,052 | 295 | Door Lock cluster, attribute reporting |
| `sdf_platform` | 1,334 | 220 | HAL wrappers |
| `sdf_cli` | 1,432 | 99 | **layout deviates**: `.c` at component root, no `src/` |
| `sdf_storage` | 1,085 | 896 | NVS, encryption policy |
| `sdf_power` + `sdf_power_policy` + `sdf_platform_power` | 1,076 | 395 | sleep decisions |
| `sdf_ota` | 830 | 235 | IDF signed-app verification, rollback |
| `sdf_event_router` | 548 | 877 | typed bus; subscribers must not emit |
| `sdf_device_state` | 585 | 297 | health report producer |
| `sdf_config`, `sdf_common`, `sdf_state_machines` | 1,449 | 638 | `sdf_common` has **no** tests |
| **Firmware total** | **~26.5k** | **~8.9k** | |
| `web-companion/` | SvelteKit 5 + Vite | 8 `*.test.ts` | gated by `npm run gate` |

**CI today:** `build-firmware (esp32c6)`, `ota-signature (esp32c6 — esp-emu)`,
`test-firmware (linux target — partial coverage)`, plus Pages deploy. No web-companion
gate job on PRs — `npm run gate` exists but appears to run only locally/at deploy time
(**verify in Phase 0**). `tests/` at repo root contains only a README while
`doc/test_strategy.md` describes a unit/integration/HIL split — drift to confirm.

---

## 3. Entry criteria

- [ ] Working tree clean. Currently the `web-companion-theming` change is uncommitted/partially staged (deleted `openspec/changes/...`, untracked `openspec/changes/archive/2026-08-26-...`). Land or stash it first so review diffs are attributable.
- [ ] `firmware/test_runner` builds and passes on the `linux` target (record the baseline pass/fail set).
- [ ] `scripts/run_ota_signature_gate.sh` passes, and `--no-verify` self-test fails as designed.
- [ ] `web-companion && npm ci && npm run gate` green; record bundle numbers vs `budget.json` (49,255 / 56,641 gzip bytes).
- [ ] `codebase-memory` index current (`index_status` → `index_repository` if stale) for call-graph tracing.

---

## 4. Workstreams

Each workstream produces one findings file under `doc/review/findings/`. Ordered by risk;
A–C are the ones worth doing even if the review is cut short.

### Phase 0 — Backlog triage (½ day)
Re-walk the root `todo` findings A1…A*, mark each **fixed / still-present / obsolete /
unverifiable**, with a `file:line` citation for the verdict. Explicitly re-test the two
Criticals that would defeat the whole product:
- **A1 auth bypass** — all-zero `password_hash` accepted at login. Trace registration →
  `sdf_storage` web-user record → `sdf_ble_companion` login compare. Verify a zero-hash
  login is rejected today, and that the rejection is a *deliberate* check, not an accident
  of a changed struct layout.
- **A2 unauthenticated GATT link** — check the characteristic permission flags for
  `_ENC`/`_AUTHEN`, bonding requirements, and whether `BLE_OWN_ADDR_PUBLIC` still leaks a
  static identity address (privacy/RPA).
Output: `findings/00-backlog-triage.md` + delete or rewrite the root `todo` so a stale
document stops masquerading as a current review.

### A — Security & trust boundaries (2 days) — *highest priority*
Boundaries to review as boundaries, each with an explicit "what does the attacker control" note:
1. **Phone ↔ device GATT** (`sdf_ble_companion/`): admission/pairing window, session
   lifecycle and expiry, per-characteristic auth checks (can an unauthenticated peer reach
   enrol/config/OTA?), permission model (`companion-identity`, `companion-user-mgmt` specs),
   rate limiting and lockout on login, error messages that leak user existence.
2. **Credential handling**: password hash derivation (iterations/salt — single-iteration
   SHA-256 was the old finding), constant-time comparison, secret zeroisation, what the
   web companion holds in memory and for how long.
3. **OTA chain** (`sdf_ota/`, `sdf_ble_companion_ota.c`): trust anchor is the running app's
   own signature block — confirm no path accepts an image before `esp_ota_end()`;
   downgrade-allowed policy vs anti-rollback; WiFi credentials + HTTPS URL supplied by the
   phone (TLS verification? URL restrictions? credential persistence?); partial/aborted
   session cleanup; `ota-session-lifecycle` spec conformance.
4. **Nuki link** (`sdf_protocol_ble/`): nonce replay cache bounds (8 entries — is that
   sufficient against interleaving?), key storage, framing/parse hardening against a
   hostile or spoofed lock.
5. **Biometric path**: brute-force threshold/window/lockout enforcement points, template
   ↔ user binding integrity, behaviour on sensor error/absent sensor (fail-open risk).
6. **Storage**: encrypted-NVS boot policy, key partition handling, what happens when the
   policy check fails (error vs continue), factory reset completeness.
7. **Web companion**: CSP hash pinning (`scripts/csp.mjs`), the four `lint.mjs` invariants,
   XSS surface, secrets in prerendered assets shipped to public GitHub Pages.
8. **Key hygiene**: `ota_signing_key.pem` is correctly gitignored and untracked (verified);
   check history for a past leak, and review the CI `OTA_SIGNING_KEY` injection path.

Method: `/security-review` on the security-critical components, plus manual boundary
tracing with `trace_path(..., mode=cross_service)`; do not rely on the skill alone.

### B — Concurrency & RTOS correctness (1.5 days)
11 tasks on one core (`doc/rtos_tasks.md`). Review for:
- mutex discipline and TOCTOU windows — the `power2.md` BUG-1 pattern (state snapshot used
  after the lock is released) is a template; grep for the same shape everywhere;
- the event-router contract "subscriber callbacks must not emit" — verify every subscriber,
  since violation is a latent deadlock/recursion, not a compile error;
- queue depths, full-queue behaviour (drop vs block) on the fingerprint, LED, zb-attr and
  event queues; blocking calls inside callbacks; ISR-context safety;
- stack high-water marks against the declared sizes (2–8 KB) under load, and priority
  interactions between the five priority-5 tasks;
- shutdown/cleanup paths on error (task deletion, handle reuse, use-after-free).

### C — Power management & sleep (1 day)
Re-verify `doc/review/power2.md` and `doc/reviews/powermanagement.md` findings against
current code (are BUG-1..n fixed?), then: light↔deep sleep transitions, wake guard
oscillation, BLE radio gating correctness around an active companion session (does sleep
drop a live OTA or enrol session?), Zigbee SED interplay, retention memory (256 B) contents
and versioning across OTA, wake-source coverage.

### D — Protocol conformance & state machines (1 day)
Nuki BLE adaptor vs `doc/NukiSmartLockAPI2_3_0.md`; Zigbee Door Lock cluster + attribute
reporting vs the `zigbee-attribute-reporting` / `zigbee-commissioning` specs; enrolment and
device state machines (`sdf_state_machines`) — unreachable states, missing timeouts, retry
storms, resource leaks on abort. Focus on error and timeout edges: the happy paths are the
ones the tests already cover.

### E — Storage, config & migration (½ day)
NVS schema versioning and migration across an OTA (0.2.x → next), partition table vs
`nvs_keys`, Kconfig defaults vs `sdf_config` constants vs documented values in `AGENTS.md`
(three sources of truth — find the drift), factory reset, wear/erase behaviour.

### F — Test coverage & CI gates (1 day)
- Map coverage gaps against risk, not against LOC. Standouts: `sdf_services` (4.7k LOC,
  one test file, owns match/enrol/admin), `sdf_drivers`, `sdf_protocol_zigbee`, `sdf_common` (none).
- The `linux`-target gap for `sdf_app` + BLE/OTA stacks — evaluate the proposed
  `add-linux-target-sdf-app-support` change; today those suites only run on-chip and the
  chip run is *not clean* (four failing suites per `AGENTS.md`). Decide: fix, gate, or
  document as accepted.
- Close the loop on the esp-emu BLE OTA harness defect (bulk transfer wedges at ~28–31
  inbound HCI ACL packets, esp-emu 0.39.0–0.40.1) — is Layer 1 + hardware evidence still
  the right compensating control?
- Add the missing web-companion CI job if Phase 0 confirms `npm run gate` isn't enforced on PRs.
- Reconcile the empty root `tests/` with `doc/test_strategy.md`.

### G — Architecture, specs & doc drift (½ day)
27 `openspec/specs/` capabilities vs implementation; only 2 ADRs for a system with this
many load-bearing decisions (signed-app OTA, event-router contract, single-core task
layout, theming token contract) — propose the missing ones; `AGENTS.md` doc-sync rule
compliance; `sdf_cli` layout deviation (`.c` at component root vs the documented
`include/`+`src/` convention); `version.md` accuracy; placeholder Nuki address still in
`sdf_app.c` as a first-boot footgun.

### H — Web companion quality (½ day)
Svelte 5 runes state model (`session.svelte.ts`, `theme.svelte.ts`), transport abstraction
seam (`FakeTransport` vs `ble.ts`), a11y on the wizard/auth flows, error surfacing for BLE
failures, bundle budget headroom, theme contract compliance, `hardware-tests/` currency.

---

## 5. Finding format & severity

Every finding, in every workstream file:

```
### <WS><n> — <one-line claim>
**Severity:** S1|S2|S3|S4   **File:** path:line   **Confidence:** high|medium|low
**Evidence:** what the code does (quote ≤10 lines)
**Impact:** what an attacker/user/device experiences
**Fix:** concrete change
**Verification:** the command or test that proves it fixed
```

- **S1 — Blocker:** door opens for someone who shouldn't get in, device bricks, or an
  update can be forged. Fix before any release.
- **S2 — Major:** correctness/availability bug reachable in normal operation (deadlock,
  lost unlock, battery drain, failed OTA recovery).
- **S3 — Minor:** edge-case bug, missing bound, poor error handling.
- **S4 — Nit:** style, naming, doc drift.

Confidence is mandatory: a static read of a concurrency path is `medium` at best until
it's reproduced under the emulator or on hardware. Say which it is.

---

## 6. Verification harness per workstream

| Workstream | How a finding gets proven / a fix gets verified |
|---|---|
| A (OTA) | `scripts/run_ota_signature_gate.sh` (+ `--no-verify` self-test) |
| A (BLE auth) | `tools/ble_ota_harness` under `esp-emu --ble-hci tcp:` (bulk transfer blocked; auth/ACL paths work) |
| B, C, D, E | `firmware/test_runner` on `linux`; on-chip via `esp-emu --chip esp32c6` for anything touching BLE/WiFi/sleep |
| F | CI job additions on a branch |
| H | `npm run gate` |

Emulator panics are treated as real defects until traced — they have reproduced on
hardware before.

---

## 7. Sequencing & effort

| # | Workstream | Effort | Depends on |
|---|---|---|---|
| 0 | Backlog triage | 0.5 d | entry criteria |
| 1 | A — Security | 2.0 d | 0 |
| 2 | B — Concurrency | 1.5 d | — |
| 3 | C — Power | 1.0 d | B (shared locking analysis) |
| 4 | D — Protocol/state machines | 1.0 d | — |
| 5 | E — Storage/config | 0.5 d | — |
| 6 | F — Tests/CI | 1.0 d | 1–5 (gaps are known by then) |
| 7 | G — Architecture/docs | 0.5 d | 1–5 |
| 8 | H — Web companion | 0.5 d | — |
| | **Total** | **~8.5 d** | |

A–C is ~5 days and covers the paths where a bug means the door opens or the device dies;
if the review is time-boxed, stop after C and ship F as a follow-up change.

---

## 8. Deliverables

1. `doc/review/findings/00-backlog-triage.md` … `08-web-companion.md` — one per workstream.
2. `doc/review/review-2026-08-summary.md` — consolidated S1/S2 list, risk verdict, sign-off.
3. Root `todo` rewritten or deleted (it is a stale review masquerading as a live backlog).
4. An `openspec` change proposal for every structural fix (missing CI gate, `linux`-target
   support, NVS migration, missing ADRs) — findings that need design go through the normal
   propose/apply/archive flow rather than being fixed inline.
5. S1 fixes land as their own changes with gate evidence attached; S3/S4 batch into a
   single cleanup change.

## 9. Known constraints

- No hardware-in-loop CI; hardware evidence is manual and must be recorded in the findings file with date + firmware hash.
- esp-emu cannot carry bulk BLE OTA transfer (see F) — that path stays hardware-verified.
- `sdf_app` and the lock-flow suites don't build for `IDF_TARGET=linux`, so workstream A/D findings there are static-analysis-plus-emulator, not host-test-backed.
- Switching `test_runner` targets rewrites `dependencies.lock`/`managed_components/` in place; always finish on the `linux` target so the committed lock stays on the CI target.

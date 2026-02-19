# SDF Implementation Roadmap (Missing Functionality)

This roadmap captures the remaining work needed to move from the current scaffolding to a complete SDF v2.0 implementation. It is organized by phase, with deliverables, dependencies, and validation notes.

## Phase 0 — Foundations (Now)
**Goal:** Ensure the firmware can build, link, and run with a clear module boundary.

**Missing items**
* ESP-IDF project wiring beyond stubs (component APIs, module integration).
* Build profiles (debug/release) validated via CI or local build.

**Deliverables**
* `idf.py build` succeeds on ESP32-C6.
* Component headers define real interfaces and data types.

**Validation**
* Build pass, compile-only smoke test.

---

## Phase 1 — BLE Transport + Nuki Pairing Integration
**Goal:** Make the BLE transport real and prove the pairing flow works on a real lock.

**Missing items**
* BLE GATT transport layer for GDIO/USDIO (subscribe indications, write long).
* Pairing flow wired to transport (GDIO for unencrypted, USDIO for encrypted).
* Connection lifecycle management (connect, MTU exchange, timeouts).
* Persistent credentials loaded at boot and applied to client.

**Deliverables**
* BLE module with callbacks:
  * `on_gdio_indication` → `sdf_nuki_pairing_handle_unencrypted`
  * `on_usdio_indication` → `sdf_nuki_client_feed_encrypted` or pairing handler
* Pairing success stores `authorization_id` + `shared_key` via NVS.
* Auto‑reconnect using stored credentials.

**Validation**
* Pairing succeeds with a test Smart Lock.
* Stored credentials survive reboot and allow encrypted commands.

---

## Phase 2 — Lock Actions + State Reporting
**Goal:** End‑to‑end unlock/lock via BLE commands and state feedback.

**Missing items**
* Lock Action command integration with challenge flow.
* Status/Keyturner States parsing and reporting.
* Error handling + retry policy.

**Deliverables**
* `Request Data (Challenge)` → `Lock Action (0x000D)` → `Status` flow.
* `Keyturner States (0x000C)` parsed into internal state.
* Unified status callback for higher layers.

**Validation**
* Unlock/lock completes and status transitions observed.
* Errors reported (CRC, auth, motor errors).

---

## Phase 3 — Zigbee Door Lock Cluster (ZHA)
**Goal:** Enable Zigbee commands and attribute reporting.

**Missing items**
* ZHA Door Lock cluster implementation (0x0101).
* Mapping `Lock/Unlock/Programming Event` to internal events.
* Attribute reporting: lock state, battery, alarms.

**Deliverables**
* Zigbee End Device with sleepy profile + check‑in.
* Command routing to BLE actions.
* Attribute updates based on BLE responses.

**Validation**
* Home Assistant (or similar) can lock/unlock and see state.
* Lock state updates on BLE actions.

---

## Phase 4 — Fingerprint Driver + Enrollment
**Goal:** Local biometric unlock + headless guided enrollment.

**Missing items**
* UART fingerprint driver (match + enrollment commands).
* Enrollment state machine with LED ring feedback.
* Mapping user IDs to sensor templates.

**Deliverables**
* Local biometric unlock flow triggers BLE unlock.
* Enrollment triggered from Zigbee `Programming Event`.
* LED patterns for 3‑step enrollment.

**Validation**
* Enrollment completes and stores template reliably.
* Successful match unlocks door locally.

---

## Phase 5 — Power Management + Sleepy End Device
**Goal:** Battery‑first operation with bounded latency.

**Missing items**
* Deep sleep scheduling (TWT/check‑in).
* Wake source integration: fingerprint WAKE, Zigbee check‑in, timers.
* Radio gating (BLE/Zigbee on demand).

**Deliverables**
* Configurable check‑in interval.
* Power state machine coordinated with BLE/Zigbee tasks.
* Battery reporting via Zigbee.

**Validation**
* Battery drain baseline measured.
* Remote unlock latency within target bounds.

---

## Phase 6 — Security & Hardening
**Goal:** Harden crypto usage and abuse resistance.

**Missing items**
* Nonce management strategy (prevent reuse).
* Rate limiting for failed biometric attempts.
* Secure storage policy for credentials (NVS encryption).

**Deliverables**
* NVS encryption verified with partition table + keys.
* Failed‑attempt thresholds and alarms via Zigbee.
* Audit logging hooks.

**Validation**
* Negative tests (bad CRC, wrong auth) handled gracefully.
* Credentials remain after reboot and remain encrypted.

---

## Phase 7 — OTA & Diagnostics
**Goal:** Operational maintainability.

**Missing items**
* OTA update mechanism (if required).
* Diagnostics/reporting (error counters, last command).

**Deliverables**
* OTA flow decision + implementation (ESP‑IDF OTA if enabled).
* Diagnostic counters exposed via logs or Zigbee attributes.

**Validation**
* OTA update on test hardware (optional).
* Diagnostics visible in logs.

---

## Phase 8 — Test & Certification Readiness
**Goal:** Confidence in release quality.

**Missing items**
* Unit tests for drivers/CRC/state machines.
* Integration tests for BLE and Zigbee.
* HIL test harness for lock/unlock + enrollment.

**Deliverables**
* CI scripts to run unit tests.
* Hardware test plan and coverage checklist.

**Validation**
* Automated tests pass; HIL tests scripted.

---

## Cross‑Cutting Decisions to Confirm
* BLE stack choice (Bluedroid vs NimBLE).
* Zigbee stack versions and attribute mapping.
* Target check‑in interval vs latency.
* OTA requirement and update channel.
* Default lock actions and retry policy.

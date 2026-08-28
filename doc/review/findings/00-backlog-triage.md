# Phase 0 — Triage of the stale root `todo` backlog

**Date:** 2026-08-26
**Verdict: the backlog is obsolete. Every Critical/High item spot-checked is fixed, and several were fixed with a comment naming the finding.**

| Old ID | Claim | Verdict | Evidence |
|---|---|---|---|
| A1 | Web users stored with all-zero password hash → auth bypass | **Fixed / superseded** | Single-message LOGIN retired (`sdf_ble_companion.c:38`). Login is now LOGIN_INIT/LOGIN_VERIFY challenge-response: per-user salt + PBKDF2-HMAC-SHA256 stretching (`sdf_services_web_auth.c:28-46`), HMAC over a single-use nonce invalidated before verification (`sdf_ble_companion.c:456-462`), constant-time compare, live admin-authority re-check (`sdf_ble_companion.c:475-479`). Registration persists only via `decide_registration()`, which refuses an unbound or unstretched credential (`sdf_services_web_auth.c:123-172`). |
| A2 | Companion GATT link unauthenticated and unencrypted | **Fixed, one part open** | All six characteristics now carry `READ_ENC`/`WRITE_ENC` (`sdf_ble_companion.c:1154-1216`), and Config/Enroll/OTA additionally gate on live admin authority (`conn_has_admin_authority()`, `sdf_ble_companion.c:857-863`). **Still open:** advertising uses `BLE_OWN_ADDR_PUBLIC` (`:1647, :1675, :1763`) — no RPA, so the device presents a persistent identity address. Carried into workstream A as a privacy item, not re-filed as a Critical. |
| A3 | OTA signature verification off by default / non-functional | **Obsolete** | The hand-rolled footer scheme is gone; verification is ESP-IDF signed-app, unconditional, inside `esp_ota_end()` (`version.md` 0.2.2), gated in CI by `ota-signature (esp32c6 — esp-emu)`. |
| A4 | Anti-rollback comparison inverted | **Fixed** | `sdf_ota.c:404-409` checks `SDF_OTA_VERSION_OLDER` with a comment recording the inversion. |
| A5 | `enrolled_user_count` capped at 1, drives privilege escalation | **Fixed** | Replaced by a bitmap + popcount (`sdf_services_enrolled_user_count()`), with five dedicated tests registered in `test_runner_main.c:163-167`. |
| A8 | Light-sleep wake timer 1000× too long | **Fixed** | `sdf_power.c:169` passes milliseconds with a comment stating the unit. |
| A14 | 512-byte stack buffers in NimBLE host callbacks | **Fixed** | Replaced by the shared GATT staging buffer; `sdf_ble_companion.c:383-385` names A14 as the reason. |
| A17 | Unvalidated, non-persisted runtime config over BLE | **Fixed** | Writes go through a validated setter (`sdf_app.c:930`), `sdf_config_validate()` range-checks every field, and validated config is persisted and re-validated on load (`sdf_config.c:178-191`). |
| A18 | Biometric match ignores permission level | **Decided, not a bug** | `sdf_app.c:1344-1357` rejects any permission outside 1–3 and documents that no tier restriction on unlatch is intended today. |

**Actions**
1. Delete or archive the root `todo`. It reads as a live backlog, is cited nowhere, and every item in it is stale — leaving it in place is a trap for the next reader.
2. The only surviving item (`BLE_OWN_ADDR_PUBLIC`, no resolvable private address) moves to workstream A as a low-severity privacy finding.

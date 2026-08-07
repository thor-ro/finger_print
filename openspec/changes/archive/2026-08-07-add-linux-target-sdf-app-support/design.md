## Context

The web companion login/registration decision logic is currently split across two components that can never build for `IDF_TARGET=linux`:

- `sdf_ble_companion_auth_access()` (`firmware/components/sdf_ble_companion/src/sdf_ble_companion.c`) inlines the `LOGIN` hash comparison (`mbedtls_ct_memcmp` against a `sdf_storage_web_user_t` looked up via `sdf_storage_web_user_find_by_name()`) directly inside the GATT characteristic access callback.
- `sdf_app.c`'s `sdf_ble_companion_on_auth_request`, `sdf_app_on_web_reg_auth_result`, and `sdf_app_on_admin_action_complete` hold the `REGISTER` admin-approval glue: emitting the auth-request event, deciding what to persist once an admin approves/denies, and resolving the pending state on timeout/rejection.

Neither component builds on `linux` (`sdf_ble_companion` unconditionally `REQUIRES bt esp_wifi esp_netif esp_http_client`; `sdf_app` unconditionally `REQUIRES sdf_ble_companion`), so none of this logic has ever run under Unity. `openspec/specs/sdf-services-tasks/spec.md`'s "Web Registration Authorization" requirement already describes `sdf_services`/`sdf_admin_task` as the intended owner of the admin-approval workflow, with the GATT server only executing the result — the current code just hasn't fully converged on that split yet. `sdf_services_admin.c` already owns the `SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH` state machine, and `sdf_services.c` already owns the pending-request state (`web_reg_auth_pending`, `request_web_username`). Both `sdf_ble_companion` and `sdf_app` already list `sdf_services` in `REQUIRES` (confirmed: `sdf_ble_companion`'s `REQUIRES` includes `sdf_services`, though no `.c` file in that component currently `#include`s `sdf_services.h` — it's an unused-so-far edge in the build graph, not a new one).

## Goals / Non-Goals

**Goals:**
- Every auth/registration *decision* (login hash verification, what to persist on approval, whether a pending request must be resolved) is expressed as a pure function inside `sdf_services`, reachable and testable under `IDF_TARGET=linux` via the existing `test_runner`.
- `sdf_ble_companion.c` and `sdf_app.c` are reduced to: gather inputs → call the decision function → perform I/O (storage write, BLE reply) based on the result. No security-relevant branching left un-tested in either file.
- Zero new `REQUIRES` edges in the component graph; zero new `test_runner` component wiring (`sdf_services` is already built and wired).
- External BLE/GATT behavior is bit-for-bit unchanged — this is a refactor plus new coverage, not a behavior change.

**Non-Goals:**
- Making `sdf_ble_companion` or `sdf_app` themselves buildable/testable for `IDF_TARGET=linux`. Both remain hardware/stack-bound, exactly as `fix-test-runner-build`'s design doc intended (it explicitly rejected the broader version of this as disproportionate risk).
- Moving the storage slot-allocation loop in `sdf_app_on_web_reg_auth_result` (the `for` loop over `SDF_STORAGE_WEB_USER_MAX` finding the first `!valid` slot) into a tested component. It's a simple linear scan with low bug surface, and moving it would mean either scope-creeping `sdf_services`'s new module beyond "auth decisions" into storage allocation, or expanding `sdf_storage`'s API — neither is in this proposal's stated Impact. Tracked as an Open Question below, not resolved here.
- Changing `sdf-services-tasks`' or `ble-companion-service`'s external-facing spec requirements. The new scenarios added to `sdf-services-tasks` describe internal guarantees (constant-time compare, pending-state always resolves) that were already implied but not testable.

## Decisions

**New file `sdf_services_web_auth.c`, following the component's existing per-responsibility split.**
`sdf_services` already separates `_match.c`, `_enroll.c`, `_admin.c`, `_button.c`. A new `_web_auth.c` fits that convention rather than growing `sdf_services.c` or `_admin.c` (which already owns the *admin-approval state machine*, a distinct concern from *auth decisions*).

Alternatives considered:
- **New standalone component (`sdf_auth`)** — rejected. Would need the same new `REQUIRES` edge from both `sdf_ble_companion` and `sdf_app` that extending `sdf_services` avoids entirely (both already require it), plus a new `test_runner` `EXTRA_COMPONENT_DIRS`/`COMPONENTS` entry and a new test target, for no isolation benefit `sdf_services_web_auth.c` doesn't already provide via its own file boundary.
- **Fold into `sdf_state_machines`** — rejected. That component is tightly coupled to `fingerprint.h`/`sdf_drivers` biometric types (`sdf_enrollment_result_from_driver()`, etc.); unrelated web-auth logic doesn't belong in a fingerprint-hardware-flavored module.

**Three decision functions, each pure (no I/O, no locks, no BLE/storage calls), added to `sdf_services.h` (public) since `sdf_ble_companion` needs to call the first one directly:**

```c
/* Login verification. Caller (sdf_ble_companion) already looked the user up
 * via sdf_storage_web_user_find_by_name(); this just isolates the
 * constant-time comparison so it's independently testable, including with
 * crafted mismatched-length / all-zero inputs. */
bool sdf_services_web_auth_verify_login(const sdf_storage_web_user_t *user,
                                         const uint8_t *submitted_hash,
                                         size_t hash_len);

/* Registration outcome. Mirrors sdf_app_on_web_reg_auth_result's logic minus
 * the actual sdf_storage_web_user_save() call and slot selection (still
 * sdf_app's job, per Non-Goals above). */
typedef struct {
  bool should_persist;              /* false on denial/timeout */
  sdf_storage_web_user_t user;      /* populated only if should_persist */
  bool reply_authorized;            /* value to pass to sdf_ble_companion_reply_auth() */
} sdf_services_web_auth_registration_decision_t;

sdf_services_web_auth_registration_decision_t sdf_services_web_auth_decide_registration(
    const char *username, const uint8_t *password_hash, size_t hash_len,
    uint8_t permission, bool admin_authorized);

/* Timeout/reject unlatch guard. Trivial today (action == WEB_REG_AUTH &&
 * result != ESP_OK), but made explicit and tested so a future admin-action
 * type addition can't silently break the "always resolve the pending BLE
 * client" guarantee sdf_app_on_admin_action_complete's comment warns about. */
bool sdf_services_web_auth_should_resolve_on_action_complete(
    sdf_services_admin_action_t action, esp_err_t result);
```

**`sdf_services`'s `CMakeLists.txt` gains `PRIV_REQUIRES mbedtls`.**
Only new dependency needed (`sdf_storage` is already required for `sdf_storage_web_user_t`). `mbedtls` already builds and is tested for `IDF_TARGET=linux` elsewhere in this repo (Nuki crypto suite), so this is low-risk.

**`sdf_ble_companion.c` and `sdf_app.c` each gain one new `#include "sdf_services.h"`.**
Neither currently includes it (confirmed: no `sdf_services` symbol is referenced anywhere in `sdf_ble_companion`'s sources today, despite the `REQUIRES` already being present) — `sdf_app.c` already includes it transitively via other services headers but should include it directly for the new calls.

## Risks / Trade-offs

- [Refactor could subtly change behavior despite being "pure extraction" — e.g. a copy-paste changes hash-length validation order, or the `mbedtls_ct_memcmp` call stops being genuinely constant-time once wrapped] → Preserve exact current buffer sizes (`SDF_STORAGE_WEB_USER_HASH_LEN`) and comparison semantics; since none of `sdf_ble_companion`/`sdf_app`'s changed call sites are `linux`-testable, do a hardware `flash monitor` smoke test of LOGIN success/fail, REGISTER approved/denied/timeout before merging — this is the one place the new unit tests can't substitute for real-hardware verification.
- [`sdf_services` is already a large, multi-file component; adding a fifth responsibility grows its surface further] → Mitigated by the new file being self-contained (`sdf_services_web_auth.c`) and exposing only the three functions above, not merging internals into `sdf_services.c` or `_admin.c`.
- [Adding `mbedtls` as a `sdf_services` dependency could theoretically affect hardware build size] → It's already linked into the firmware via `sdf_ble_companion`/`sdf_protocol_ble`; this adds a new caller, not a new library — negligible.

## Migration Plan

1. Add `PRIV_REQUIRES mbedtls` to `sdf_services/CMakeLists.txt`; confirm `sdf_services` still builds for both `linux` and `esp32c6` targets with no other change.
2. Implement `sdf_services_web_auth.c` and the three functions/types above; add their declarations to `sdf_services.h`.
3. Add Unity cases to `firmware/components/sdf_services/test/test_sdf_services.c`: valid/invalid login hash (including mismatched `hash_len`, all-zero hash, wrong-user lookup miss handled by caller not this function), register-authorized → correct `sdf_storage_web_user_t` populated, register-denied/timeout → `should_persist == false`, and the resolve-guard's true/false cases across all `sdf_services_admin_action_t` values (not just `WEB_REG_AUTH`) to lock in the "only this action type triggers resolution" guarantee. Wire into `test_runner_main.c`.
4. Update `sdf_ble_companion_auth_access()`'s `LOGIN` branch to call `sdf_services_web_auth_verify_login()` instead of inlining the compare.
5. Update `sdf_app_on_web_reg_auth_result()` and `sdf_app_on_admin_action_complete()` to call the new decision functions, keeping only the I/O (`sdf_storage_web_user_save`, `sdf_ble_companion_reply_auth`) inline.
6. Hardware smoke test (per Risks above): LOGIN with correct/incorrect password, REGISTER with admin-approve/admin-deny/timeout, confirm identical behavior to pre-change.
7. No data migration — NVS layout for `sdf_storage_web_user_t` is untouched. Rollback is a plain revert; no runtime state to unwind.

## Open Questions

- Should the storage slot-allocation loop in `sdf_app_on_web_reg_auth_result` move into a `sdf_storage_web_user_add()` convenience (auto-picks the first free slot) in a follow-up, closing the one remaining untested sliver of the registration path? Not resolved here — flagged as a possible small follow-up once this change lands and its test coverage patterns are established.
- Is `sdf_services_web_auth_verify_login()` the right name/shape, or should login verification also own the `sdf_storage_web_user_find_by_name()` lookup (taking `username` + `submitted_hash` instead of a pre-looked-up `sdf_storage_web_user_t`)? Current design keeps the lookup in `sdf_ble_companion` (unchanged from today) to minimize the diff; worth revisiting if it turns out to want its own coverage too.

## 1. Introduce the origin type and the authorization helper

- [x] 1.1 Add an admin-action request origin enum (e.g. `SDF_SERVICES_ADMIN_ORIGIN_LOCAL_PHYSICAL`, `SDF_SERVICES_ADMIN_ORIGIN_REMOTE`) in `sdf_services_internal.h`.
- [x] 1.2 Add the helper in `sdf_services.c` taking `(sdf_services_admin_action_t action, origin)` and reporting whether it performed a bypassed execution, so the caller knows whether to fall through to the ordinary pending-action flow.
- [x] 1.3 Implement the helper's decision: bypass only when origin is local-physical **and** `sdf_services_enrolled_user_count()` is 0.
- [x] 1.4 Implement the bypassed execution body, moved verbatim in behavior from `sdf_button_dispatch_action()`: clear `pending_admin_action` and `pending_admin_action_start_us`, then route `ENROLL` to `led_pulse_blue()` + `sdf_services_request_enrollment(1, 3)` and every other action to the configured `admin_action_cb` with its `admin_action_ctx`.
- [x] 1.5 **Reproduce the lock discipline exactly** (design Decision 3): acquire `s->lock`, read the user count, clear the pending fields, **release the lock**, and only then execute. Nothing that can re-enter services code may run while the lock is held.
- [x] 1.6 Declare the helper in `sdf_services_internal.h`.

## 2. Route the existing call sites through it

- [x] 2.1 `sdf_services_button.c`: replace the inline zero-users branch in `sdf_button_dispatch_action()` (currently lines ~143-164) with a call to the helper passing local-physical origin; return early if it reports it executed.
- [x] 2.2 Confirm the remaining body of `sdf_button_dispatch_action()` — the pending-action gate and its LED switch — is unchanged, and that it still holds the lock across exactly the same span as before.
- [x] 2.3 `sdf_services.c`: route `sdf_services_request_admin_action()` through the helper passing remote origin, so its no-bypass behavior becomes an expressed decision. Confirm this does not perturb its existing pre-checks (`initialized`, `pending_admin_action != NONE`, `permission_change_pending`, `enrollment_request_pending`, `sdf_enrollment_sm_is_active()`) or its `admin_action_done_sem` drain.
- [x] 2.4 Update the doc comment on `sdf_button_dispatch_action()` — its current text explains why `NUKI_PAIR` can never reach the zero-users branch; that reasoning now belongs with the helper.

## 3. Verification

- [x] 3.1 Run the host test suite (`sdf_services`) — the existing tests drive `sdf_button_dispatch_action()` directly and cover both the zero-users and pending-action branches, so they are the primary regression gate. Confirm no changes needed.
- [x] 3.2 Add a host test asserting the bypass is **not** granted for remote origin on a zero-user device (the property that was previously implicit).
- [x] 3.3 Add a host test asserting the bypass clears any pre-existing pending action before executing.
- [x] 3.4 Add a host test covering the non-`ENROLL` bypass route (action reaches `admin_action_cb`, no pending action left set) — this path is currently specified only by the corrected requirement.
- [x] 3.5 **Deadlock check**: verify on real hardware (or emulator) with a factory-reset, zero-user device that a single-click starts enrollment and a double-click opens the pairing window, neither hanging. This is the failure mode design Decision 3 guards against and it must be exercised on the real lock, not just the host mock.
- [x] 3.6 Verify a claimed device (≥1 user) is unaffected: single-click still sets a pending action and awaits an admin fingerprint.

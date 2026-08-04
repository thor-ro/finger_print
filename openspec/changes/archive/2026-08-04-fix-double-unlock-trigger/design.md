## Context

The `unlock_cb` callback was the original unlock mechanism before the event router existed. After the event router was added, the match task was updated to emit `BIOMETRIC_MATCH` events, but the `unlock_cb` call was not removed. Both paths now coexist, causing double triggering.

## Goals / Non-Goals

**Goals:**
- Single unlock trigger path: event router only
- Remove the legacy `unlock_cb` / `unlock_ctx` fields
- Preserve identical user-visible behavior

**Non-Goals:**
- Changing the lock flow or BLE transport behavior
- Changing match task timing or cooldown logic

## Decisions

**Remove `unlock_cb` from `sdf_services_config_t`, remove call from match task.**

The event-router path is the canonical architecture per the task architecture spec. The `unlock_cb` is a legacy artifact. Removing it simplifies the match task and makes the unlock trigger deterministic (one event, one handler).

Steps:
1. In `sdf_services_match.c`: delete the `unlock_cb(unlock_ctx, match.user_id)` call and the local variable copies of `unlock_cb`/`unlock_ctx`
2. In `sdf_services_internal.h` or `sdf_services.h`: remove `unlock_cb` and `unlock_ctx` fields from `sdf_services_config_t`
3. In `sdf_services.c` (`sdf_services_get_default_config`): remove those fields from default init
4. In `sdf_app.c`: remove `sdf_app_on_fingerprint_unlock` and the lines assigning it to `services_cfg`
5. Update any tests that set `unlock_cb`

## Risks / Trade-offs

- [Test breakage] Tests that directly set `unlock_cb` need updating — they should instead subscribe to `BIOMETRIC_MATCH` events or verify via the event router
- [Admin-claim path] `sdf_services_try_claim_admin_action(&match)` is called before `unlock_cb` in the match task. The admin-claim check runs before the event emit, so ordering is preserved

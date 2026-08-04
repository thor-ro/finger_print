## Why

A successful fingerprint match triggers the unlock via two paths simultaneously:

1. **Direct callback**: `unlock_cb(unlock_ctx, match.user_id)` → `sdf_app_on_fingerprint_unlock()` → `sdf_app_lock_action(UNLATCH, 0)`
2. **Event router**: `sdf_event_router_emit(SDF_EVENT_ROUTER_BIOMETRIC_MATCH)` → `sdf_app_on_event()` → `sdf_app_lock_action(UNLATCH, 0)`

Both paths call `sdf_app_lock_action()` for the same action. The lock flow state guard catches the second call when BLE is connected, but under BLE-not-ready conditions, two queued actions are set up, leading to a redundant second BLE unlock attempt after reconnect. The design is fragile; the `unlock_cb` callback predates the event router and should be removed in favor of the event-driven path.

## What Changes

- Remove the `unlock_cb` / `unlock_ctx` fields from `sdf_services_config_t` (or deprecate them)
- The match task should only emit `SDF_EVENT_ROUTER_BIOMETRIC_MATCH` — not call any callback directly
- `sdf_app_init()` removes the `unlock_cb` assignment; the event handler in `sdf_app_on_event()` remains the single unlock trigger
- Remove `sdf_app_on_fingerprint_unlock()` static function

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- None (behavior is preserved; only the trigger path is deduplicated)

## Impact

- `firmware/components/sdf_services/src/sdf_services_match.c` — remove `unlock_cb` call, keep event emission
- `firmware/components/sdf_services/include/sdf_services.h` — remove `unlock_cb`/`unlock_ctx` from config struct
- `firmware/components/sdf_app/src/sdf_app.c` — remove `sdf_app_on_fingerprint_unlock`, remove `unlock_cb` assignment
- Any test that exercises the `unlock_cb` path must be updated

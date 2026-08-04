## Why

`sdf_ble_companion.c` creates a FreeRTOS mutex (`s_lock`) in `init()` but **never takes or gives it** anywhere. The GATT access callbacks (`sdf_ble_companion_auth_access`, `_config_access`, etc.) run from the NimBLE host task, while reply/notify functions are called from the app task. Shared state (`s_connections[]`, `s_auth_value[]`, `s_config_value[]`, `s_enroll_value[]`, `s_ota_value[]`) is therefore accessed concurrently without synchronization — a data race that can corrupt connection state and authentication results.

## What Changes

- Add `xSemaphoreTake` / `xSemaphoreGive` guards around all accesses to `s_connections[]` and the shared value buffers
- Protect `sdf_ble_companion_get_conn()` and `sdf_ble_companion_get_free_conn()` calls with the mutex
- Protect all notify/reply public functions that access shared state from the app task

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- None (internal synchronization fix, no API change)

## Impact

- `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c` — mutex take/give around shared state access
- No header or API changes
- Deadlock risk is low: the mutex is held only for short non-blocking sections; NimBLE callbacks must not block

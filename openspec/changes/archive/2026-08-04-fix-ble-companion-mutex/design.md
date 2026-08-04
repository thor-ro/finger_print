## Context

`sdf_ble_companion.c` allocates a mutex in `init()` but never uses it. GATT access callbacks are called from the NimBLE host task context. The public API functions (`reply_auth`, `notify_*`) are called from the app/event-router task. Both access `s_connections[]` and the value buffers without any lock.

## Goals / Non-Goals

**Goals:**
- Protect all concurrent accesses to `s_connections[]` with the existing `s_lock` mutex
- Protect shared value buffers (`s_auth_value`, `s_config_value`, `s_enroll_value`, `s_ota_value`) for the notify/reply paths
- Avoid deadlock: mutex must never be held across blocking calls (BLE stack calls must be made outside the lock)

**Non-Goals:**
- Restructuring the connection model
- Per-connection value buffers (tracked as separate change `fix-ble-per-connection-buffers`)

## Decisions

**Pattern: lock → copy state → unlock → call BLE stack.**

NimBLE GAP/GATT callbacks cannot be blocked. The mutex must be acquired for the minimum duration to read/write shared state, then released before calling any BLE stack API.

```c
// Example pattern for notify:
if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
    conn = sdf_ble_companion_get_conn(conn_handle);
    if (!conn || !conn->connected || conn->auth_state != AUTHENTICATED) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(s_config_value, data, len);
    s_config_value_len = len;
    xSemaphoreGive(s_lock);
}
// BLE call outside lock:
rc = ble_gatts_notify_custom(conn_handle, s_config_val_handle, om);
```

The GAP event callback (running in NimBLE task) must also use `xSemaphoreTake` with a short timeout (e.g., `pdMS_TO_TICKS(10)`) to avoid starving the NimBLE host. Use `pdMS_TO_TICKS(100)` for app-task callers.

## Risks / Trade-offs

- [NimBLE reentrancy] NimBLE is single-threaded internally; GATT access callbacks are always serialized within the NimBLE task. The mutex protects against cross-task access, not reentrance.
- [Timeout strategy] Using `portMAX_DELAY` in app-task callers is safe. GAP callbacks should use a short timeout and log a warning on contention.

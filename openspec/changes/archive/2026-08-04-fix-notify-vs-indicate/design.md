## Context

NimBLE characteristics are either Notify (CCCD bit 0, no ACK) or Indicate (CCCD bit 1, requires ACK). The current code declares all characteristics as INDICATE but calls `ble_gatts_notify_custom()` for outgoing messages. The web companion's `startNotifications()` only subscribes to Notify CCCD bit. Result: auth result, OTA result never reaches the browser.

## Goals / Non-Goals

**Goals:**
- Align characteristic declaration flags with the actual transmission API used
- Ensure the auth result reliably reaches the web companion after login/register
- Document the chosen delivery semantic (notify vs indicate) per characteristic

**Non-Goals:**
- Changing the GATT service UUID or characteristic layout
- Implementing per-characteristic reliability levels beyond the chosen semantic

## Decisions

**Use NOTIFY on all characteristics, not INDICATE.**

Rationale:
- The web companion already calls `startNotifications()` (notify subscriber)
- Indicate requires the client to implement ATT confirmation; the current JS does not
- For this low-security companion app, notify is sufficient — retransmission at the application level (e.g., user retries auth if result doesn't arrive) is acceptable
- Using INDICATE on only the auth characteristic and updating the JS is an option but adds asymmetry

Change characteristic flags:
```c
// Before:
.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE,
// After:
.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
```

Change `sdf_ble_companion_set_authenticated()`:
```c
// Before:
ble_gatts_indicate_custom(conn->conn_handle, s_auth_val_handle, om);
// After:
ble_gatts_notify_custom(conn->conn_handle, s_auth_val_handle, om);
```

## Risks / Trade-offs

- [Delivery guarantee] Notify has no ACK; if the BLE radio drops the packet, the client never learns the auth result. Mitigation: the client can poll by reading the characteristic, or retry the auth flow.
- [Breaking change for existing clients] Any client subscribed to indications will need to re-subscribe to notifications. Not a real concern since no production clients exist yet.

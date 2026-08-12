## 1. Compile-Time Configuration

- [x] 1.1 Add compile-time constants for pairing-window duration (default 60000 ms) and failed-login threshold (default 3) in `sdf_ble_companion`, mirroring the existing `SDF_BLE_COMPANION_*` `#define` pattern.
- [x] 1.2 Confirm `CONFIG_BT_NIMBLE_WHITELIST_SIZE=12` remains sufficient for the allow list (bounded by `CONFIG_BT_NIMBLE_MAX_BONDS=3`) and document why in a comment near the constant.

## 2. Allow-List & Bond-Tracking State

- [x] 2.1 Add an in-memory allow-list/bond-tracking table to `sdf_ble_companion`, keyed by resolved BLE identity address, holding an allow-listed flag and a failed-login counter per identity.
- [x] 2.2 Add helpers to add/remove an identity from the allow list and to look up/reset/increment its failed-login counter.
- [x] 2.3 Push allow-list changes into the NimBLE Filter Accept List whenever the table changes.

## 3. Sparse, Allow-List-Filtered Default Advertising

- [x] 3.1 Set the default-mode advertising filter policy to require accept-list matching for connection requests.
- [x] 3.2 Replace the current always-on fast-then-slow-forever advertising loop with a sparse duty cycle for default mode.
- [x] 3.3 Confirm the Nuki central role (`sdf_nuki_ble_transport.c`) is unaffected, since it never uses the Filter Accept List (`ble_gap_disc()` + direct-address connect).

## 4. Admin-Fingerprint-Gated Pairing Window

- [x] 4.1 Add a new `sdf_services_admin_action_t` value for requesting the BLE Companion pairing window.
- [x] 4.2 Wire the new action through the existing `pending_admin_action` gate in `sdf_services.c` (LED indication, `admin_action_cb` dispatch on fingerprint success/denial/timeout).
- [x] 4.3 On successful completion of this admin action, have `sdf_ble_companion` switch advertising to unfiltered for the compile-time window duration.
- [x] 4.4 On bond completion (`BLE_GAP_EVENT_ENC_CHANGE`) while the window is open: add the resolved identity to the allow list, close the window immediately (revert to sparse/filtered advertising), and cancel the window's own timeout.
- [x] 4.5 On window timeout with no completed bond: close the window and revert to sparse/filtered advertising.
- [x] 4.6 Confirm connections that never complete bonding during the window are ignored - no allow-list mutation, no window-state change, window stays open until a real bond or the timeout.

## 5. Button Gesture Rebind

- [x] 5.1 In `sdf_services_button.c`, register `BUTTON_DOUBLE_CLICK` against `sdf_button_cb` with the new admin action, mirroring the existing hold-8s (`FACTORY_RESET`) registration pattern.
- [x] 5.2 Update the comment documenting Double-Press as "retired" / "free for future use" to reflect its new binding.

## 6. Failed BLE Login Lockout

- [x] 6.1 In `sdf_ble_companion_auth_access`'s LOGIN branch, on hash mismatch, increment the connecting identity's failed-login counter via the bond-tracking state added in section 2.
- [x] 6.2 On successful LOGIN, reset the identity's failed-login counter to zero.
- [x] 6.3 When the counter reaches the compile-time threshold, remove the identity's NimBLE bond record and allow-list entry, then terminate the connection (`ble_gap_terminate`) as part of the same handling path.
- [x] 6.4 Confirm the counter is keyed by resolved identity (not `conn_handle`), so it survives disconnect/reconnect, and confirm it is never written to NVS.

## 7. Tests

- [x] 7.1 Unit tests for the failed-login counter state machine: increment on failure, reset on success, eviction at threshold, value retained across a simulated reconnect, value cleared on simulated reinit/reboot.
- [x] 7.2 Unit tests for pairing-window admission: first bond closes the window and allow-lists the identity; an incomplete connection is ignored and leaves the window open; timeout with no bond closes the window.
- [x] 7.3 Test confirming an allow-listed device connects successfully while a non-allow-listed device cannot, under filtered advertising.
- [x] 7.4 Button-dispatch test confirming double-click requests the new admin action, and is ignored when a different admin action is already pending.

## 8. Documentation

- [x] 8.1 Update `doc/ble_configuration.md` to describe the sparse/filtered advertising default and the pairing-window flow, replacing the outdated speculative "Configuration Mode Button" section with what was actually built.

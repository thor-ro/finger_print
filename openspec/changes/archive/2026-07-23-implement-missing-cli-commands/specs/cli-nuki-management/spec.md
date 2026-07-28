# CLI Nuki Management Capability

## Overview
Monitor and control Nuki Smart Lock 3 Pro pairing/connection state via USB-C CLI for field diagnostics and standalone provisioning.

## Requirements

### REQ-CLI-NUKI-001: Status
**User Story**: As a field technician, I want to see Nuki pairing/connection status so I can diagnose BLE issues.
- **CLI**: `nuki status`
- **Output**:
  ```
  Nuki Status:
  Paired: yes/no
  Authorization ID: <hex> (if paired)
  BLE Transport: ready/connecting/disconnected
  Last Keyturner State: locked/unlocked/unlatched/unknown (timestamp)
  Signal RSSI: <dBm> (if connected)
  ```
- **Backend**: 
  - `sdf_storage_nuki_load()` → check credentials exist
  - `sdf_nuki_ble_is_ready(&s_ble)` → transport state
  - `sdf_app_request_keyturner_state()` → last known state (cached)
- **Auth**: Requires CLI login

### REQ-CLI-NUKI-002: Connect
**User Story**: As a field technician, I want to manually initiate BLE connection to the Nuki lock.
- **CLI**: `nuki connect`
- **Behavior**:
  - If already connected: "Already connected"
  - If not paired: "Not paired — run 'nuki pair' first"
  - Else: enable BLE transport, start connection
- **Backend**: `sdf_nuki_ble_set_enabled(&s_ble, true)` + `sdf_nuki_ble_start(&s_ble)`
- **Auth**: Requires CLI login

### REQ-CLI-NUKI-003: Pair
**User Story**: As an installer, I want to pair the device to a Nuki lock via CLI as alternative to button flow.
- **CLI**: `nuki pair`
- **Prerequisites**: 
  - Nuki lock in pairing mode (hold button 5s until LED solid)
  - Device not already paired (or will overwrite)
- **Flow**:
  1. Enable BLE transport
  2. Init pairing: `sdf_nuki_pairing_init(&s_pairing, &s_client, 1, SDF_APP_ID, SDF_APP_NAME)`
  3. Start: `sdf_nuki_pairing_start(&s_pairing)` → sends pairing request
  4. Wait for Nuki challenge response (automatic via BLE callbacks)
  5. On completion: save credentials via `sdf_storage_nuki_save(auth_id, shared_key)`
  6. Output: "Pairing complete. Authorization ID: <hex>"
- **Auth**: Requires CLI login + admin fingerprint auth (triggers `SDF_SERVICES_ADMIN_ACTION_NUKI_PAIR`)

### REQ-CLI-NUKI-004: Unpair
**User Story**: As an installer, I want to unpair from the Nuki lock to reset for a different lock.
- **CLI**: `nuki unpair`
- **Behavior**:
  - Clear stored credentials: `sdf_storage_nuki_clear()` + `sdf_storage_ble_target_clear()`
  - Stop BLE transport: `sdf_nuki_ble_stop(&s_ble)`
  - Reset pairing state
  - Output: "Nuki unpair complete. Device ready for new pairing."
- **Auth**: Requires CLI login + admin fingerprint auth

## Acceptance Criteria
- All commands require authenticated CLI session
- Pair/unpair require admin fingerprint authorization (10s timeout)
- Status shows actionable info for diagnostics
- Connect works after pair without reboot
- Unpair fully resets Nuki state (credentials + BLE)

## Non-Functional
- Pairing flow reuses existing `sdf_nuki_pairing` state machine
- No new BLE logic — only CLI command wiring
- Compatible with button-initiated pairing (same backend)
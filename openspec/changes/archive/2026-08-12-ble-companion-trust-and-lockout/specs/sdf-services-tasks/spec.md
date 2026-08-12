## REMOVED Requirements

### Requirement: Double-Press Gesture Retired
**Reason**: Double-Press is being reactivated to trigger the BLE Companion Service's new admin-fingerprint-gated device pairing window (see the `ble-companion-service` capability's "Admin-Fingerprint-Gated Device Pairing Window" requirement).
**Migration**: No caller action needed. The button task simply gains a callback registration for `BUTTON_DOUBLE_CLICK` where none previously existed; no other gesture mapping changes.

## ADDED Requirements

### Requirement: Double-Press Requests BLE Companion Pairing Window
The button task SHALL bind `BUTTON_DOUBLE_CLICK` to request the BLE Companion Service's admin-fingerprint-gated device pairing window, following the same `pending_admin_action` authorization flow used by every other admin action.

#### Scenario: Double-click requests the pairing window
- **WHEN** a double-click occurs on the physical button
- **AND** no other admin action is currently pending
- **THEN** the system sets `pending_admin_action` to request the BLE Companion pairing window
- **AND** awaits an Admin fingerprint scan within the pending-action timeout, per the existing admin-fingerprint pending-action pattern

#### Scenario: Double-click ignored while another admin action is pending
- **WHEN** a double-click occurs
- **AND** `pending_admin_action` is already set to a different action
- **THEN** the double-click SHALL NOT change the pending action

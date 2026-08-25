## MODIFIED Requirements

### Requirement: Double-Press Requests BLE Companion Pairing Window
The button task SHALL bind `BUTTON_DOUBLE_CLICK` to request the BLE Companion Service's admin-fingerprint-gated device pairing window, following the same `pending_admin_action` authorization flow used by every other admin action. This binding SHALL apply only once the device's setup-completion latch is set. While the device is in the setup phase, a button press SHALL instead reclaim the setup connection and re-arm the setup phase, and SHALL set no pending admin action.

#### Scenario: Double-click requests the pairing window
- **WHEN** a double-click occurs on the physical button
- **AND** the setup-completion latch is set
- **AND** no other admin action is currently pending
- **THEN** the system sets `pending_admin_action` to request the BLE Companion pairing window
- **AND** awaits an Admin fingerprint scan within the pending-action timeout, per the existing admin-fingerprint pending-action pattern

#### Scenario: Double-click ignored while another admin action is pending
- **WHEN** a double-click occurs
- **AND** `pending_admin_action` is already set to a different action
- **THEN** the double-click SHALL NOT change the pending action

#### Scenario: Double-click during the setup phase does not request a pairing window
- **WHEN** a double-click occurs while the setup-completion latch is unset
- **THEN** no pairing window is requested and no pending admin action is set
- **AND** the press is handled as a setup-phase reclaim/re-arm

## REMOVED Requirements

### Requirement: State-Dependent Single-Click Setup Action
**Reason**: First-time setup is now guided exclusively by the Web Companion App. Neither Admin enrolment nor Nuki pairing is reachable by a button gesture, so single-click has no setup action left to resolve, and setup state is no longer derived from enrolled-user count plus persisted Nuki credentials but read from a latched flag.

**Migration**: Enrol the first Admin and pair the Nuki through the setup wizard in the Web Companion App. Enrolment of additional users after setup completion remains available through the companion app's Enrollment characteristic.

### Requirement: Nuki Pairing Unreachable By Button After Setup Complete
**Reason**: Nuki pairing is no longer reachable by any button gesture in any setup state, so a requirement scoped to the post-completion case is subsumed. Initial pairing happens in the setup wizard; re-pairing afterwards uses the existing admin-fingerprint-gated companion request.

**Migration**: Use the wizard for initial pairing and "Request Nuki Re-pair" in the companion app afterwards. A factory reset still clears Nuki credentials and returns the device to the setup phase.

### Requirement: Simplified Pre-Enrollment Bootstrap Branch
**Reason**: The bootstrap branch existed so a button gesture on a device with zero enrolled users could execute without an Admin fingerprint that did not yet exist. No button gesture now triggers an admin action during the setup phase, and factory reset requires no fingerprint in any state, so the branch has no remaining callers.

**Migration**: None. Admin enrolment on an unclaimed device is performed by the setup wizard over an unauthenticated setup-phase connection, which is gated by the setup phase itself rather than by an action-level bypass.

### Requirement: Unauthenticated Bootstrap Bypass Is Restricted To Local Physical Origin
**Reason**: The bypass this requirement constrained no longer exists. Admin actions are never executed without authorization; the unclaimed device's openness is now a property of the time-bounded, singly-occupied setup phase rather than of an origin-dependent exemption on individual actions.

**Migration**: None. Request origin no longer affects admin-action authorization.

### Requirement: Bootstrap Bypass Decision Is Single-Sited
**Reason**: With no bootstrap bypass remaining, there is no bypass decision to site in one place. Every admin-action request path now follows the ordinary admin-fingerprint pending-action flow without exception.

**Migration**: None. New admin-action request paths follow the ordinary pending-action flow and need not pass a request origin.

## Why

The BLE Companion Service currently bonds with any device in range via Just Works pairing (no MITM, no identity check) and never rate-limits failed LOGIN attempts on the Auth characteristic. Once bonded, an attacker within radio range can hammer the password-hash comparison indefinitely with no lockout, unlike the biometric match path which already has a 5-in-60s -> 120s lockout. There is currently no way to restrict which physical devices are even allowed to reach the GATT server, so a stolen/leaked credential works from any phone, forever.

## What Changes

- Companion Service advertising switches from continuous, unfiltered, undirected advertising to sparse, accept-list-filtered advertising by default. Devices not already on the allow list can no longer complete a connection at all. (No previously bonded companion devices exist in the field yet, so this ships greenfield with no migration/grandfathering concern - every device is paired through the new flow from the start.)
- New one-shot, admin-fingerprint-gated pairing window: the (now reactivated) double-click button gesture requests a new admin action; once an Admin fingerprint is scanned, a time-boxed (compile-time constant, default 60s) unfiltered advertising window opens. The first device to complete bonding during that window is added to the allow list and the window closes immediately. Stray or incomplete connection attempts (never completing bonding) are ignored and do not hold the window open or consume it.
- Double-Press gesture is reactivated to trigger the new "request pairing window" admin action, superseding the existing "Double-Press Gesture Retired" requirement.
- New failed-login lockout on the Auth characteristic's LOGIN command: a per-bonded-identity failed-attempt counter, held in the Companion Service's in-memory bond-tracking state (survives disconnect/reconnect within uptime, intentionally not persisted across reboot). On the 3rd (compile-time constant) consecutive failed LOGIN from a given bonded identity, that identity's bond record and allow-list entry are removed and its live connection is terminated immediately. Regaining access requires the full pairing-window flow again (fresh identity, fresh counter).
- Explicitly out of scope for this change: salting/KDF-hardening the stored password hash and any wire-protocol change to stop a stolen hash from being directly replayable. Tracked as a separate follow-on.

## Capabilities

### New Capabilities
(none - this extends the existing BLE Companion Service and button-dispatch capabilities)

### Modified Capabilities
- `ble-companion-service`: adds sparse/allow-list-filtered advertising as the default connectable state, adds the admin-fingerprint-gated pairing-window trigger and allow-list admission behavior, and adds failed-login lockout with bond eviction to the existing BLE GATT Authentication requirement.
- `sdf-services-tasks`: replaces the "Double-Press Gesture Retired" requirement - double-click now dispatches the new pairing-window admin action instead of remaining unbound.

## Impact

- `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c`: advertising setup (filter policy, sparse interval/duty cycle), GAP event handling (bond completion detection to close the pairing window and to key the fail counter), Auth characteristic LOGIN handling (fail counting, eviction, connection termination), new in-memory allow-list/bond-tracking state.
- `firmware/components/sdf_services/include/sdf_services.h` and `src/sdf_services.c`: new `sdf_services_admin_action_t` value for the pairing-window request, dispatch/authorization plumbing reusing the existing single-slot `pending_admin_action` mechanism.
- `firmware/components/sdf_services/src/sdf_services_button.c`: register `BUTTON_DOUBLE_CLICK` against the new admin action.
- NimBLE/Kconfig: accept-list (filter policy) configuration, compile-time constants for pairing-window duration and failed-login threshold.
- Firmware host tests (`firmware/components/sdf_ble_companion/test`, `firmware/components/sdf_services/test`): coverage for lockout counter transitions, window admission/closing, and button dispatch of the new gesture.
- `doc/ble_configuration.md`, `openspec/specs/ble-companion-service/spec.md`, `openspec/specs/sdf-services-tasks/spec.md`: documentation/spec updates.

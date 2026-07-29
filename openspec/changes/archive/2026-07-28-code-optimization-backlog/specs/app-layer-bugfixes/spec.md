## ADDED Requirements

### Requirement: No duplicate variable declarations for s_pairing_active and s_pairing_requested
`sdf_app.c` SHALL not contain duplicate `static bool` declarations for `s_pairing_active` and `s_pairing_requested`. Each variable SHALL be declared exactly once.

#### Scenario: s_pairing_active is declared once
- **WHEN** `sdf_app.c` is compiled
- **THEN** there is exactly one `static bool s_pairing_active;` declaration at module scope

#### Scenario: s_pairing_requested is declared once
- **WHEN** `sdf_app.c` is compiled
- **THEN** there is exactly one `static bool s_pairing_requested;` declaration at module scope

### Requirement: Event subscriptions are cleaned up on init failure
`sdf_app_init()` SHALL unsubscribe all previously subscribed event handlers when any subscription fails, then return the error.

#### Scenario: Subscription #5 fails, subscriptions #1–4 are cleaned up
- **WHEN** `sdf_event_router_subscribe()` returns an error for the 5th subscription
- **THEN** `sdf_app_init()` unsubscribes subscriptions 1–4 using their handle pointers
- **AND** returns the error code

### Requirement: Alarm mask is only updated when it actually changes
`sdf_app_set_alarm_mask_bits()` SHALL compare the new mask to the current mask before calling `sdf_protocol_zigbee_update_alarm_mask()`.

#### Scenario: Setting bits that are already set does not trigger Zigbee update
- **WHEN** `sdf_app_set_alarm_mask_bits()` is called with `set_bits` that are already set in `s_zigbee_alarm_mask` and `clear_bits` that are already clear
- **THEN** `s_zigbee_alarm_mask` is unchanged
- **AND** `sdf_protocol_zigbee_update_alarm_mask()` is NOT called

#### Scenario: Actually changing the alarm mask triggers Zigbee update
- **WHEN** `sdf_app_set_alarm_mask_bits()` is called with bits that change `s_zigbee_alarm_mask`
- **THEN** `sdf_protocol_zigbee_update_alarm_mask()` IS called with the new mask value
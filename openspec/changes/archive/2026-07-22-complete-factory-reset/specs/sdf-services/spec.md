## MODIFIED Requirements

### Requirement: State Reset
The `sdf_services` component SHALL provide a function to reset all internal state to defaults.

#### Scenario: Reset internal state
- **WHEN** `sdf_services_reset_state()` is called
- **THEN** Reset `enrolled_user_count` to 0
- **THEN** Reset `failed_attempt_count` to 0
- **THEN** Reset `lockout_until_us` to 0
- **THEN** Reset `pending_admin_action` to `SDF_SERVICES_ADMIN_ACTION_NONE`
- **THEN** Reset `match_cooldown_until_us` to 0
- **THEN** Reset enrollment state machine to IDLE
- **THEN** Turn off LED via `led_off()`
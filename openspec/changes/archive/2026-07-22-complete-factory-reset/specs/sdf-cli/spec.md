## MODIFIED Requirements

### Requirement: Factory Reset CLI Command
The `sdf_cli` component SHALL provide a `factory_reset` command that executes a complete factory reset.

#### Scenario: Factory reset via CLI
- **WHEN** User enters `factory_reset` in CLI
- **THEN** System prints "This will erase ALL data and reboot. Type 'YES' to confirm:"
- **WHEN** User types "YES" and presses Enter
- **THEN** Call `sdf_storage_erase_all()`
- **THEN** Call `fp_delete_all_users()`
- **THEN** Call `sdf_protocol_zigbee_factory_reset()`
- **THEN** Call `sdf_services_reset_state()`
- **THEN** Call `esp_restart()`
- **THEN** Device reboots to UNCLAIMED state

#### Scenario: CLI confirmation required
- **WHEN** User enters `factory_reset` but types anything other than "YES"
- **THEN** System prints "Aborted."
- **THEN** No reset occurs
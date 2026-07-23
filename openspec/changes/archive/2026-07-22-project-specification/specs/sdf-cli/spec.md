## ADDED Requirements

### Requirement: CLI Registration
The `sdf_cli` component SHALL register ESP-IDF console commands for debugging and diagnostics.

#### Scenario: Command registration
- **WHEN** `sdf_cli_register()` called
- **THEN** Register commands: `sdf_status`, `sdf_fp_test`, `nuki`, `zigbee`, `fp`, `led`, `battery`, `storage`, `power`, `factory_reset`

### Requirement: Status Command
The `sdf_cli` component SHALL provide a status command showing system state.

#### Scenario: sdf_status output
- **WHEN** `sdf_status` entered
- **THEN** Print: device claimed (Y/N), enrolled users, lock state, paired (Y/N), battery %, Zigbee network state, uptime, free heap

### Requirement: Fingerprint Test Commands
The `sdf_cli` component SHALL provide fingerprint sensor test commands.

#### Scenario: fp_match
- **WHEN** `fp match` entered
- **THEN** Trigger single match cycle, print result (user_id, permission, confidence)

#### Scenario: fp_enroll
- **WHEN** `fp enroll <user_id> <perm>` entered
- **THEN** Start 3-step enrollment for user_id with permission

#### Scenario: fp_delete
- **WHEN** `fp delete <user_id>` entered
- **THEN** Delete specific user

#### Scenario: fp_list
- **WHEN** `fp list` entered
- **THEN** Print all enrolled users with IDs and permissions

### Requirement: Nuki BLE Commands
The `sdf_cli` component SHALL provide Nuki BLE test commands.

#### Scenario: nuki_pair
- **WHEN** `nuki pair` entered
- **THEN** Start pairing process, print progress

#### Scenario: nuki_lock
- **WHEN** `nuki lock <action>` entered (action: lock/unlock/unlatch)
- **THEN** Send lock action, print result

#### Scenario: nuki_status
- **WHEN** `nuki status` entered
- **THEN** Print pairing status, auth_id, shared_key (redacted), target address

### Requirement: Zigbee Commands
The `sdf_cli` component SHALL provide Zigbee test commands.

#### Scenario: zb_join
- **WHEN** `zb join` entered
- **THEN** Start Zigbee network steering

#### Scenario: zb_state
- **WHEN** `zb state` entered
- **THEN** Print network state, PAN ID, channel, parent, LQI

#### Scenario: zb_report
- **WHEN** `zb report <attr>` entered
- **THEN** Force attribute report (lock_state, battery, alarm_mask, user_list)

### Requirement: LED Test Commands
The `sdf_cli` component SHALL provide LED pattern test commands.

#### Scenario: led_test
- **WHEN** `led test <pattern>` entered (pattern: green/red/blue/yellow/cyan/white/breathe/pulse)
- **THEN** Show pattern for 5 seconds

### Requirement: Battery Commands
The `sdf_cli` component SHALL provide battery test commands.

#### Scenario: battery_read
- **WHEN** `battery read` entered
- **THEN** Print raw ADC, voltage (mV), percentage

### Requirement: Storage Commands
The `sdf_cli` component SHALL provide NVS storage inspection commands.

#### Scenario: storage_dump
- **WHEN** `storage dump` entered
- **THEN** Print all SDF namespace keys and values (redact secrets)

#### Scenario: storage_erase
- **WHEN** `storage erase <key>` entered
- **THEN** Erase specific NVS key

### Requirement: Power Commands
The `sdf_cli` component SHALL provide power management test commands.

#### Scenario: power_sleep
- **WHEN** `power sleep` entered
- **THEN** Force immediate deep sleep entry

#### Scenario: power_wake_reason
- **WHEN** `power wake_reason` entered
- **THEN** Print last wake reason (GPIO, timer, etc.)

### Requirement: Factory Reset Command
The `sdf_cli` component SHALL provide factory reset command.

#### Scenario: factory_reset
- **WHEN** `factory_reset` entered
- **THEN** Confirm prompt, then call `sdf_app_on_admin_action(FACTORY_RESET)`
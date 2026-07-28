# CLI Zigbee Management Capability

## Overview
Monitor and control Zigbee network state via USB-C CLI for field diagnostics, commissioning, and network troubleshooting without a smart home coordinator UI.

## Requirements

### REQ-CLI-ZB-001: Status
**User Story**: As a field technician, I want to see Zigbee network status so I can diagnose connectivity issues.
- **CLI**: `zigbee status`
- **Output**:
  ```
  Zigbee Status:
  Enabled: yes/no (from Kconfig)
  Stack Started: yes/no
  Network Joined: yes/no
  PAN ID: 0xXXXX
  Channel: XX
  Short Address: 0xXXXX
  IEEE Address: XX:XX:XX:XX:XX:XX:XX:XX
  Check-in Interval: XX ms
  Parent RSSI: <dBm> (if joined)
  ```
- **Backend**:
  - `sdf_protocol_zigbee_is_enabled()` 
  - `sdf_protocol_zigbee_is_ready()` (stack started + joined)
  - ESP Zigbee stack APIs for PAN ID, channel, addresses
- **Auth**: Requires CLI login

### REQ-CLI-ZB-002: Connect (Permit Join)
**User Story**: As an installer, I want to initiate Zigbee network steering via CLI to join a coordinator.
- **CLI**: `zigbee connect`
- **Behavior**:
  - If already joined: "Already joined to network PAN 0xXXXX"
  - If disabled: "Zigbee disabled in build config"
  - Else: Start network steering via `sdf_protocol_zigbee_permit_join()`
  - Output: "Network steering started. Check coordinator for join request."
- **Backend**: `sdf_protocol_zigbee_permit_join()` → triggers `ESP_ZB_BDB_MODE_NETWORK_STEERING`
- **Auth**: Requires CLI login + admin fingerprint auth (triggers `SDF_SERVICES_ADMIN_ACTION_ZB_JOIN`)

### REQ-CLI-ZB-003: Unpair (Factory Reset Zigbee)
**User Story**: As an installer, I want to leave the Zigbee network and clear NVRAM via CLI.
- **CLI**: `zigbee unpair`
- **Behavior**:
  - If not joined: "Not joined to any network"
  - Else: Call `sdf_protocol_zigbee_factory_reset()` → leaves network, clears Zigbee NVRAM
  - Output: "Zigbee network left and NVRAM cleared"
- **Backend**: `sdf_protocol_zigbee_factory_reset()` (already implemented)
- **Auth**: Requires CLI login + admin fingerprint auth

## Acceptance Criteria
- All commands require authenticated CLI session
- Connect/unpair require admin fingerprint authorization (10s timeout)
- Status shows actionable network diagnostics
- Connect triggers same network steering as button hold (3s)
- Unpair fully resets Zigbee stack state (equivalent to factory reset Zigbee step)

## Non-Functional
- No new Zigbee logic — only CLI command wiring to existing `sdf_protocol_zigbee` APIs
- Compatible with button-initiated join (same backend)
- Works whether Zigbee enabled in build or not (graceful "disabled" message)
## Context

Config and enroll characteristics exist in the GATT service but their app-side callbacks do nothing. The web companion has a "Read Config" button with no handler, and no enrollment UI. These are the core management features the companion app is designed to provide.

## Goals / Non-Goals

**Goals:**
- Define a wire protocol for config read/write over the Config characteristic
- Implement config read (send current snapshot) and write (apply delta) on the firmware side
- Define a wire protocol for enrollment trigger over the Enroll characteristic
- Implement enrollment trigger and progress notification on the firmware side
- Implement the corresponding UI in the web companion

**Non-Goals:**
- Full config editor for all fields — scope to a useful subset (LED brightness, sleep timeouts, Nuki target address)
- Real-time enrollment step-by-step progress streaming (deliver final result only in v1)

## Decisions

**Wire format: JSON over the characteristic for both config and enroll.**

Config write payload: `{"field":"value", ...}` — only recognized fields are applied.
Config read response: full JSON snapshot of mutable config fields sent back as a notify.
Enroll write payload: `{"user_id": N, "permission": P}` — triggers `sdf_services_request_enrollment()`.
Enroll notify: `{"status": "started"|"success"|"failed", "user_id": N}`.

The JSON approach reuses the existing cJSON dependency already present in the OTA module.

## Risks / Trade-offs

- [BLE MTU limit] The default ATT MTU is 23 bytes; a JSON config snapshot easily exceeds this. The companion app must negotiate a larger MTU (up to 512 bytes, matching `SDF_BLE_COMPANION_ATTR_MAX_LEN`) before reading. Add MTU negotiation to the web companion's connect flow.
- [Enrollment concurrency] Enrollment can only be active one at a time. The enroll characteristic write must return an error response if enrollment is already active.

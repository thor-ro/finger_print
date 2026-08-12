## Context

See proposal.md - Why. Relevant current-state facts that shape the approach:

- The shared NimBLE host already bonds (`sm_bonding = 1`, `sm_sc = 1`, `sm_our_key_dist`/`sm_their_key_dist` include `BLE_SM_PAIR_KEY_DIST_ID`) with Just Works pairing (`sm_io_cap = BLE_SM_IO_CAP_NO_IO`, `sm_mitm = 0`). This already gives every bonded peer a stable *resolved identity* (via IRK) independent of MAC rotation, and NimBLE already persists bonds to NVS (`CONFIG_BT_NIMBLE_NVS_PERSIST=y`, `CONFIG_BT_NIMBLE_MAX_BONDS=3`). A Filter Accept List is already provisioned with headroom (`CONFIG_BT_NIMBLE_WHITELIST_SIZE=12`).
- The Nuki central role never touches the Filter Accept List (it scans generally via `ble_gap_disc()` and connects to the lock by direct address), so the Companion Service (peripheral role) is free to own that shared HW resource exclusively.
- Advertising today (`sdf_ble_companion_start_advertising_fast/slow`) is undirected, general-discoverable, unfiltered, and runs forever once the host syncs.
- The admin-fingerprint pending-action mechanism (`sdf_services_request_admin_action` / `s_state.pending_admin_action`) already exists and is reused unchanged by this design; it supports exactly one pending action system-wide.
- Greenfield deployment: no companion devices are bonded in the field yet, so there is no backward-compatibility/migration path to design for.

## Goals / Non-Goals

**Goals:**
- Prevent any BLE device not explicitly admin-approved from ever reaching the Auth/Config/Enrollment/OTA characteristics.
- Rate-limit and ultimately evict a bonded device that repeatedly fails LOGIN, without adding any new persistent storage.
- Reuse existing NimBLE bonding/identity and the existing single-slot admin-action pattern rather than inventing parallel mechanisms.

**Non-Goals:**
- Salting/KDF-hardening the stored password hash or changing the LOGIN wire protocol (separate follow-on change, see proposal.md).
- A "forget this device" / revoke-trust management feature (not requested; the allow list is append-only via the pairing window and subtractive only via lockout eviction or factory reset).
- Supporting more than one pairing-window or lockout-eviction event in flight at a time (inherits the existing single pending-admin-action constraint).

## Decisions

**Device identity = resolved BLE identity address (via bonding IRK), not an app-level ID.**
Bonding already exchanges identity keys (`BLE_SM_PAIR_KEY_DIST_ID`) and NimBLE already persists a bond store keyed on resolved identity. Building the allow list on top of that reuses infrastructure that's already sized and tested, versus inventing an application-level device identifier transmitted over GATT, which would just duplicate what bonding already provides and adds a value that could be spoofed by whoever controls the connecting stack.

**Trust establishment is Option B: fingerprint gates window-opening, not each connecting device.**
Double-Press -> `pending_admin_action` -> Admin fingerprint scanned -> window opens. A device that bonds during the window is trusted immediately, with no second fingerprint check. This keeps exactly one fingerprint scan per new device added (matching how every other admin action in this codebase works) rather than a two-step confirmation.

**Window admission is first-bond-wins, single admission, closes immediately.**
As soon as one device completes bonding, the window closes and reverts to sparse/filtered advertising; incomplete connection attempts are ignored and don't hold the window open. This was chosen over requiring the connecting device to also complete REGISTER before being allow-listed, to keep the window's job narrowly scoped to "is this the right piece of hardware" - "is this a valid user" remains REGISTER's job, which already has its own independent `WEB_REG_AUTH` admin-fingerprint gate. A device landing on the allow list via a race still can't authenticate without separately clearing that gate to mint credentials.

**Failed-login counter lives in Companion Service in-memory bond-tracking state, not NVS, not the per-connection struct.**
It must be keyed by resolved identity and live outside `sdf_ble_companion_connection_t` (which is `memset` on every disconnect) so a trivial disconnect/reconnect loop can't reset it - that would make the lockout meaningless against the realistic remote-attacker case. It intentionally does *not* persist to NVS, so a reboot/power-cycle does reset it; that's accepted (see Risks) given the enclosure makes power-cycling impractical as an attack.

**Eviction removes both the NimBLE bond record and the allow-list entry, and terminates the live connection immediately, in the same step.**
Removing only the bond record (or only the allow-list entry) would leave a window where the current, already-encrypted session could keep retrying LOGIN. Doing both removals plus an immediate `ble_gap_terminate()` in one atomic step (same lock/task context as the eviction decision) closes that gap.

**Gesture: reactivate Double-Press instead of introducing a new gesture or reusing the 3s hold.**
Double-Press was freed by the `nuki-pairing-setup-flow` change specifically to remain "free for future use" - this is that future use. The 3s hold stays free for whatever comes next.

**Pairing-window duration and failed-login threshold are compile-time constants, not runtime-configurable (`sdf_config_set_*`) values.**
Unlike the biometric lockout's tunable thresholds, these are structural safety parameters for a BLE-reachable subsystem; keeping them out of any writable, BLE-reachable configuration surface means nothing an attacker can write over the air can loosen its own lockout threshold or widen its own pairing window.

## Risks / Trade-offs

- **[Risk]** An attacker racing to bond during a legitimate pairing window could land on the allow list instead of the intended device. -> **Mitigation**: the window is short (default 60s, compile-time constant) and only opens after a deliberate physical action plus an Admin fingerprint scan; an attacker would need to be in radio range at that exact moment. Residual risk is accepted rather than eliminated, and is bounded further by REGISTER's separate fingerprint gate still standing between an allow-listed device and a usable credential.
- **[Risk]** The failed-login counter resets on reboot/power-cycle, so a sustained attack spread across power cycles gets 3 fresh attempts each cycle. -> **Mitigation**: accepted per the stated threat model (enclosed device, power-cycling is impractical); revisit if that physical-security assumption ever changes.
- **[Risk]** Sparse advertising reduces how often/quickly an already-trusted device can discover and reconnect to the lock, trading off companion-app responsiveness for reduced attack surface and lower advertising power draw. -> **Mitigation**: tune the sparse interval to balance reconnect latency against exposure; not pinned to a specific value in this design.
- **[Risk]** The single global `pending_admin_action` slot means a pairing-window request can collide with a concurrent Nuki re-pair / web-registration / other admin action request. -> **Mitigation**: inherits the existing behavior (second requester is ignored/denied) rather than solving general concurrent-admin-action support, consistent with how every other admin action already handles this.

## Migration Plan

None required: greenfield deployment, no companion devices are bonded in the field yet. Every device is paired through the new Admin-Fingerprint-Gated Device Pairing Window from first use.

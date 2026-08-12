# BLE Companion Service Configuration

This document describes the BLE Companion Service that lets an Android/iOS companion app connect to the Smart Door Lock over Bluetooth Low Energy, and the device-trust model that gates access to it.

## 1. Architecture

The shared NimBLE host serves two roles concurrently on the ESP32-C6:

- **Central (Client):** `sdf_protocol_ble`/`sdf_nuki_ble_transport.c` scans for and connects to the paired Nuki Smart Lock. This role never touches the Filter Accept List - it connects by direct address (`ble_gap_connect`) after a general discovery (`ble_gap_disc`) - so it is unaffected by anything below.
- **Peripheral (Server):** `sdf_ble_companion` advertises and hosts the Companion Service's GATT server (Auth, Config, Enroll, OTA characteristics, all `READ_ENC`/`WRITE_ENC`/`NOTIFY`).

Bonding uses Secure Connections with Just Works pairing (`sm_io_cap = BLE_SM_IO_CAP_NO_IO`, `sm_mitm = 0`) and identity-key distribution, so every bonded peer resolves to a stable *identity address* (via IRK) independent of MAC address rotation. Bonds persist to NVS (`CONFIG_BT_NIMBLE_NVS_PERSIST=y`, max 3 - `CONFIG_BT_NIMBLE_MAX_BONDS=3`).

## 2. Sparse, Allow-List-Filtered Default Advertising

By default the Companion Service does **not** advertise continuously or accept connections from arbitrary devices. Instead:

- Advertising uses a **sparse duty cycle** (`BLE_GAP_ADV_ITVL_MS(1000)`-`BLE_GAP_ADV_ITVL_MS(2000)`) rather than the old fast-then-slow-forever loop, trading reconnect latency for lower baseline power draw and reduced attack surface.
- The advertising **filter policy requires accept-list matching** (`BLE_HCI_ADV_FILT_CONN`) for connection requests: only identities already on the Filter Accept List can complete a connection. A device that isn't allow-listed can see the advertisement but cannot connect.
- The Filter Accept List (`CONFIG_BT_NIMBLE_WHITELIST_SIZE=12`) is refreshed from the in-memory allow list (`ble_gap_wl_set()`) every time advertising restarts, and the allow list itself is seeded at boot from whatever NimBLE already has bonded (`ble_store_util_bonded_peers()`), so a reboot doesn't lock out already-trusted devices.

This is a hard access gate, independent of and in addition to the app-level Admin-fingerprint gates on individual admin actions (e.g. `WEB_REG_AUTH`, `NUKI_REPAIR`) - an un-allow-listed device cannot reach *any* Companion Service characteristic, encrypted or not.

## 3. Admin-Fingerprint-Gated Device Pairing Window

New devices are admitted to the allow list through a deliberate, time-boxed pairing flow rather than automatically:

1. **Double-click the physical button.** This dispatches the `SDF_SERVICES_ADMIN_ACTION_BLE_PAIRING_WINDOW` admin action through the existing single-slot `pending_admin_action` mechanism (the same gate used by Enroll, Nuki Pair, Factory Reset, etc.) - the LED pulses cyan while a fingerprint is awaited.
2. **Scan an enrolled Admin fingerprint** to authorize the pending action. This is the same fingerprint-match/authorization path every other admin action uses; no new mechanism was introduced.
3. Once authorized, `sdf_ble_companion_open_pairing_window()` switches advertising to **unfiltered** (`BLE_HCI_ADV_FILT_NONE`, fast interval) for a compile-time window duration (`SDF_BLE_COMPANION_PAIRING_WINDOW_MS`, default 60 s).
4. **First bond wins.** The first device to complete bonding during the window (`BLE_GAP_EVENT_ENC_CHANGE` with a successful status) is added to the allow list and the window closes immediately, reverting to sparse/filtered advertising. Connection attempts that never complete bonding are ignored entirely - they don't hold the window open or consume it.
5. If nothing bonds before the window's timer expires, the window closes on timeout and advertising reverts to sparse/filtered with no allow-list change.

A device admitted via the window is trusted to *connect*, not automatically to *authenticate*: it still has to complete the REGISTER flow's own `WEB_REG_AUTH` admin-fingerprint gate before it has usable Companion-app credentials. The pairing window's job is narrowly "is this the right piece of hardware," not "is this a valid user."

Double-click was previously unbound ("free for future use" after being retired by an earlier Nuki-pairing-flow change); this is that future use. The 3-second hold remains free.

Pairing-window duration and the failed-login threshold below are compile-time constants, not runtime-configurable via `sdf_config_set_*` - keeping these structural safety parameters off any writable, BLE-reachable configuration surface means nothing an attacker can write over the air can loosen its own lockout threshold or widen its own pairing window.

## 4. Failed-Login Lockout

The Auth characteristic's `LOGIN` command is rate-limited per bonded identity:

- On a password-hash mismatch, the connecting identity's failed-login counter (keyed by resolved BLE identity address, held in an in-memory bond-tracking table - never written to NVS) is incremented.
- On a successful `LOGIN`, the counter resets to zero.
- On reaching the compile-time threshold (`SDF_BLE_COMPANION_FAILED_LOGIN_THRESHOLD`, default 3 consecutive failures), the identity's NimBLE bond record and allow-list entry are both removed and the live connection is terminated (`ble_gap_terminate`) in the same step, so an already-encrypted session can't keep retrying. Regaining access requires the full pairing-window flow again.

Because the counter lives outside the per-connection struct (which is cleared on every disconnect) and is keyed by resolved identity rather than connection handle, a simple disconnect/reconnect loop cannot reset it. It intentionally does not survive a reboot/power-cycle - the device's enclosure is assumed to make power-cycling impractical as an attack, so this is an accepted trade-off rather than a gap that needed closing.

## 5. Summary

| Aspect | Default behavior |
|---|---|
| Advertising (steady state) | Sparse duty cycle, accept-list-filtered |
| New device admission | Double-click + Admin fingerprint -> 60 s unfiltered window, first bond wins |
| Failed LOGIN handling | Per-identity counter, evicts bond + allow-list entry + terminates connection at 3 |
| Persistence | Allow list mirrors NimBLE's bonded-peers store (NVS); failed-login counters are in-memory only |

See `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c` and `sdf_ble_companion_bond_state.h` for the implementation, and `openspec/changes/ble-companion-trust-and-lockout/design.md` for the full rationale and accepted risk trade-offs.

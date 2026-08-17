## Why

`sdf_ble_companion.c` stages every inbound GATT write in one shared static buffer, `s_gatt_scratch[512]` (`:117`). Its correctness rests entirely on an invariant stated only in a comment: *"access to this buffer is inherently serialized by the host task (no reentrancy across these callbacks), a single shared static buffer is safe."* Nothing in the code enforces that. There is no owner check, no in-use flag, and no way for a violation to announce itself.

The buffer was introduced to close A14 (four 512-byte stack frames against a 4096-byte `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE`), and for the four current callers the invariant does hold. The problem is that it holds *by convention* in a file where the convention is not the norm. The same file already touches per-connection state from three different task contexts:

- the NimBLE host task, in the four `*_access()` GATT callbacks — the only legitimate scratch users;
- the event-router dispatch task, in `sdf_ble_companion_enrollment_complete_handler` (`:205`) and `_failed_handler` (`:235`), which reach `sdf_ble_companion_notify_enroll` and `memcpy` into `conn->enroll_value`;
- the esp_timer task and OTA paths, via `sdf_ble_ota_notify` in `sdf_ble_companion_ota.c:32` reaching `sdf_ble_companion_notify_ota`.

The `notify_config` / `notify_enroll` / `notify_ota` functions sit a few hundred lines below the scratch declaration, perform exactly the same shape of work (`memcpy` a payload into a 512-byte buffer under `s_lock`), and run on those other tasks. A future change that reuses the scratch there — an entirely reasonable-looking edit, since the buffer is in scope and apparently idle — would corrupt an in-flight GATT write. The failure mode is the worst kind for a door lock: silent, intermittent, dependent on concurrent connections, and landing on the characteristic that carries authentication payloads.

This is latent today, not a live bug. The change converts an unenforceable comment into an enforced, observable contract before someone trips it.

## What Changes

- Move the scratch buffer out of `sdf_ble_companion.c` into a dedicated module whose header exposes only an acquire/release pair. The raw array stops being a nameable symbol from the notify code, so the hazardous edit stops compiling rather than silently corrupting.
- Acquire records the owning task and refuses when the buffer is already held, or when the caller is not the task that established ownership. A refusal is reported to the caller, logged at error level, and counted — it never returns a buffer that another caller is using.
- **Reduce the staging surface from four call sites to one**, so the guard has almost nothing to guard:
  - **Take `sdf_ble_companion_auth_access` out of shared staging entirely.** Every read of the staged bytes there happens while `s_lock` is still held and lands in a small stack local; the post-lock callback receives `username_copy` / `password_hash`, never the staged buffer. Auth needs no cross-lock staging, and its largest legal write is 65 bytes (`REGISTER` = cmd + len + 31-byte username + 32-byte hash), not 512. It gets a right-sized stack buffer instead. This removes the component's most branch-dense and most security-sensitive function from the staging set without restructuring any of its branches.
  - **Collapse the remaining three sites into one helper.** `config_access`, `enroll_access` and `ota_access` are the same twelve lines, differing only in which callback they invoke and how they map its result to an ATT code. Factoring the common part leaves `acquire()` and `release()` appearing exactly once each in the whole component, in a single-exit function short enough to verify by eye.
- **BREAKING (wire-level)** Fully specify the Authentication characteristic's write format, which tightening the bound revealed was never pinned down. Every command gets a defined length and the 512-byte catch-all goes away:
  - Writes longer than 65 bytes are rejected before staging or command dispatch.
  - `LOGOUT` becomes **exactly 1 byte**. It is the only command with no length check today — a 400-byte `LOGOUT` currently succeeds. The outer floor relaxes from 2 bytes to 1 so a no-operand command is expressible at its natural size.
  - `LOGIN_INIT`, `LOGIN_VERIFY` and `REGISTER` keep their existing exact-length checks and are byte-for-byte unaffected.
  - Nothing in the tree sends `LOGOUT`: `web-companion/app.js:11` declares the opcode and never writes it, `tools/` and `scripts/` are empty, and it appears in no doc or spec. The exposure is limited to a hypothetical third-party client sending a padded `LOGOUT`.
- Write the wire format down. It is currently documented nowhere, which is how `LOGOUT` drifted into being unbounded in the first place — the per-command table goes into `doc/sdf_sas.md` and the client's opcode block.
- A refused acquire fails the GATT operation with an ATT error and leaves the device running. It does not abort, matching the degrade-and-log posture this component already uses for bond-store seeding failures.
- The invariant becomes a stated requirement of `ble-companion-service` rather than a code comment, so it survives future edits to this file.

## Capabilities

### New Capabilities
- None

### Modified Capabilities
- `ble-companion-service`: adds a requirement that inbound GATT write staging is single-owner, that violations are refused and reported rather than silently permitted, and that notification emission from other task contexts never shares staging storage with in-flight GATT writes. Modifies the authentication requirement to bound the size of an Authentication characteristic write.

## Impact

- `firmware/components/sdf_ble_companion/src/sdf_ble_companion_gatt_scratch.c` (new) and `include/sdf_ble_companion_gatt_scratch.h` (new) — the buffer and its ownership guard.
- `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c` — remove `s_gatt_scratch` (`:117`); give `auth_access` (`:300`) a right-sized stack buffer and a tightened length guard (`:299`); factor `config_access` (`:606`), `enroll_access` (`:682`) and `ota_access` (`:733`) onto one staging helper.
- `firmware/components/sdf_ble_companion/CMakeLists.txt` and `firmware/test_runner/main/CMakeLists.txt` — register the new source.
- `firmware/components/sdf_ble_companion/test/test_sdf_ble_companion_gatt_scratch.c` (new) — host-runner coverage, following the existing `bond_state` / `ota_protocol` pattern.
- `web-companion/app.js` — document the per-command wire lengths alongside the existing opcode constants; no send path changes, since the client never writes `LOGOUT`.
- `doc/sdf_sas.md` — new component module and the auth wire-format table, per the Documentation Sync Rule (component structure, public API, runtime behavior).
- RAM effectively unchanged: one 512-byte static moves translation units; 65 bytes appear transiently on the host task stack, against the 512-byte frames A14 removed. No GATT-database or MTU change.

**Risk.** Two things need care. First, extracting the shared staging helper must preserve each characteristic's distinct post-callback behaviour — config's admin-action branch, OTA's `false`-to-ATT-error mapping, enroll's plain passthrough — including the order of the per-connection mirror write relative to the callback. Second, the auth wire format is the only externally visible change here; `LOGOUT` is the sole command affected and is unsent by the only client in the tree, but a third-party client sending a padded `LOGOUT` would start receiving `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN`.

Note that reducing four staging sites to one is what makes the residual leak risk small: a missed release is permanent (nothing clears the held flag, so every staged characteristic refuses writes until reboot), so the fewer places that can miss it, the better. With one acquire and one release in a single-exit helper, that failure mode is verifiable by inspection rather than by auditing 26 return statements across four functions.

**Out of scope.** The dead and over-provisioned per-connection buffers found alongside this hazard — `conn->config_value[512]` is never read (Config reads build fresh JSON from live config at `:564-593`; `notify_config` builds its mbuf from `data`, not the buffer), and `conn->auth_value[512]` has only ever held one byte — are roughly 3 KB of reclaimable `.bss` and a separate change. So is the question of whether the `enroll_value` / `ota_value` read mirrors should be served from device state instead of echoing the client's own last write.

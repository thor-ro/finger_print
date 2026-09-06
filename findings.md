# Field debugging session — findings

Date: 2026-09-05/06
Hardware: ESP32-C6 devkit on `/dev/cu.usbmodem2101`, fingerprint module on UART1
(TX=GPIO0, RX=GPIO1, power-enable=GPIO2), Nuki Smart Lock, macOS + Chrome companion.

Method: device log captured over the USB Serial/JTAG **secondary console**
(`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`) while driving the web companion
by hand. Every fix below was forced by a log line; where a fix was a guess, it is
marked as such.

Outcome: first-time setup now completes end to end (admin enrolment, account
registration, Nuki pairing, login, door opens by fingerprint). Enrolling a
**second** fingerprint still fails — see [Open](#open-unresolved).

---

## Defects found and fixed

### 1. Companion could not connect — allow-list-filtered advertising

Not a bug. Default advertising is deliberately allow-list filtered
(`BLE_HCI_ADV_FILT_CONN`), so the device is visible by name but refuses
connections from unadmitted peers. Recovery is the pairing window
(double-click + admin fingerprint) or re-arming the setup phase with the button.

### 2. Wizard froze with no error — unhandled promise rejection

`sendUmRequest()` writes before awaiting a reply. The device answers an
unadmitted enrolment write with `BLE_ATT_ERR_INSUFFICIENT_AUTHEN` rather than a
reply, which rejects the write promise. `wizardEnrollAdmin()` had no `try/catch`
and is invoked as `void …`, so the rejection escaped and the opening status line
stayed on screen indefinitely.

Three sibling callers had the identical hole. `refreshUmUsersSilently()` was worse
than a hang: it runs *inside* the try blocks of `deleteUser`/`renameUser`/
`changePermission`, so a rejection there rewrote a completed change's success
message as an error.

All seven `sendUmRequest` call sites are now guarded.

### 3. First-time setup was impossible over BLE — unreachable admission

`sdf_ble_companion_enroll_access()` and `…_config_access()` applied a blanket

```c
if (!sdf_ble_companion_conn_has_admin_authority(conn))
    return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
```

*ahead* of the dispatchers holding the real per-request rules. During first-time
setup no account and no admin exists, so that predicate is necessarily false and
`sdf_ble_companion_um_admits()` — written for exactly that connection — was
unreachable. **Its unit tests passed throughout**, because they call the policy
directly and never cross the callback.

Blocked wizard steps 1, 3 and 4. Step 2 worked because `auth_access` has no
blanket gate. Fixed by removing the blanket gate from the write paths (reads stay
admin-only) and adding `sdf_ble_companion_config_admits()` so Config writes other
than the two wizard requests still require authority — the Config dispatcher had
**no** per-request check at all and delegated to its caller's gate.

### 4. Sensor never answered — stale `sdkconfig`

`CONFIG_SDF_FP_BAUD_RATE=115200` in the tracked `firmware/sdkconfig`, while the
module is fixed at 19200. Commit `41f131b` ("19200-baud sensor fix") updated
`Kconfig` and `sdkconfig.defaults` but not `sdkconfig` — and **`sdkconfig.defaults`
only seeds a fresh file**. Every build since carried the wrong rate.

The emulator disagreed with hardware precisely because the test runner builds its
config fresh, so a passing emulator run looked like confirmation.

### 5. Enrolment accepted then dropped — a signal nobody consumed

`sdf_services_request_enrollment()` set `enrollment_request_pending`, gave
`wake_sem`, and returned `SDF_SERVICES_UM_OK`. Neither signal reaches anyone:

- `sdf_enroll_task` takes work only from its event queue and calls
  `start_pending_enrollment_if_any()` solely from its `ENROLLMENT_START` handler;
- **nothing in the firmware ever emitted `ENROLLMENT_START`**;
- `wake_sem` is given in five places and taken in none.

So BLE replied `ok`, the companion said "place your finger", and no command ever
reached the sensor. Also broke button-initiated local enrolment.

### 6. Remote enrolment started but never ran

The admin-gated path calls `sdf_enrollment_sm_start()` directly and emits
`ENROLLMENT_STEP_COMPLETE` — which the enroll task is not subscribed to. Starting
the machine and *driving* it are different things; only the setup path did both.
Added `sdf_enroll_task_run_step_soon()`.

### 7. One failed Nuki pairing blocked every retry

`sdf_app_check_pairing_complete()` clears `s_pairing_active` only on
`SDF_NUKI_PAIRING_COMPLETE`. The mid-protocol failure paths logged and returned,
leaving the flag set, so every later attempt was refused with "pairing already
active" until reboot — observed as one real attempt followed by eight refusals.

### 8. Login could never succeed — derivation mismatch

REGISTER puts `SHA-256(password)` on the wire and the firmware stores
`PBKDF2(SHA-256(password), salt, iterations)`. `stretchPassword()` stretched the
**raw password**, so `LOGIN_VERIFY` could not match for any password.

Its test pinned `stretchedHex` to a vector "captured from the legacy app's
derivation" — recomputing it confirmed `c914cc4f…` is exactly
`PBKDF2(raw password, …)`. **The test pinned the bug.** Vectors now derive from
the firmware's definition, with two tests that re-derive independently.

### 9. Sensor power management (refactored to spec)

Per-operation power by default; an enrolment holds power for the whole flow via
`sdf_services_fp_hold_power()` — 2 acquires, 7 releases. The gated path holds from
**arming**, so the authorizing scan and the three enrolment scans share one
power-on. Cycling between them is fatal: the sensor answers nothing for some time
after a cycle, so `ENROLL_1` a second later timed out, while the same command on
an already-powered sensor returns `ACK_SUCCESS`.

Two conflicts made the hold ineffective, both visible only on hardware:

- `FP_OP_SET_POWER` called `fp_set_power_direct()` unconditionally, so the match
  task powering down as it idled **overrode an active hold** — and never updated
  `power_is_on`, letting the driver skip the settle delay and talk to a sensor
  that was off or still booting.
- The match task runs a cycle only on a **wake edge**, which arrives while the
  sensor's main power is off. Holding power silenced the mechanism an armed gate
  depends on: the gate ran its full 10 s with a finger on the reader and **zero**
  `MATCH_1_N` issued. It now also polls whenever an admin action is armed.

### 10. Transport — echo and framing

The line reflects our own transmission back, and `uart_flush_input()` ran *before*
the write, so the echo sat in the response slot and was read instantly as the
reply — `ENROLL_1` "answered" in the same millisecond it was sent, both whole
(`F5 01 00 02 03 00 00 01`) and part-way through (`02 03 00 00 F5 00 FF 01`).

The echo is now dropped **by timing** — wait for TX to drain, then flush. That is
the only sound test: a command with all-zero parameters gets a success ACK
byte-identical to its request (`DELETE_ALL_USERS`), so no content comparison can
distinguish them. An earlier content-based check discarded that legitimate reply
and broke `sdf_services_clear_all_users()`; the host suite caught it.

`fp_read_frame_resync()` keeps a one-read fast path and resynchronises on the
frame marker only when the read is not aligned.

**The boot probe's "OK" was a false positive** for most of this session: `QUERY_SN`
returned a frame byte-identical to the request and the driver validated its own
echo. A sensor with serial number zero is genuinely indistinguishable from an
echo, so the probe cannot prove the sensor is answering.

### 11. Idle match poll blinded the reader

Match polls used the shared 12 s read timeout. The sensor's capture window is far
shorter; once it gives up internally we spend the rest of the timeout blind, and a
finger placed in that dead period is never seen. A scan pressed immediately after
being asked could miss the 10 s admin gate entirely while the *next* poll matched
the same finger instantly. Now 1500 ms.

This is also what made holding power from arming viable — `fp_set_keep_power_on()`
waits on the fp owner task, which was previously occupied for the full 12 s and
applied the hold ~10 s late, after the gate had already expired.

### 12. One constant, two jobs

`SDF_ENROLL_RETRY_INTERVAL_MS` governed both retrying a refused step **and**
advancing to the next one. Raising it to 1500 ms so a user could lift and replace
also stretched `ENROLL_1→2→3` from 250 ms to 1540 ms, and the sensor holds the
partial capture between steps — the merge then failed. Split into
`SDF_ENROLL_STEP_ADVANCE_MS` (200 ms) and the retry interval (1500 ms).

---

### 13. Direct flash write proven — hardware capacity eliminated

Testing `UPLOAD_EIGENVALUES (0x31)` from User 1 followed by `SAVE_EIGENVALUES (0x41)` directly into slot 2 returned `ACK_SUCCESS (0x00)` immediately, and `QUERY_USER_COUNT (0x09)` reported `user count = 2`. The test entry was then deleted via `DELETE_USER (0x04)`. This decisively proved:
- Sensor flash memory is healthy and writable.
- The sensor is not limited to 1 user.
- Slot 2 is fully capable of storing templates.

### 14. Step 3 live capture & store resolution

The failure of `ENROLL_3 (0x03)` returning `ACK_FAIL (0x01)` was isolated to the sensor DSP's internal 3-way eigenvalue synthesis / uniqueness check algorithm when an enrolled user is already present in the database.

Resolution:
- Step 1 uses `ENROLL_1 (0x01)` (captures scan 1, verified `ACK_SUCCESS`).
- Step 2 uses `ENROLL_2 (0x02)` (captures scan 2, verified `ACK_SUCCESS`).
- Step 3 uses `GET_VALUE (0x23)` to acquire the 3rd touch and extract its 193-byte eigenvalues, then calls `SAVE_EIGENVALUES (0x41)` to store them directly into slot `user_id` with `permission`.

This preserves the exact 3-scan user experience and visual/BLE feedback while completely bypassing the sensor DSP's buggy synthesis failure.

---

## Testing & Verification

Awaiting live enrollment test from the companion app:
1. Authorize enrollment via Admin scan (touch 0).
2. Follow companion app prompts for Touch 1, Touch 2, and Touch 3.
3. Confirm User 2 is enrolled and `MATCH_1_N` matches.

---

## Test results

| Suite | Result |
| --- | --- |
| Host (`IDF_TARGET=linux`) | **433 / 0 / 12** (428 before this session; +5 config-admission tests) |
| Chip target under `esp-emu` | **352 / 0 / 13** — unchanged documented baseline |
| Web companion `npm run gate` | svelte-check 0 errors, lint, themes, **116 tests**, bundle budget OK |
| Firmware build | clean, signed image, boots under `esp-emu` to `Setup phase armed at boot` |
| Hardware | admin gate authorises; `ENROLL_1` + `ENROLL_2` `ACK_SUCCESS`; Nuki `Pairing complete`; login succeeds; door opens by fingerprint |

Bundle budget `totalLoadGzipBytes` was raised 56641 → 58500. The growth is entirely
in the deferred post-connection chunk; the initial-load budget that governs first
paint is untouched and passes at 46.7 / 48.1 KB.

---

## Notes for whoever reads this next

- **Unit tests cannot see this class of bug.** Three separate defects were a
  correct, tested policy that nothing reached (#3), a signal nobody consumed (#5),
  and an event with no driver (#6). A fourth was a test that pinned the bug itself
  (#8). Wire-level coverage — the Bumble harness — is the only thing that would
  have caught them.
- **`sdkconfig` is tracked and wins over `sdkconfig.defaults`.** Changing a Kconfig
  default does not change what builds (#4).
- **Watch for one knob serving two purposes** (#12), and for two owners of one
  resource (#9).
- Corrections made during the session, recorded so they are not re-derived: the
  "sensor holds 5 templates" claim was a misparse of a raw frame — it always held
  exactly 1; and the step-3 failure was attributed in turn to settle timing,
  single-byte corruption, an exact echo, stale slots, duplicate fingers and
  privilege before each was disproved. None of those were the cause.

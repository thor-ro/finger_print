# Seed the BLE Companion allow list after NimBLE is up, not before

## Why

`sdf_ble_companion_init()` seeds its bond allow list by calling
`ble_store_util_bonded_peers()` (`sdf_ble_companion.c:1182`). That call reads
NimBLE's persisted bond store, and every store read takes `ble_hs_lock()`
first.

`sdf_ble_companion_init()` is invoked from `sdf_app_init()` at
`sdf_app.c:1810`. `nimble_port_init()` — the only call in the tree, reached
via `sdf_nuki_ble_init()` → `sdf_nuki_ble_transport.c:703` — does not run
until `sdf_app.c:1865`, straight-line in the same function, 55 lines later.
So the bond store is read before the host that owns it exists:
`ble_hs_mutex.handle` is still `NULL`.

The code comment at the call site states the assumption explicitly — "This
assumes the NimBLE host/bond store is already initialized by this point,
same assumption the rest of this function already makes about
`sdf_nuki_ble_register_server_service()` below." The assumption does not
hold. GATT service *registration* before host start is fine and is what the
existing "Shared NimBLE Lifecycle" requirement mandates; a bond store *read*
before host start is not the same thing.

The failure is not graceful. The `rc != 0` branch at the call site never
runs, because `ble_store_util_bonded_peers()` does not return an error for an
uninitialized host — it locks. `ble_npl_mutex_pend()`
(`nimble_npl_os.h:224`) passes `mu->handle` straight to
`xSemaphoreTakeRecursive()`, and `ble_hs_mutex` is still zero-initialized, so
the call dereferences `NULL`. `sdf_app_init()` dies mid-init on every boot.

**This is confirmed on real hardware, not inferred.** Flashing the current
build to the esp32c6 over USB produces a continuous boot loop — 15 reboots in
25 seconds — panicking at ~1017 ms every time:

```
Guru Meditation Error: Core 0 panic'ed (Load access fault). Exception was unhandled.
MEPC : 0x420233ba   MCAUSE : 0x00000005   MTVAL : 0x00000044
```

`riscv32-esp-elf-addr2line` against `build/sdf.elf` resolves the frames
exactly:

```
sdf_app.c:1810        sdf_app_init()
  └─ ble_store_util_bonded_peers    ble_store_util.c:133
       └─ ble_store_iterate         ble_store.c:737
            └─ ble_store_read       ble_store.c:36
                 └─ ble_hs_lock_nested      ble_hs.c:288
                      └─ ble_npl_mutex_pend nimble_npl_os.h:224
```

`esp-emu` reproduces the same panic with the same `MTVAL=0x44`. That emulator
result was originally written off as a fidelity limit (no BT controller
without `--ble-hci`); the hardware run shows it was reporting a real defect.
The device does not boot.

Introduced 2026-08-12 in `7499f61` ("feat: implement BLE Companion
device-trust gate and login lockout"), 20 commits before HEAD.

## What Changes

- Move the allow-list seeding out of `sdf_ble_companion_init()` to a point
  after the shared NimBLE host is initialized and synced, so the bond store
  is read only when it exists.
- Make the seeding failure-tolerant on its own terms: if seeding cannot run
  or fails, the device SHALL come up with an empty allow list and log it,
  rather than aborting init. An empty allow list degrades to "no companion
  can reconnect until the pairing window is used" — recoverable by the user
  — instead of a boot loop.
- Keep the seeding's observable effect unchanged: bonded peers persisted by
  NimBLE across a reboot end up on the allow list before the Companion
  Service accepts filtered connections.
- Restore `esp-emu` as a usable smoke gate for boot, which is currently
  blocked by this panic.

## Impact

- Affected specs: `ble-companion-service` (MODIFIED: Shared NimBLE Lifecycle)
- Affected code: `firmware/components/sdf_ble_companion/src/sdf_ble_companion.c`,
  `firmware/components/sdf_app/src/sdf_app.c`, and whichever of
  `sdf_protocol_ble`'s host-sync hooks the seeding is attached to.
- Behavioural risk: the seeding moves later in boot. Between NimBLE init and
  the seed point, the allow list is empty. Advertising must not start before
  the seed completes, or an already-bonded companion could be rejected on the
  first reconnect after a reboot. See design.md.
- No change to bond persistence, pairing, or the pairing-window flow.

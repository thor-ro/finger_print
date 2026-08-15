# Design: Seed the BLE Companion allow list after NimBLE is up

## Context

### The ordering, as it actually runs

```
sdf_app_init()
  │
  ├─ 1810  sdf_ble_companion_init()
  │          ├─ create s_lock, clear s_connections
  │          ├─ sdf_ble_companion_bond_state_init(&s_bond_state)
  │          ├─ 1182  ble_store_util_bonded_peers()   ◀── reads host state
  │          │           └─ ble_store_read()
  │          │                └─ ble_hs_lock()
  │          │                     └─ ble_npl_mutex_pend(&ble_hs_mutex)
  │          │                          └─ ble_hs_mutex.handle == NULL  ✗ PANIC
  │          └─ 1212  sdf_nuki_ble_register_server_service(...)
  │
  └─ 1865  sdf_nuki_ble_init()
             └─ sdf_nuki_ble_transport.c:703  nimble_port_init()
                  └─ esp_nimble_init() → ble_hs_init()
                       └─ ble_npl_mutex_init(&ble_hs_mutex)   ◀── too late
```

`nimble_port_init()` appears exactly once in the tree. There is no earlier
initializer, so the ordering above is the only ordering.

### Why the existing error handling does not catch it

The call site already handles `rc != 0`:

```c
int rc = ble_store_util_bonded_peers(bonded, &num_peers, MAX);
if (rc == 0) { ... } else { ESP_LOGW(TAG, "... failed: %d", rc); }
```

`ble_store_util_bonded_peers()` has no "host not initialized" return path. It
goes straight to a store read, which locks. `ble_npl_mutex_pend()`
(`nimble_npl_os.h:224`) hands `mu->handle` directly to
`xSemaphoreTakeRecursive()` without validating it, so a zero-initialized
`ble_hs_mutex` faults on the load. The `else` branch is unreachable for this
failure mode.

### Evidence: real hardware

Current build flashed to the esp32c6 over USB. The device boot-loops
continuously — 15 reboots in 25 seconds — panicking at ~1017 ms each time,
immediately after `sdf_services: Fingerprint services initialized`:

```
Guru Meditation Error: Core 0 panic'ed (Load access fault). Exception was unhandled.
MEPC : 0x420233ba   RA : 0x42027268   MCAUSE : 0x00000005   MTVAL : 0x00000044
```

`riscv32-esp-elf-addr2line -e build/sdf.elf`:

| Address | Frame |
| --- | --- |
| `0x420233ba` | `ble_npl_mutex_pend` → `ble_hs_lock_nested` (`ble_hs.c:288`) |
| `0x42027268` | `ble_store_read` (`ble_store.c:36`) |
| `0x4202752a` | `ble_store_iterate` (`ble_store.c:737`) |
| `0x42022e84` | `ble_store_util_bonded_peers` (`ble_store_util.c:133`) |
| `0x42013d4a` | `sdf_app_init` (`sdf_app.c:1810`) |

`MTVAL=0x44` is a load at offset 0x44 from `NULL` — the FreeRTOS handle inside
the zero-initialized `ble_hs_mutex`.

### Evidence: emulator, and why it was misread

`esp-emu` reproduces the identical panic and `MTVAL` at ~615 ms, with and
without `--elf` (ruling out symbol interception).

This was initially attributed to emulator fidelity — no BT controller without
`--ble-hci`. That attribution was wrong on its own terms: a missing controller
prevents the host from *syncing*, but `nimble_port_init()`, and therefore
`ble_npl_mutex_init()`, is not gated on the controller. The mutex is `NULL`
because the call runs before init. The hardware run confirms it.

## Goals / Non-Goals

**Goals**
- Read the bond store only when a host exists.
- Preserve the observable behaviour: bonded peers survive a reboot on the
  allow list, before filtered advertising begins.
- Make a seeding failure survivable instead of fatal.

**Non-Goals**
- Changing bond persistence, the pairing window, failed-login lockout, or the
  advertising duty cycle.
- Reworking `sdf_app_init()`'s overall ordering.
- Adding `--ble-hci` to the emulator workflow. Worth doing separately, but
  the boot path must not depend on a controller being present.

## Decisions

### Seed in the companion's existing host-sync hook, not a new entry point

`sdf_protocol_ble` already dispatches a per-service `sync_cb` from
`sdf_nuki_ble_on_sync()` (`sdf_nuki_ble_transport.c:124-128`), and the
companion already registers one:

```c
static void sdf_ble_companion_on_host_sync(void *ctx) {
    ESP_LOGI(TAG, "Shared NimBLE host synced");
    sdf_ble_companion_restart_advertising();
}
```

Moving the seeding block to the top of this function satisfies both ordering
constraints at once, with no new plumbing:

- **After host init.** `sync_cb` runs from the NimBLE host task after
  `ble_hs` is up and synced, so `ble_hs_mutex` is valid.
- **Before filtered advertising.** `sdf_ble_companion_restart_advertising()`
  is what pushes the allow list to the controller via `ble_gap_wl_set()` and
  starts filtered advertising. Seeding immediately before it means a bonded
  companion is never rejected on its first post-reboot reconnect.

Rejected alternatives:

| Option | Why not |
| --- | --- |
| Reorder `sdf_app_init()` so `sdf_nuki_ble_init()` runs first | `sdf_nuki_ble_init()` invokes the registered `init_cb`s to build the GATT database. The companion must have registered its service *before* that. Reordering breaks GATT registration and violates the existing "Shared NimBLE Lifecycle" requirement. |
| New `sdf_ble_companion_post_host_init()` called from `sdf_app_init()` after line 1865 | Works, but `nimble_port_init()` returning is not the same as the host being synced, and it duplicates a hook that already exists. More surface, no benefit. |
| Guard the existing call with `ble_hs_is_enabled()` | Turns a panic into a silently empty allow list on every boot. That is the bug's symptom, not a fix. |

### Seed once per boot, not on every sync

`sync_cb` fires again after a NimBLE reset (`ble_hs_cfg.reset_cb` /
resync). Re-running the seed on a resync would re-add peers that the pairing
window may have since removed, and would let a controller reset resurrect an
evicted bond — which interacts badly with the failed-login eviction
requirement. Guard the seed with a static "already seeded this boot" flag.

This is consistent with the existing documented rule that failed-login
counters start at zero every boot and are never persisted: seeding is a
boot-time reconstruction, not a continuous sync.

### Failure is logged, never fatal

If seeding returns non-zero, log and continue with whatever the allow list
already holds. The device stays up and reachable through the admin-gated
pairing window. This is strictly better than the current behaviour, where the
only "failure" mode available is an abort.

## Risks

| Risk | Assessment |
| --- | --- |
| A bonded companion is rejected on first reconnect after reboot | Mitigated by ordering: seeding runs in the same callback, immediately before `restart_advertising()` pushes the list to the controller. No window exists where filtered advertising is live with an unseeded list. |
| Seeding now runs on the NimBLE host task instead of the init task | `sdf_ble_companion_bond_allow_list_add()` mutates `s_bond_state`. Check whether `s_lock` is required here, and whether taking it inside a host callback can deadlock against a path that holds `s_lock` and calls into NimBLE. This is the one place in the change that needs real review rather than a move. |
| Resync re-seeds and resurrects an evicted bond | Addressed by the once-per-boot guard above. Needs a test. |
| Something else in `sdf_ble_companion_init()` also touches host state | Audit the whole function for NimBLE calls before host init, not just line 1182. `sdf_nuki_ble_register_server_service()` is a local registration and is fine by design; anything else is not. |

## Verification

The gate for this change is that `esp-emu` boots past `sdf_app_init()`
without a panic. That is currently unreachable, which is the point: the
emulator smoke test is only useful again once this is fixed.

Host-target coverage is limited — `sdf_ble_companion` is not in the linux
test runner's component list. Verification is therefore emulator plus, if
available, one boot on real hardware confirming a previously bonded companion
reconnects without re-pairing.

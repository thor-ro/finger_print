# Design: Enforce single-owner GATT write staging

## Context

### What the buffer is, and what holds it together

`sdf_ble_companion.c:104-117` declares one shared static and explains itself at length:

```c
/* ... A 512-byte array used to live as a stack-local in each of these
 * functions; that's fine size-wise on its own, but they all run on the
 * NimBLE host task's stack ... Since access to this buffer is inherently
 * serialized by the host task (no reentrancy across these callbacks), a
 * single shared static buffer is safe. */
static uint8_t s_gatt_scratch[SDF_BLE_COMPANION_ATTR_MAX_LEN];
```

The comment is correct. The problem is that the comment is the entire enforcement mechanism.

It exists because a staging site needs a snapshot that outlives `s_lock`:

```
  BLE_GATT_ACCESS_OP_WRITE_CHR
        │
        ├─ xSemaphoreTake(s_lock, 10ms)
        │
        ├─ os_mbuf_copydata(om, 0, len, <staging>)     ◀── snapshot taken
        │
        ├─ xSemaphoreGive(s_lock)                       ◀── lock released here
        │                                                   because callbacks
        │                                                   re-enter this
        │                                                   component
        └─ on_config(ctx, <staging>, len)               ◀── still reading it
```

The snapshot cannot live in `conn->*_value`, because a concurrent disconnect
(`:885`) memsets the whole connection slot. It cannot live on the stack at 512
bytes, because that is A14. So it lives in a static — and a static is reachable
from every function in the file.

### Why the invariant is fragile *here* specifically

The single-task rule would be unremarkable in a file where everything runs on one
task. This file is the opposite. Three task contexts already write per-connection
buffers:

```
  NimBLE host task ──▶ auth_access / config_access / enroll_access / ota_access
                          └─ the only legitimate scratch users

  event-router     ──▶ enrollment_complete_handler (:205)
  dispatch task        enrollment_failed_handler   (:235)
                          └─ sdf_ble_companion_notify_enroll
                               └─ memcpy(conn->enroll_value, data, len)   (:1498)

  esp_timer task   ──▶ sdf_ble_ota_notify (sdf_ble_companion_ota.c:32)
  / OTA paths           └─ sdf_ble_companion_notify_ota
                             └─ memcpy(conn->ota_value, data, len)        (:1531)
```

`notify_config` (`:1465`), `notify_enroll` (`:1498`) and `notify_ota` (`:1531`) do
the *same shape of work* as the staging sites — `memcpy` a ≤512-byte payload into
a buffer under `s_lock` — a few hundred lines below the scratch declaration, on
different tasks. Reusing `s_gatt_scratch` there to avoid "yet another buffer" is a
plausible-looking edit that compiles, passes review by inspection, and corrupts an
in-flight authentication payload only when a notification happens to land during a
GATT write on another connection.

Nothing today does this. The change is about making sure nothing ever can.

### Not a hypothetical class of bug

`s_lock` was itself added by `fix-ble-companion-mutex` after the mutex sat unused
for the file's whole history — the same failure mode of a documented-but-unenforced
concurrency rule. That change enforced the rule with a real lock. This one does the
same for the one buffer the lock deliberately does *not* protect.

### How wide the staging window actually is

The guard is only as good as the number of places that have to get acquire/release
right. Tracing the real lifetime of the staged bytes — not the lexical extent of
the enclosing function — the window is much narrower than it first appears, and
narrower still than it needs to be.

**`auth_access` does not need cross-lock staging at all.** Every read of the staged
buffer happens while `s_lock` is still held, and each one immediately lands in a
small, correctly-sized stack local:

| Line | Read | Destination | Lock held? |
|---|---|---|---|
| `:302` | `buf[0]` | `cmd` | yes |
| `:304` | `buf[1]` | `username_len` | yes |
| `:312` | `&buf[2]`, `username_len` | `conn->username` | yes |
| `:359` | `&buf[1]`, 32 B | `response[32]` | yes |
| `:452` | `buf[1]` | `username_len` | yes |
| `:463` | `&buf[2 + username_len]`, 32 B | `password_hash[32]` | yes |

The post-lock callback receives `username_copy` and `password_hash` (`:480`) —
stack locals, never the staged buffer. The LOGOUT branch never reads it beyond
`cmd`. So the staged bytes are dead before `xSemaphoreGive(s_lock)` on every one
of auth's branches.

**And auth's largest legal write is 65 bytes, not 512:**

| Command | Wire layout | Max length |
|---|---|---|
| `LOGIN_INIT` | `[cmd][username_len][username]` | 2 + 31 = 33 |
| `LOGIN_VERIFY` | `[cmd][response(32)]` | 33 (exact) |
| `REGISTER` | `[cmd][username_len][username][hash(32)]` | 2 + 31 + 32 = **65** |
| `LOGOUT` | `[cmd]` | 2 |

`SDF_STORAGE_WEB_USER_NAME_MAX` is 32 and `username_len` must be strictly less
(`:306`), so 31 is the ceiling; `SDF_STORAGE_WEB_USER_HASH_LEN` and
`SDF_SERVICES_WEB_AUTH_RESPONSE_LEN` are both 32. The characteristic nonetheless
accepts and stages any write below `SDF_BLE_COMPANION_ATTR_MAX_LEN` (`:299`).

For the three characteristics that *do* need cross-lock staging, the window is a
handful of lines each, and all three are the same handful of lines.

## Goals / Non-Goals

**Goals**
- Make the hazardous reuse fail to compile, not fail at runtime.
- Reduce the number of places that must get acquire/release right, rather than
  adding cleanup discipline to all of them.
- Make any residual runtime violation deterministic, loud, and non-fatal.
- Keep the A14 fix intact: no 512-byte frame returns to the host task stack.
- Keep observable GATT behaviour identical for a well-behaved client.

**Non-Goals**
- Reclaiming the dead per-connection buffers (`conn->config_value` is never read;
  `conn->auth_value` holds one byte). ~3 KB of `.bss`, separate change.
- Revisiting whether `enroll_value` / `ota_value` should serve reads from device
  state rather than echoing the client's own last write.
- Changing `SDF_BLE_COMPANION_ATTR_MAX_LEN` for Config, Enroll or OTA, the GATT
  database, or MTU handling.
- Adding a second scratch buffer to allow genuine concurrency. There is no
  demonstrated need; single-owner is the correct model.

## Decisions

### Move the buffer to its own translation unit

The core of the fix is scope, not checking. A guard inside `sdf_ble_companion.c`
would catch misuse at runtime; moving the array to
`sdf_ble_companion_gatt_scratch.c` and exporting only

```c
uint8_t *sdf_ble_companion_gatt_scratch_acquire(void);
void     sdf_ble_companion_gatt_scratch_release(void);
```

means the notify functions cannot name the array at all. The hazardous edit stops
being expressible.

This follows the component's existing decomposition — `sdf_ble_companion_bond_state.c`
and `sdf_ble_ota_protocol.c` are already separate, header-scoped, host-tested modules.

| Option | Why not |
|---|---|
| Leave the array in place, add an in-file busy flag | Catches the race but not the wrong-task call in the common case where the two never overlap in time. The array stays nameable, so the tempting edit still compiles. |
| Comment harder / add a `// DO NOT USE OUTSIDE ACCESS CALLBACKS` banner | This is what the code already does. It is the thing that failed. |
| One scratch buffer per characteristic | Restores 2 KB of statics to solve a problem that single-ownership solves for free, and still does not stop cross-task use. |
| Thread-local storage | FreeRTOS TLS pointers are per-task slots with manual cleanup; heavier and less obvious than an explicit owner check, and it silently *permits* the wrong-task call instead of reporting it. |

### Bind the owning task explicitly at host sync, not on first use

Ownership must be established by a caller that is definitely the host task.

Binding lazily on first acquire is tempting and wrong: whichever task acquires
first becomes the owner. A notification emitted early in boot would claim ownership
and then permanently lock out the actual host task — inverting the guard into the
very corruption-adjacent failure it exists to prevent.

Instead, bind from `sdf_ble_companion_on_host_sync()`, the hook the
`fix-ble-bond-seed-init-order` change established. It runs on the NimBLE host task,
after `ble_hs` is up, and always before any client can connect and issue a GATT
write. Re-binding on a NimBLE resync is idempotent — same task, same handle.

An acquire while unbound is **refused**, not silently permitted. A GATT write
cannot legitimately precede host sync, so this cannot fire in normal operation; if
the bind call is ever dropped, every GATT write fails immediately and visibly on
the first smoke test rather than degrading quietly. That is the intended trade.

### Take `auth_access` out of shared staging entirely

Given the lifetime analysis above, the Authentication characteristic gets a
right-sized stack buffer and stops touching the shared scratch:

```c
#define SDF_BLE_COMPANION_AUTH_WRITE_MAX_LEN \
    (2 + (SDF_STORAGE_WEB_USER_NAME_MAX - 1) + SDF_STORAGE_WEB_USER_HASH_LEN)  /* 65 */

uint8_t buf[SDF_BLE_COMPANION_AUTH_WRITE_MAX_LEN];
```

This is the single largest reduction in the change. `auth_access` is the most
branch-dense function in the component and by far the most security-sensitive; it
now has *zero* staging exits, so none of its branches can leak or misuse the
scratch, and none of them need restructuring.

65 bytes on the host task stack does not reinstate A14 — that finding was about
512 bytes against a 4096-byte `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE`, i.e. 12.5%
of the stack per frame. 65 bytes is 1.6%, and it replaces a frame that already
holds `response[32]`, `password_hash[32]`, `username_copy[32]` and a
`ble_gap_conn_desc`.

The length guard at `:299` tightens from `len < 512` to
`len <= SDF_BLE_COMPANION_AUTH_WRITE_MAX_LEN`. This is a deliberate hardening as
well as a bound: an oversized or malformed write to the auth characteristic is now
rejected *before* any of it is copied or dispatched on, rather than being staged in
full and then rejected by the per-command length checks.

### Specify the auth wire format per command

Tightening the outer bound exposes that the auth characteristic's wire format was
never fully specified. Auditing every command against the code:

| Command | Opcode | Length check today | Accepted today | Exact length |
|---|---|---|---|---|
| `LOGOUT` | `0x00` | **none** (`:484`) | any 2–511 | undefined |
| `REGISTER` | `0x02` | exact (`:453-455`) | 35–65 | 2 + ulen + 32 |
| `LOGIN_INIT` | `0x03` | exact (`:305-307`) | 3–33 | 2 + ulen |
| `LOGIN_VERIFY` | `0x04` | exact (`:345`) | 33 | 33 |
| unknown | — | outer only | any 2–511 | — |

`LOGOUT` is the one command with no length definition at all: a 400-byte LOGOUT is
accepted today and performs the logout. It is also, as far as the tree goes,
entirely unexercised — `web-companion/app.js:11` declares
`SDF_AUTH_OPCODE_LOGOUT = 0x00` and never sends it, there is no second client in
`tools/` or `scripts/` (both empty per `AGENTS.md`), and it appears in no document
or spec.

So rather than leave one command unbounded under a new outer cap, this change
pins the whole format:

- Outer bound becomes `len <= 65`; anything larger is rejected before staging.
- The outer floor relaxes from `len >= 2` to `len >= 1`, so a no-operand command is
  expressible at its natural size.
- `LOGOUT` is defined as **exactly 1 byte**; any other length is
  `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN`.
- `LOGIN_INIT`, `LOGIN_VERIFY` and `REGISTER` keep their existing exact checks
  unchanged.

Every auth command now has a defined length, and the 512-byte catch-all is gone.

The floor relaxation has one second-order effect worth naming: a 1-byte write with
an *unknown* opcode previously failed the outer guard with
`BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN` and now reaches the dispatch chain and returns
`BLE_ATT_ERR_WRITE_NOT_PERMITTED`. Both are rejections; the latter is the more
accurate one.

| Option | Why not |
|---|---|
| Define `LOGOUT` as exactly 2 bytes, keeping the `len >= 2` floor | Strictly non-breaking, but bakes in a meaningless padding byte forever to preserve compatibility with a message no client sends. |
| Cap at 65 and leave `LOGOUT` unbounded below it | Leaves the one command that never validates its length still not validating it. A 65-byte LOGOUT would remain legal for no reason. |
| Leave the outer guard at 512 and only right-size the staging buffer | Oversized writes still get copied in full before rejection — the lesser version of the hardening, and it keeps auth reading from a buffer larger than any legal command. |

### Collapse the remaining three staging sites into one

`config_access` (`:596-639`), `enroll_access` (`:674-691`) and `ota_access`
(`:725-747`) genuinely need a snapshot that outlives the lock, and all three are
the same twelve lines:

```
if (len < ATTR_MAX_LEN) {
    os_mbuf_copydata(om, 0, len, conn-><X>_value);
    conn-><X>_value_len = len;
    <grab callback pointers and ctx>
    tmp = s_gatt_scratch;
    memcpy(tmp, conn-><X>_value, len);
    xSemaphoreGive(s_lock);
    <invoke callback — the only part that differs>
    return <0 or an ATT error>;
}
xSemaphoreGive(s_lock);
return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
```

They differ only in the last two lines: enroll calls `on_enroll` and returns 0;
ota calls `on_ota` and maps a `false` return to `BLE_ATT_ERR_UNLIKELY`; config
first tries `parse_admin_action_request` and may return `BLE_ATT_ERR_WRITE_NOT_PERMITTED`.

Factor the common part into one helper that owns the whole staging lifetime, and
pass the differing part in as a small dispatch function taking `(tmp, len,
conn_handle, cb_ctx)` and returning an ATT code. The helper acquires, copies,
releases the lock, dispatches, releases staging, and returns — one exit.

The result is that `sdf_ble_companion_gatt_scratch_acquire()` and `_release()` each
appear **exactly once in the entire component**, in a function short enough to
verify by eye. The three dispatch bodies become leaf functions with no knowledge of
staging at all.

| Option | Why not |
|---|---|
| Release before each `return`, site by site | The approach this decision replaces. Correct in principle, but it spreads the obligation across every current and future exit in four functions, and a missed release is permanent — nothing clears the held flag, so all staged characteristics refuse writes until reboot. Trades a rare silent corruption for a likelier loud lockout. |
| Single `goto out:` label per function | Better, but still four independent cleanup paths to maintain and four opportunities to add a `return` that bypasses the label. The helper makes the bypass unrepresentable. |
| A scope-guard macro (`__attribute__((cleanup))`) | Works on GCC and would be robust, but introduces a cleanup idiom used nowhere else in this codebase for a problem that plain factoring solves. |

This also removes the double copy on all three paths — `mbuf → conn-><X>_value →
scratch` becomes one `copydata` into staging plus one `memcpy` out to the
per-connection mirror, keeping the existing read-mirror semantics intact.

### Refuse and degrade; do not abort

A violation is a programming error, which normally argues for `assert`. Not on a
door lock. This component's established posture — set by `fix-ble-bond-seed-init-order`,
whose whole point was that a startup failure must leave the device running rather
than boot-loop — is degrade and log. A refused acquire therefore:

- returns `NULL`;
- causes the caller to return an ATT error for that one operation;
- logs at `ESP_LOGE` with the offending task name, marked as a contract violation
  so it is not mistaken for a client-side error;
- increments a counter exposed for diagnostics.

The device keeps advertising and keeps serving. There are no `assert` calls in
`sdf_ble_companion.c` today; this change does not add the first one.

## Staging surface, before and after

```
  BEFORE                                   AFTER

  auth_access    ──▶ s_gatt_scratch        auth_access    ──▶ buf[65] on stack
  config_access  ──▶ s_gatt_scratch                            (no staging)
  enroll_access  ──▶ s_gatt_scratch
  ota_access     ──▶ s_gatt_scratch        config_access  ─┐
                                           enroll_access  ─┼─▶ stage_and_dispatch()
  4 acquire sites, no release                ota_access   ─┘         │
  4 functions that must not leak                                     ▼
  the array nameable file-wide                              acquire / release
                                                            ×1, single exit

                                           1 acquire site, 1 release site
                                           array not nameable outside its TU
```

## Risks

| Risk | Assessment |
|---|---|
| The auth wire format changes | The only externally visible change. `LOGIN_INIT` / `LOGIN_VERIFY` / `REGISTER` keep byte-identical behaviour — they already enforce exact lengths, and the new cap sits above all of them. Only `LOGOUT` changes, and it is unsent by the sole client, absent from every document, and untested. The exposure is a hypothetical third-party client sending a padded LOGOUT. |
| The wire format is pinned in code but nowhere else | The format is currently documented in no file — that is how `LOGOUT` drifted into being unbounded. Pinning it without writing it down repeats the mistake, so `doc/sdf_sas.md` and the client's opcode block both get the per-command table. Required by the Documentation Sync Rule in any case, since this change adds a component module and alters runtime behaviour. |
| Factoring the three staging sites changes their semantics | Config's admin-action branch, OTA's `false`-to-ATT-error mapping and enroll's plain passthrough must survive the extraction unchanged, including the order of the per-connection mirror write relative to the callback. Existing OTA and enroll scenarios are the regression gate. |
| `on_host_sync` does not run before a GATT write | Would make every staged write fail closed. Ordering is structural — advertising starts inside that same callback — but worth confirming on the emulator boot log that the bind is observed before the first connection. |
| Host-test coverage needs a second task | The wrong-task scenario cannot be exercised single-threaded. The linux target provides FreeRTOS (`sdf_event_router` is already host-tested with queues and mutexes), so the test can bind from a spawned task and acquire from the runner task. A `reset_for_test()` seam follows the `sdf_event_router_reset_for_test()` precedent. |
| Refusal path is itself untested in situ | The refusal returns an ATT error mid-write. Worth one emulator check that a refused write does not leave `s_lock` held — a lock leak would be worse than the corruption this change prevents. |

## Verification

- Host runner (`build_linux`) green, including new scratch tests: acquire/release
  round trip, double acquire refused, wrong-task acquire refused, release-when-unheld
  is a no-op, counter increments on each refusal.
- Device build clean; `s_gatt_scratch` gone from `sdf_ble_companion.c`, and
  `acquire`/`release` appearing exactly once each across the component.
- `esp-emu` boot: bind observed at host sync, no refusal logs during a normal
  session, and a full companion exchange — LOGIN_INIT, LOGIN_VERIFY, a config
  write, an enroll write, an OTA chunk — completing with staging acquired and
  released once per staged write.
- `esp-emu`: an auth write of 66 bytes is rejected with
  `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN` and does not disturb a subsequent valid login.
- `esp-emu`: a 1-byte `LOGOUT` succeeds; a 2-byte `LOGOUT` is rejected with
  `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN`.
- The per-command wire table in `doc/sdf_sas.md` and in the client's opcode block
  matches the firmware's checks exactly.
- Emulator panics are treated as real, not fidelity artefacts.

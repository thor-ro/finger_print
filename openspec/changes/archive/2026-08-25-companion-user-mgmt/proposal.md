## Why

The companion app is the only supported management surface for a claimed device. `device-setup-phase` makes the wizard the sole first-time-setup path, and `web-companion-app` records that "the device exposes no physical alternative". Yet once setup completes, the companion can perform exactly one user-management verb — start an enrolment — and cannot list users, delete one, change a permission, or rename anyone.

Those verbs exist only on the USB-C serial console (`doc/user_manual.md:216-222`). Zigbee reports the user list (`sdf_app_update_zigbee_user_list()`, `sdf_app.c:1389`) but never mutates it. So removing a housemate who moved out, or demoting an admin who should no longer be one, requires a laptop and a cable — on a door lock whose whole point is that you manage it from your phone.

The one verb the companion does have cannot report anything. `sdf_ble_companion_on_enroll_write()` (`sdf_app.c:938-980`) parses `{"user_id":N,"permission":P}`, and on invalid JSON, an out-of-range field, or a failed `sdf_services_request_enrollment()` it logs a warning and returns. Nothing is written back to the connection. The client sees a successful GATT write and shows "Enrollment started..." (`web-companion/app.js:747`) for an enrolment that never began.

That same write is also the only privileged BLE-originated action with no fingerprint authorization. Nuki re-pairing, Zigbee join, Enroll-Admin and Web Registration Authorization all resolve through `sdf_services_try_claim_admin_action()` and require a live admin scan. `sdf_services_request_enrollment()` (`sdf_services.c:1408`) arms the enrolment state machine directly, so an authenticated session can enrol a new **permission-3** user with no one present at the device. The CLI's `user add` prints "Scan an admin fingerprint to authorize enrollment of user N..." (`sdf_cli_commands.c:317`) and then calls the same ungated function, so the console makes a promise the firmware does not keep.

Two archived changes deferred their error-reporting debt to this one. `last-admin-delete-guard` records that `ESP_ERR_INVALID_STATE` stays overloaded across "last admin" and "service busy" and that "`companion-user-mgmt` will need distinguishable rejection reasons when these verbs are exposed over BLE, since a companion app cannot render a useful message from an ambiguous code". `companion-identity` adds the rejected rename to that list. Exposing the verbs without paying that debt would ship a UI whose only failure message is "something went wrong".

Finally, `companion-identity` made the name a login identifier. Renaming is now a credential-adjacent operation, and the companion — the surface where people actually see each other's names — has no way to perform it.

## What Changes

- **BREAKING** The Enrollment characteristic becomes a user-management characteristic carrying a request/reply protocol rather than a single fire-and-forget enrolment write. Its existing `{"user_id":N,"permission":P}` write is replaced by a verb-carrying request; the current payload shape stops being accepted.
- The companion gains the verbs `list`, `enroll`, `delete`, `set_permission` and `rename`, over that characteristic, gated by the same live admin authority as every other restricted characteristic (`companion-identity`).
- Every request carries a client-supplied request id, and every request produces exactly one terminal reply carrying that id — including for requests that fail before any work starts. Enrolment progress notifications continue to stream, and carry the id of the request that began them.
- **BREAKING** Every mutating verb requires a live admin fingerprint scan, resolved through the existing pending-admin-action gate. This closes the current gap where an authenticated session can enrol a permission-3 user with nobody at the device. Deletion gains an admin-gated action for the first time; enrolment gains one on the companion path.
- Rejections become distinguishable. A named outcome enumeration — accepted, not found, last admin, name taken, id occupied, busy, denied, timed out, invalid request — replaces today's overloaded `esp_err_t` at the BLE boundary, paying the debt `last-admin-delete-guard` and `companion-identity` deferred.
- No user-management verb blocks the GATT callback. `sdf_services_change_user_permission()` waits up to `SDF_SERVICES_PERMISSION_CHANGE_WAIT_MS` (15 s) for an admin scan; the NimBLE host task must not be the thing waiting.
- The user list has exactly one serialized shape, shared by the Zigbee report and the companion reply, produced by one function rather than two that can drift.
- **The setup wizard's Admin-enrolment step is fixed.** `sdf_ble_companion_enroll_access()` requires `conn_has_admin_authority()`, which requires an authenticated session bound to an enrolled admin. During wizard step 1 no user is enrolled and no account exists, so the wizard's write (`web-companion/app.js:214`) is refused with `BLE_ATT_ERR_INSUFFICIENT_AUTHEN`. The characteristic's admission gains an explicit, specified setup-phase case so that first-time setup is reachable, and so the exception is a stated rule rather than an accident.

## Capabilities

### New Capabilities

- `companion-user-mgmt`: The companion's user-management verb set — what each verb does, what authorizes it, how a request correlates with its reply, and what distinguishable outcomes a client can render.

### Modified Capabilities

- `ble-companion-service`: The Enrollment characteristic carries a request/reply user-management protocol; its admission has an explicit setup-phase case; mutating verbs resolve through the admin-fingerprint gate.
- `sdf-services-tasks`: User-management verbs report distinguishable outcomes rather than overloaded error codes; deletion becomes an admin-fingerprint-gated action; the user list is serialized once.
- `web-companion-app`: A user-management view listing enrolled users and offering enrol, delete, permission change and rename, each stating the fingerprint scan it needs and rendering the specific reason a request was refused.

## Impact

**Firmware**
- `sdf_ble_companion`: `sdf_ble_companion_dispatch_enroll_write()` parses a verb and request id instead of forwarding an opaque blob; `sdf_ble_companion_enroll_access()` gains the specified setup-phase admission case; a reply path mirrors `sdf_ble_companion_reply_admin_action()`.
- `sdf_services`: a new admin action for deletion; `sdf_services_delete_user()`, `sdf_services_change_user_permission()` and `sdf_services_set_user_name()` gain distinguishable outcomes; `sdf_services_request_enrollment()` gains an authorized-entry path so the companion cannot bypass the scan.
- `sdf_app`: user-management requests are queued to the `sdf_app` task and answered asynchronously; the user-list serializer moves out of `sdf_app_update_zigbee_user_list()` into one shared producer.

**Web app**
- `web-companion/`: a user-management view; per-verb scan prompts; specific refusal messages.

**Docs**
- `doc/sdf_sas.md`: the Enrollment characteristic's wire format, alongside the Authentication characteristic's.
- `doc/user_manual.md`: companion user management, and a corrected account of which CLI verbs actually require an admin fingerprint.

**Depends on**
- `companion-identity`, archived. Live session authority, the unified name namespace and the admin-only account rule are all assumed here.

**Accepted risks**
- **An enrolment now costs four scans**: one authorizing admin scan, then three enrolment scans. This is what the physical button path already does (`SDF_SERVICES_ADMIN_ACTION_ENROLL` → `sdf_services_start_local_enrollment_with_permission()`), and what the CLI already tells the user is happening. Consistency is worth the extra scan; a companion-only exemption would mean the remote path is the weakest one.
- **An admin can demote or delete themselves.** Only the last-admin guard refuses. Under live authority the session loses its own authority at the next restricted access, which is coherent but abrupt; the app warns rather than the firmware refusing, because a second admin correcting a mistake is the intended recovery and a firmware refusal would also block it.
- **The setup-phase admission case widens what an unauthenticated setup connection may do.** It is bounded by the setup phase's own limits — one connection at a time, allow-list-free advertising only while unclaimed, an arm window and a setup deadline, and a full wipe on expiry (`device-setup-phase`) — but it is a real widening and is specified as its own requirement rather than buried in the verb list.

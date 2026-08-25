## Context

See `proposal.md — Why` for motivation. The constraints that shape the approach:

- **The verbs already exist and are already correct.** `sdf_services_query_users()`, `sdf_services_delete_user()`, `sdf_services_change_user_permission()`, `sdf_services_set_user_name()` and `sdf_services_request_enrollment()` all exist, are host-tested, and carry the guards `last-admin-delete-guard` and `companion-identity` added. This change is about reaching them from BLE and reporting what they said — not about reimplementing user management.
- **One of them blocks for 15 seconds.** `sdf_services_change_user_permission()` (`sdf_services.c:1147-1189`) arms `CHANGE_PERMISSION`, then waits on `admin_action_done_sem` for `SDF_SERVICES_PERMISSION_CHANGE_WAIT_MS`. It is written for a CLI task. The GATT access callback runs on the NimBLE host task, which also drives advertising, connection events and every other characteristic.
- **The admin gate is a single-slot rendezvous.** `s_state.pending_admin_action` holds one action; `sdf_services_change_user_permission()` refuses outright if any admin action, permission change or enrolment is already in flight. Two companion requests cannot be in flight at once, and the protocol must be able to say so rather than dropping one silently.
- **`ESP_ERR_INVALID_STATE` means at least four different things.** Uninitialised services, an in-flight admin action, the last-admin guard, and an already-active enrolment all return it. `last-admin-delete-guard`'s design records this and defers the fix here. A CLI can paper over it with a printf that knows its own context (`sdf_cli_commands.c:279-283` does exactly that); a wire protocol cannot.
- **Enrolment already streams progress on this characteristic.** `sdf_ble_companion_notify_enroll()` carries step/status notifications that the wizard and the dashboard both consume (`web-companion/app.js:800-902`). Whatever request/reply scheme this change adds has to coexist with that stream, not replace it.
- **The user list is already serialized, in one place, for Zigbee.** `sdf_app_update_zigbee_user_list()` (`sdf_app.c:1389-1447`) builds `[{"id":1,"perm":3,"name":"Alice"}]` from `sdf_services_query_users()` plus a per-user `sdf_storage_web_user_load()`. A second serializer for BLE would be the same query, the same fields, and a second place for the shape to drift.
- **The Enrollment characteristic's admission currently excludes the wizard.** `sdf_ble_companion_enroll_access()` (`sdf_ble_companion.c:864-868`) refuses any connection without live admin authority, which no connection has during wizard step 1.

## Goals / Non-Goals

**Goals:**
- Make the companion able to perform every user-management verb the CLI can, with the same guards.
- Make every request answerable: exactly one terminal reply, correlated, with a reason a UI can render.
- Bring the companion's enrolment path under the same admin-fingerprint gate as every other privileged BLE-originated action.
- Keep the NimBLE host task out of every wait.
- Specify the setup-phase admission the wizard depends on, instead of leaving it broken or implicit.

**Non-Goals:**
- Device state and health reporting. That is `companion-device-health`.
- User-attributed bonds. Deleting a user still does not evict their phone from the allow list; `companion-identity` accepted that risk and this change does not revisit it.
- Defining permission level 2. It stays the reserved placeholder `doc/features.md:11` describes; the companion continues to offer only Admin and Standard.
- CLI changes beyond the outcome enumeration it now shares. The console keeps its commands and its printf-rendered messages.
- Changing the enrolment state machine, its retry policy, or its progress notifications.

## Decisions

### The Enrollment characteristic becomes the user-management characteristic

The alternative was a fifth characteristic. Rejected: the Enrollment characteristic already carries the one user-management verb that exists and already streams its progress, so a new characteristic would split one concern across two handles and give the setup-phase admission question two answers instead of one. The UUID and the notify subscription stay as they are; what changes is that writes carry a verb.

The cost is the breaking payload change — `{"user_id":N,"permission":P}` stops being accepted. There are no devices in the field and the app ships from the same repository, so the two move together.

### Every request carries a client-supplied id; every request gets exactly one terminal reply

```
request:  {"req":<id>, "verb":"delete", "user_id":4}
reply:    {"req":<id>, "result":"last_admin"}
progress: {"req":<id>, "step":2, "status":"..."}     (enroll only)
```

Correlation is needed because the mutating verbs are asynchronous and admin-gated: a reply can arrive ten seconds after its request, after the user has walked to the door and scanned. Without an id, a client that retried, or that has an enrolment streaming, cannot tell which reply belongs to what — the current app already needs a single-slot `pendingBleAdminAction` hack (`web-companion/app.js:651-672`) to work around exactly this on the Config characteristic, and that hack is only sound because the device enforces one pending action at a time.

"Exactly one terminal reply, including for requests rejected before any work starts" is the part that fixes today's silence. An unparseable request, an out-of-range field, and a busy device all produce a reply; the only thing that produces none is a connection that has gone away.

### Distinguishable outcomes, named on the wire

| Result | Means |
|---|---|
| `ok` | The verb completed |
| `not_found` | No such enrolled user |
| `id_occupied` | Enrolment target id already enrolled |
| `last_admin` | Refused: would remove or demote the only admin |
| `name_taken` | Refused: another user holds that name |
| `busy` | Another admin action, permission change or enrolment is in flight |
| `denied` | The authorizing admin scan did not match, or matched a non-admin |
| `timeout` | No authorizing scan arrived within the pending-action window |
| `invalid` | Malformed request, unknown verb, or out-of-range field |

These are produced at the `sdf_services` boundary, not decoded from `esp_err_t` by the BLE layer. Decoding at the edge is what forces every caller to re-derive context — the CLI comment at `sdf_cli_commands.c:279-283` exists precisely because `ESP_ERR_INVALID_STATE` arrived without one. `last_admin` and `busy` are the pair `last-admin-delete-guard` explicitly left conflated; `name_taken` is the one `companion-identity` left conflated.

The CLI keeps its current messages by rendering the same enumeration, so the two surfaces cannot drift into disagreeing about why something was refused.

### Mutating verbs go through the admin-fingerprint gate; nothing waits on the host task

Every mutating verb arms a pending admin action and returns immediately. The GATT callback's only job is to parse, validate shape, and hand off to the `sdf_app` task (which `give-sdf-app-a-task` established for exactly this kind of work). The reply is notified when the action resolves — authorized, denied, or timed out — reusing the resolution path that `sdf_ble_companion_reply_admin_action()` already drives for Nuki re-pair, Zigbee join and Enroll-Admin.

Deletion needs a new action kind; enrolment from the companion needs to route through the gate rather than around it via `sdf_services_request_enrollment()`. Both are additions to an existing mechanism, including the LED mapping that `sdf-services-tasks — Pending Admin Action LED Mapping Is Complete` requires to be exhaustive.

`busy` becomes a first-class answer rather than an accident, because the single-slot gate means a second request genuinely cannot proceed and the client needs to be told to wait rather than shown a generic failure.

### The setup phase admits the wizard's enrolment explicitly

The wizard's first step must write to this characteristic on a connection that cannot yet be authenticated — registration, which is what authenticates it, is step 2 and needs an enrolled admin to authorize it. The gate is therefore stated as: live admin authority, **or** a setup-phase connection on a device with no enrolled users, limited to the enrolment verb.

The "no enrolled users" clause is what keeps this from being a bypass. Once any user exists, the ordinary gate applies, so the exception cannot be used to add a second admin to a device that already has one. It is bounded further by the setup phase itself: one connection at a time, an arm window and a setup deadline, and a wipe of all partial state on expiry (`device-setup-phase`).

An admin fingerprint scan is not required for this case for the same reason `device-setup-phase` already gives for the phase as a whole — "no Admin exists until the wizard enrols one".

### One user-list serializer

The producer moves out of `sdf_app_update_zigbee_user_list()` and both callers use it. The Zigbee path keeps its size limit check (`SDF_ZIGBEE_USER_LIST_MAX`); the BLE path has its own, since the ATT MTU is the binding constraint there and a ten-user list with 31-character names does not fit a single notification. Chunking the list reply is part of this change; inventing a different JSON shape to make it fit is not.

## Risks / Trade-offs

- **Four scans per enrolment.** Accepted; see `proposal.md`. The alternative — exempting the companion path — would make the remote surface the weakest one.
- **Self-demotion and self-deletion are permitted.** The firmware refuses only when the last admin would be lost. An admin who demotes themselves loses their session's authority at its next restricted access, which will look like an abrupt logout. The app warns before submitting; the firmware does not refuse, because the recovery path (another admin fixing it) requires the same verb to stay available.
- **The list reply must be chunked.** Ten users at up to 31 name characters exceeds a single notification at any realistic MTU. This adds a small sequencing concern to an otherwise one-shot protocol. Reusing the OTA characteristic's chunking contract was considered and rejected — that contract is built around a client-driven byte stream with resume, which is far more machinery than a bounded list needs.
- **The setup-phase exception is a real widening of an unauthenticated surface.** Constrained to one verb on a device with no enrolled users, inside a time-bounded phase that wipes on expiry. It is specified as its own requirement so that it is reviewable rather than implicit.

## Open Questions

- Whether `rename` should also require an admin scan. It is not destructive, but under `companion-identity` the name is a login identifier, so a rename changes what someone types to log in. The change assumes it does require a scan, for consistency with the other mutating verbs; if that proves annoying in practice it is a one-line relaxation, whereas adding a gate later is a breaking protocol change.
- Whether the CLI's `user del` should also acquire the admin-fingerprint gate it currently only claims to have (`sdf_cli_commands.c:255-290`, `doc/user_manual.md:219`). This change corrects the documentation; adding the gate is a CLI behaviour change with its own recovery implications and is not attempted here.

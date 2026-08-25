## Why

The device maintains two disjoint user namespaces that describe the same people. Fingerprint users live in the enrolled-user cache keyed by user id 1-10, carry a permission level and an optional display name, and are managed through the CLI and Zigbee. Companion accounts live in a separate five-slot table keyed by its own index, carry a username, salt and stretched credential — and a `permission` field (`firmware/components/sdf_storage/include/sdf_storage.h:59`) that duplicates the fingerprint permission scale verbatim with nothing keeping the two copies in sync.

Nothing links them. Registration is already authorized by an Admin fingerprint scan, and the matched identity is already in hand at `firmware/components/sdf_services/src/sdf_services.c:466-484` — where `match->user_id` is logged and then discarded. The device knows which admin authorized each account and throws that knowledge away.

The consequences are concrete. An admin demoted to standard permission keeps a working companion login, because the session's authority comes from the account's own stored `permission` rather than from the fingerprint user it shadows: the fingerprint gate and the session gate disagree about who is an admin. A deleted admin leaves orphaned credentials that still authenticate. A person can carry two names, one per namespace. And the capacity limits disagree — ten fingerprint users, five account slots, no cap on admins — so the sixth admin's registration fails on a limit unrelated to their permission.

Joining the namespaces removes the duplicated permission, removes the second identity space, and makes the existing admin-fingerprint gate the single source of who may manage the device.

## What Changes

- **BREAKING** A companion account becomes an optional attribute of a fingerprint user rather than a record in its own table. The fingerprint user id is the primary key for both halves; the account's separate index space is removed.
- **BREAKING** Registration binds the new credential to the fingerprint user whose scan authorized it. `match->user_id` is carried from the admin gate into the registration decision instead of being discarded.
- **BREAKING** Session authority is derived live from the bound user's current permission on every authorization decision. The stored `sdf_storage_web_user_t.permission` field is **removed**; a stored copy could only contradict the live value.
- **BREAKING** Only users with admin permission may hold a companion account. This needs no new enforcement — both the pairing window that admits a bond and the registration authorization already resolve through the single `match->permission == 3` gate — but it becomes a stated invariant rather than an emergent one.
- **BREAKING** The companion username and the fingerprint display name become one field. Every user has a name; admins additionally have a credential. Names become unique across users, since the name is now a login identifier.
- Renaming an admin renames their login identifier. Live sessions survive it, because authority is keyed on user id rather than on the name.
- Re-registration by the same admin replaces the stored credential in place. This is the supported password-reset path: the reset is authorized by the same fingerprint scan that authorized the original registration.
- Account capacity is raised from five to ten, matching `SDF_STORAGE_FP_USER_ID_MAX`, so that no admin can be refused an account by a limit unrelated to their permission.
- Deleting a fingerprint user destroys its bound credential.
- Permission level 2 ("Elevated") is documented as reserved and companion-irrelevant. It remains storable and behaviourally identical to level 1, as `doc/features.md:11` and `firmware/components/sdf_app/src/sdf_app.c:996-1005` already record. The companion presents only Admin and Standard, so it does not advertise a capability difference that does not exist.

## Capabilities

### New Capabilities

- `companion-identity`: The joined identity model — one record per fingerprint user, the optional admin-only credential, live-derived session authority, one account per admin, the name as login identifier, capacity, and the reserved status of permission level 2.

### Modified Capabilities

- `ble-companion-service`: REGISTER binds the credential to the authorizing admin's user id; a session's admin authority is looked up live from that user rather than from anything stored on the account; a session whose bound user is deleted or demoted loses authority without needing a cascade.
- `sdf-services-tasks`: Web Registration Authorization carries the matched admin's user id into the registration decision; Web Login Verification resolves the account's permission from the bound fingerprint user instead of from the stored record.
- `web-companion-app`: Registration is presented as admin-only and binds to the confirming fingerprint; the account name is the user's name; re-registration is presented as the password-reset path.

## Impact

**Firmware**
- `sdf_storage`: `sdf_storage_web_user_t` loses its `permission` field; the web-user table and the fingerprint-name keyspace (`sdf_storage_fp_user_name_key`) merge into one record keyed by user id 1-10. `SDF_STORAGE_WEB_USER_MAX` 5 → 10, matching `SDF_STORAGE_FP_USER_ID_MAX`. `sdf_storage_web_user_save/load/clear` become user-id-keyed; `sdf_storage_save_user_name`/`load_user_name`/`delete_user_name` fold into the same record. `sdf_storage_web_user_find_by_name` scans the unified table.
- `sdf_services`: `sdf_services_try_claim_admin_action()` (`sdf_services.c:455-495`) captures `match->user_id` into the pending-request state alongside `request_web_username`/`request_web_password_hash` (`sdf_services_internal.h:65-66`). The registration decision function takes the binding. `sdf_services_delete_user()` destroys the bound credential. Name uniqueness is enforced at enrolment and rename.
- `sdf_ble_companion`: the authenticated-session record holds the bound user id; every admin-authority check resolves permission live.

**Web app**
- `web-companion/`: registration copy states the account belongs to the confirming admin; the account name is the user's name; re-registration is labelled as password reset.

**Docs**
- `doc/features.md`: the Elevated User placeholder gains the note that level 2 is companion-irrelevant, so the open product decision is not silently narrowed by this change.

**Depends on**
- `last-admin-delete-guard`. Under this model deleting the last admin also destroys the last companion account, leaving no way to log in and no admin fingerprint to authorize a new bond or registration — factory reset would be the only recovery. That guard must be real before this lands.

**No migration required** — there are no devices in the field, so the merged record format may be introduced directly with no compatibility path.

**Accepted risks**
- **Bonds are not user-attributed.** Deleting an admin destroys their credential but cannot remove their phone from the allow list, because a bond identifies a device, not a person. That device can still connect and read setup state; it cannot log in, and it cannot authorize any admin action. Removing it remains possible only through the existing failed-login lockout eviction. Making bonds attributable to users is a larger change and is not attempted here.
- **The name is both a display label and a login identifier.** Renaming is therefore a credential-adjacent operation, and a rename that collides with another user must be refused. This is a smaller surface than two independently drifting names, but it does mean a cosmetic-looking action has an authentication consequence.

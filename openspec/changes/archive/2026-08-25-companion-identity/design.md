## Context

See `proposal.md — Why` for motivation. The constraints that shape the approach:

- **The authorizing identity is already in hand and already discarded.** `sdf_services_try_claim_admin_action()` (`sdf_services.c:455-495`) takes the pending action under the lock, gates on `match->permission == 3`, and then logs `match->user_id` before dropping it. Binding requires carrying that value a few frames further, not obtaining it.
- **Credential material deliberately bypasses the event router.** `sdf-services-tasks — Web Registration Authorization` requires the name and password hash to live only in `sdf_services`' owned pending-request state (`sdf_services_internal.h:65-66`), never in an event payload. The authorizing user id joins that state for the same reason: it is part of what decides the persisted record.
- **Permission already has one authoritative home.** The enrolled-user bitmap plus packed 2-bit permissions in `sdf_services_state_t` is established by `sdf-services-tasks — Enrolled-User Cache Is Authoritative From Boot`, loaded synchronously at init and served without a sensor query. `sdf_storage_web_user_t.permission` is a second copy of that fact with no synchronisation; the join deletes the copy rather than adding synchronisation.
- **Two NVS keyspaces already index the same people.** `sdf_storage_fp_user_name_key(user_id)` keys names by fingerprint user id 1-10; the web-user table keys credentials by its own index 0-4. Their name lengths already agree (`SDF_STORAGE_FP_USER_NAME_MAX` and `SDF_STORAGE_WEB_USER_NAME_MAX` are both 32), so merging is dimensional, not lossy.
- **Bonds are not attributable to people.** The allow list holds addressed identities (`sdf_storage_ble_target_save`-shaped records, per `ble-companion-admission`). Nothing associates a bond with the fingerprint user who authorized it, and this change does not add that association.
- **No devices in the field.** No compatibility path is required for any persisted format introduced here.

## Goals / Non-Goals

**Goals:**
- One identity namespace: a person is a fingerprint user, and a companion credential is something that user may or may not have.
- Make the existing admin fingerprint gate the single point that decides who can manage the device, rather than one of two points that can disagree.
- Remove the duplicated permission rather than keeping it in sync.
- Ensure a demotion or deletion takes effect everywhere without depending on a cascade someone has to remember to write.

**Non-Goals:**
- User-attributed bonds. Deleting an admin will not evict their phone from the allow list; see Risks.
- Defining permission level 2. It stays the reserved placeholder `doc/features.md:11` describes; this change only records that it confers no companion access, so that the open product decision is not narrowed by silence.
- Exposing user management over BLE. That is `companion-user-mgmt`, which builds on this model.
- Any change to the LOGIN wire protocol, the key-derivation parameters, or the OTA flow.

## Decisions

### One record per user id, permission excluded

The unified record holds the name and, optionally, the credential:

```
user_record[user_id 1..10]
    name[32]
    has_credential          bool
    salt[16]                valid iff has_credential
    stretched_credential[32]  valid iff has_credential
```

Permission is deliberately absent. It stays in `sdf_services`' packed 2-bit array, which is already authoritative, already persisted, and already the source `sdf_services_change_user_permission()` counts admins from. Putting it in the record would recreate exactly the duplication this change exists to remove.

This makes "only admins hold accounts" an invariant over two structures rather than a field: a record may carry a credential, and the permission array says whether that user is currently an admin. The live lookup reconciles them at decision time, which is why no cascade is needed on demotion.

`SDF_STORAGE_WEB_USER_MAX` becomes 10 and collapses into `SDF_STORAGE_FP_USER_ID_MAX`. The web-user index space disappears; `sdf_storage_web_user_find_by_name()` scans user ids rather than table slots.

### Live lookup instead of a demotion cascade

Two ways to keep a demoted admin from retaining companion authority: delete their account when they are demoted, or stop storing authority on the account at all.

The cascade was rejected because it is a correctness obligation on every future mutation path. Every code path that can change a permission — CLI, Zigbee programming events, the pending `CHANGE_PERMISSION` admin action, and whatever `companion-user-mgmt` adds — would have to remember to fire it, and a missed one is silent: the account keeps working and nothing looks wrong until someone audits it.

Live lookup moves the obligation to a single read at the point of decision. Concretely: the authenticated-connection record in `sdf_ble_companion` gains a bound user id, and the check that today asks "is this connection authenticated?" before serving Config, Enrollment and OTA additionally asks "is the bound user still enrolled, and still permission 3?". Both reads hit the in-memory cache, so the cost is a bitmap test and a 2-bit extract per restricted access — no NVS read, no sensor query.

A useful consequence: deletion needs no separate session-invalidation step either. The bound user's bitmap bit is clear, so the lookup fails and the session loses authority on its next request.

### Name uniqueness enforced where names are written

Making the name a login identifier requires uniqueness, which the fingerprint-name store never needed. The check goes at the two write points — registration (which sets the name) and rename — and scans the unified record table. At ten records with a 32-byte name this is a bounded linear scan of the same table `find_by_name` already walks.

Deletion releases the name implicitly by clearing the record, so no separate reclamation step exists.

### Non-admin names answer LOGIN_INIT as unknown

`ble-companion-service` already requires an unregistered name to receive a deterministic salt and a fresh nonce, indistinguishable from a real account's reply, so that an observer cannot enumerate accounts. The join creates a third category — a name that belongs to a real enrolled user who holds no credential — and that category must fall on the unknown side of the line. Otherwise the challenge response reveals which household members are admins, which is exactly the fact the indistinguishability rule exists to hide.

Implementation is the existing pseudo-salt path (`sdf_storage_web_pseudo_salt_key_load_or_generate`), keyed on the submitted name as it already is. The lookup simply treats "record exists but `has_credential` is false" the same as "no record".

### Registration refuses rather than persists unbound

The registration decision takes the authorizing user id as an input and refuses if it is absent. This is a defensive check on a path that should not be reachable — the authorization gate cannot fire without a match — but the alternative failure mode is a persisted credential belonging to nobody, which the live lookup would then resolve against a nonexistent user on every request. Refusing is cheaper than defining that behaviour.

## Risks / Trade-offs

- **A deleted admin's device keeps its allow-list entry.** Deletion destroys the credential, so that device cannot authenticate and cannot authorize an admin action; it can still connect and read setup state. Removing it requires the existing failed-login lockout eviction. Making bonds user-attributable would fix this properly and is a larger change than this one.
- **Renaming has an authentication consequence.** `user set-name` on an admin changes their login identifier. Live sessions survive it because authority is keyed on the user id, and the old name correctly becomes an unknown name — but a cosmetic-looking operation now invalidates a stored bookmark in someone's browser. The alternative, two names per person, is what this change is removing.
- **A single-admin device concentrates recovery on one finger.** With accounts admin-only and re-registration the reset path, an admin who loses both their password and their enrolled finger has only factory reset. This is unchanged in kind from today — admin actions already depend on that finger — but the join adds the companion account to what is lost with it. `last-admin-delete-guard` prevents the device from reaching a zero-admin state by deletion; it cannot prevent a lost fingerprint.
- **The guard depends on cache accuracy.** Live lookup is only as correct as the enrolled-user cache. The existing rollback and retry rules in `sdf-services-tasks — NVS Write Failure Handling` are what keep the cache and the sensor from diverging; this change adds no new divergence path but does add a new consumer of that guarantee.

## Open Questions

None blocking. `companion-user-mgmt` will need to decide how a rejected rename (duplicate name) and a rejected delete (last admin) report distinguishable reasons to a companion client; `last-admin-delete-guard` documents that same limitation for the CLI-facing path.

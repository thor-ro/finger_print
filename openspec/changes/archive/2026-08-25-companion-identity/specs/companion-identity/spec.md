## ADDED Requirements

### Requirement: Companion Accounts Are An Attribute Of A Fingerprint User

A companion account SHALL be stored as an optional attribute of a fingerprint user record, keyed by that user's fingerprint user id, rather than as a record in an independent table with its own index space. There SHALL be exactly one identity namespace for people known to the device.

The stored user record SHALL carry a name for every enrolled user, and SHALL additionally carry a salt and stretched credential for users that hold a companion account. The user's permission SHALL continue to be held in the enrolled-user permission state rather than duplicated onto the stored record, so that the two can never disagree (see "Session Authority Is Derived Live From The Bound User").

#### Scenario: Account stored against a user id

- **WHEN** a companion account is created
- **THEN** it is stored against the fingerprint user id of the admin it belongs to
- **AND** no separate account index is assigned

#### Scenario: A user without an account

- **WHEN** an enrolled user holds no companion account
- **THEN** the stored user record still carries that user's name
- **AND** the record carries no salt or stretched credential
- **AND** the user's permission remains readable from the enrolled-user permission state

### Requirement: Registration Binds The Credential To The Authorizing Admin

Registration SHALL bind the new credential to the fingerprint user whose scan authorized it. The identity of the matched admin SHALL be captured at the moment the pending registration is authorized and carried into the decision that persists the credential.

Registration SHALL be refused if no authorizing admin identity is available, rather than persisting an unbound credential.

#### Scenario: Credential bound at authorization

- **WHEN** an admin fingerprint scan authorizes a pending companion registration
- **THEN** the user id of the matched admin is captured
- **AND** the persisted credential is stored against that user id

#### Scenario: Unbound registration refused

- **WHEN** a registration would be persisted without an authorizing admin identity
- **THEN** the registration is refused
- **AND** no credential is stored

### Requirement: Only Admins Hold Companion Accounts

Only a user whose permission is admin SHALL hold a companion account. A user whose permission is not admin SHALL have no path to obtain one: they can neither cause a bonded companion identity to be admitted nor authorize a registration, because both resolve through the admin fingerprint gate.

The system SHALL NOT admit a bonded companion identity that an admin did not authorize by fingerprint scan. This is the invariant that makes companion access admin-only; it is stated at the bond level because a bond identifies a device, not a person.

#### Scenario: Non-admin cannot authorize a bond

- **WHEN** a user whose permission is not admin scans while a pairing window is requested
- **THEN** the window does not open
- **AND** no companion identity is admitted

#### Scenario: Non-admin cannot authorize a registration

- **WHEN** a user whose permission is not admin scans while a registration is pending
- **THEN** the registration is denied
- **AND** no credential is stored

#### Scenario: Every admitted identity traces to an admin scan

- **WHEN** the set of admitted companion identities is examined
- **THEN** every entry was admitted during a window opened by an admin fingerprint scan

### Requirement: Session Authority Is Derived Live From The Bound User

The permission that governs an authenticated companion session SHALL be read from the session's bound fingerprint user at the time each authorization decision is made. The system SHALL NOT store a permission on the account record, so that no stored value can contradict the bound user's current permission.

A change to the bound user's permission SHALL take effect on sessions that are already open, without requiring the account to be modified or deleted.

#### Scenario: Authority resolved per decision

- **WHEN** an authenticated session attempts an action that requires admin permission
- **THEN** the bound user's current permission is read at that moment
- **AND** the action is allowed only if that permission is admin

#### Scenario: Demotion takes effect on an open session

- **WHEN** the bound user of an open authenticated session is demoted from admin
- **THEN** subsequent actions on that session that require admin permission are refused
- **AND** no cascade over account records is required for this to hold

#### Scenario: No permission is stored on the account

- **WHEN** an account record is examined
- **THEN** it carries no permission field of its own

### Requirement: One Account Per Admin, Re-Registration Replaces It

A fingerprint user SHALL hold at most one companion account. A registration authorized by an admin who already holds an account SHALL replace that account's stored credential in place rather than creating a second account or being refused as a duplicate.

Replacement SHALL generate a fresh salt and derive a fresh stretched credential, and SHALL NOT retain the previous credential material. This is the supported credential-recovery path: it is authorized by the same admin fingerprint scan that authorized the original registration.

#### Scenario: Re-registration replaces the credential

- **WHEN** an admin who already holds an account completes a registration
- **THEN** that account's salt and stretched credential are replaced
- **AND** the user still holds exactly one account

#### Scenario: Previous credential no longer authenticates

- **WHEN** an account's credential has been replaced by re-registration
- **THEN** a login response computed from the previous credential is rejected

#### Scenario: Two admins hold separate accounts

- **WHEN** two different admins each register
- **THEN** each account is bound to its own user id
- **AND** neither replaces the other

### Requirement: The User's Name Is The Login Identifier

Each enrolled user SHALL have a single name, used both as the display name and, for users holding a companion account, as the login identifier. The system SHALL NOT maintain a separate account username distinct from the user's name.

Names SHALL be unique across enrolled users. Enrolling or renaming a user to a name already held by another user SHALL be refused.

Renaming a user SHALL change the login identifier of that user's account, if they hold one. Sessions already authenticated SHALL survive the rename, because a session's authority is bound to the user id rather than to the name.

#### Scenario: Registration adopts the user's name

- **WHEN** an admin registers a companion account
- **THEN** the account is identified by that user's name
- **AND** no second name is stored for that user

#### Scenario: Duplicate name refused

- **WHEN** a user is renamed to a name already held by another enrolled user
- **THEN** the rename is refused
- **AND** neither user's name changes

#### Scenario: Rename changes the login identifier

- **WHEN** an admin holding an account is renamed
- **THEN** a login challenge requested for the new name resolves to that account
- **AND** a login challenge requested for the previous name is treated as an unknown user

#### Scenario: Rename does not disturb an open session

- **WHEN** the bound user of an open authenticated session is renamed
- **THEN** that session remains authenticated
- **AND** its authority continues to resolve from the same user id

### Requirement: Deleting A User Destroys Its Companion Account

Deleting a fingerprint user SHALL destroy any companion account bound to that user, so that no credential outlives the identity it authenticates.

Deletion SHALL NOT be expected to remove bonded companion identities, because a bond identifies a device rather than a person and is not attributable to a user. A device bonded before the deletion may still connect; it SHALL NOT be able to authenticate, since the credential is gone, and SHALL NOT be able to authorize any admin action, since the fingerprint is gone.

#### Scenario: Account destroyed with the user

- **WHEN** an enrolled user holding a companion account is deleted
- **THEN** that account's salt and stretched credential are destroyed
- **AND** a login challenge requested for that name is treated as an unknown user

#### Scenario: Name is released for reuse

- **WHEN** a user holding a companion account is deleted
- **THEN** that user's name becomes available for another user

#### Scenario: A previously bonded device retains connectivity only

- **WHEN** a device bonded under a since-deleted admin connects
- **THEN** the connection is accepted if the identity remains admitted
- **AND** no login on that connection can succeed

### Requirement: Account Capacity Matches User Capacity

The number of companion accounts the system can store SHALL be at least the maximum number of enrolled fingerprint users, so that no admin can be refused an account by a capacity limit unrelated to their permission.

#### Scenario: Every admin can register

- **WHEN** the maximum supported number of users are enrolled and all hold admin permission
- **THEN** each of them can hold a companion account
- **AND** no registration is refused for lack of account capacity

### Requirement: Permission Level 2 Is Reserved And Companion-Irrelevant

Permission level 2 SHALL remain a reserved placeholder with no defined capability, as recorded in `doc/features.md`. It SHALL be storable and SHALL behave identically to level 1 in every authorization decision, including companion access: a level 2 user holds no companion account and cannot authorize a bond or a registration.

The companion SHALL NOT present level 2 as a selectable permission, so that it does not advertise a capability difference the system does not implement.

#### Scenario: Level 2 confers no companion access

- **WHEN** a user with permission level 2 is examined for companion access
- **THEN** they hold no account
- **AND** they cannot authorize a bond or a registration

#### Scenario: Companion presents two permission levels

- **WHEN** the companion displays or offers a user's permission
- **THEN** only Admin and Standard are presented

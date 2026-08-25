# Spec: companion-user-mgmt

## Purpose
This specification covers user-management operations performed via the BLE Companion Service, enabling enrolled users to be listed, enrolled, deleted, renamed, and have their permissions updated remotely while preserving security invariants and admin authorization requirements.

## Requirements

### Requirement: The Companion Can Perform Every User-Management Verb

The companion SHALL be able to list enrolled users, enrol a new user, delete a user, change a user's permission, and rename a user. These verbs SHALL be reachable from the companion without physical access to the device's serial console, since the companion is the only supported management surface for a claimed device.

Each verb SHALL apply the same guards as the equivalent console command, including the last-admin guard and the name-uniqueness rule, so that the two surfaces cannot disagree about what is permitted.

#### Scenario: Users listed
- **WHEN** an authorized companion session requests the user list
- **THEN** system reports every enrolled user with its id, permission and name

#### Scenario: User deleted from the companion
- **WHEN** an authorized companion session deletes an enrolled user and the deletion is authorized
- **THEN** that user is no longer enrolled
- **AND** any companion account bound to that user is destroyed with it

#### Scenario: Permission changed from the companion
- **WHEN** an authorized companion session changes an enrolled user's permission and the change is authorized
- **THEN** that user's permission is updated in the authoritative enrolled-user state

#### Scenario: User renamed from the companion
- **WHEN** an authorized companion session renames an enrolled user and the rename is authorized
- **THEN** that user's name is updated
- **AND** the name that user logs in with changes accordingly

#### Scenario: Guards apply equally on both surfaces
- **WHEN** a verb is refused by a guard on the console
- **THEN** the same verb with the same arguments is refused for the same reason from the companion

### Requirement: Refusals Are Reported With Distinguishable Reasons

Every user-management verb SHALL report its outcome as a distinguishable reason rather than a shared error code. The reasons SHALL at minimum distinguish: success; no such user; the target id is already enrolled; refused because it would leave no admin; refused because another user holds that name; the device is busy with another action; the authorizing scan was denied; the authorizing scan did not arrive in time; and the request was malformed.

No two of these conditions SHALL be reported identically. In particular, "would leave no admin" SHALL be distinguishable from "the device is busy", and "another user holds that name" SHALL be distinguishable from a general failure, since both were previously conflated.

The console SHALL report the same reasons, so that the two surfaces cannot diverge in what they say about the same refusal.

#### Scenario: Last-admin refusal is distinguishable from busy
- **WHEN** a permission change is refused because it would demote the only admin
- **THEN** the reported reason differs from the reason reported when the device is busy with another action

#### Scenario: Duplicate name refusal is distinguishable
- **WHEN** a rename is refused because another enrolled user holds that name
- **THEN** the reported reason identifies the name conflict specifically

#### Scenario: Occupied id refusal is distinguishable
- **WHEN** an enrolment is refused because the target user id is already enrolled
- **THEN** the reported reason identifies the occupied id rather than reporting a generic invalid request

#### Scenario: Denied and timed-out scans are distinguishable
- **WHEN** an authorizing scan is refused because the finger did not match an admin
- **THEN** the reported reason differs from the reason reported when no scan arrived before the pending action expired

#### Scenario: Console and companion agree
- **WHEN** the same refusal occurs on the console and on the companion
- **THEN** both report the same underlying reason

### Requirement: A User May Change Their Own Enrolment

The system SHALL permit an authorized user to delete or demote their own enrolled user, subject only to the last-admin guard. It SHALL NOT add a separate self-targeting refusal.

Because session authority is resolved live from the bound user (`companion-identity`), such a change SHALL take effect on the acting session itself at its next restricted access.

#### Scenario: Self-demotion permitted while another admin exists
- **WHEN** an admin changes their own permission to standard and at least one other admin remains enrolled
- **THEN** the change is applied

#### Scenario: Self-demotion of the last admin refused
- **WHEN** an admin changes their own permission to standard and they are the only enrolled admin
- **THEN** the change is refused by the last-admin guard

#### Scenario: Acting session loses authority after self-demotion
- **WHEN** an admin successfully demotes their own user
- **THEN** their session's next restricted access is refused

### Requirement: The User List Has One Serialized Shape

The system SHALL produce the list of enrolled users from a single serializer, shared by every consumer that reports it. It SHALL NOT maintain a second representation of the same list for a different transport.

A user holding no name SHALL be represented by the absence of the name rather than by an empty name.

#### Scenario: One producer
- **WHEN** the user list is reported to a companion client and to a Zigbee central
- **THEN** both are produced by the same serializer with the same field names and value shapes

#### Scenario: Nameless user represented by absence
- **WHEN** an enrolled user has no name recorded
- **THEN** the serialized entry omits the name rather than carrying an empty one

### Requirement: A Truncated User List Is Never Mistaken For A Complete One

When the user list does not fit a single reply, the system SHALL deliver it in ordered parts and SHALL mark the final part explicitly. A client SHALL NOT have to infer completeness from a part's size or from the absence of a further notification.

#### Scenario: Multi-part list is terminated explicitly
- **WHEN** the user list requires more than one notification to deliver
- **THEN** the parts are delivered in order
- **AND** the final part carries an explicit end marker

#### Scenario: Interrupted list is not treated as complete
- **WHEN** delivery of a multi-part list stops before the final part
- **THEN** the client has received no end marker and does not treat the partial list as the full set of users

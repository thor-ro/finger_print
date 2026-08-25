## ADDED Requirements

### Requirement: User Management View

The Web App SHALL provide a user-management view for an authenticated admin session that lists every enrolled user with their id, name and permission, and that offers enrolling a new user, deleting a user, changing a user's permission, and renaming a user.

The view SHALL be reachable after setup completes, not only during the wizard, since it is the only remote surface for these operations.

#### Scenario: Enrolled users listed
- **WHEN** an authenticated admin opens the user-management view
- **THEN** the app lists each enrolled user with id, name and permission

#### Scenario: Verbs offered
- **WHEN** the user-management view is shown
- **THEN** the app offers enrol, delete, permission change and rename

#### Scenario: View available on a claimed device
- **WHEN** an authenticated admin connects to a device whose setup is complete
- **THEN** the user-management view is reachable without re-running the wizard

### Requirement: Each Verb States The Physical Action It Requires

Before submitting a verb that requires a fingerprint scan, the Web App SHALL tell the user which scans are needed and in what order — the authorizing admin scan, and for enrolment the three enrolment scans that follow — so that a user standing away from the device knows the request cannot complete without them.

The app SHALL indicate that a request is waiting for a scan, rather than presenting it as finished, until the device reports its outcome.

#### Scenario: Authorizing scan announced
- **WHEN** the user submits a delete, permission change or rename
- **THEN** the app states that an admin fingerprint scan at the device is required to authorize it

#### Scenario: Enrolment scan sequence announced
- **WHEN** the user submits an enrolment
- **THEN** the app states that an admin scan authorizes the request and that the new user then scans three times

#### Scenario: Pending request is not shown as complete
- **WHEN** a request is waiting for an authorizing scan
- **THEN** the app shows it as awaiting the scan
- **AND** it does not report success until the device reports the outcome

### Requirement: Refusals Are Rendered Specifically

The Web App SHALL render the specific reason a user-management request was refused — that it would leave no admin, that the name is taken, that the target id is already enrolled, that the device is busy, that the scan was denied, or that no scan arrived in time — rather than a generic failure message.

#### Scenario: Last-admin refusal explained
- **WHEN** the device refuses a delete or demotion because it would leave no admin
- **THEN** the app says so, rather than reporting a generic error

#### Scenario: Name conflict explained
- **WHEN** the device refuses a rename or enrolment because another user holds that name
- **THEN** the app says the name is already in use

#### Scenario: Busy explained as retryable
- **WHEN** the device reports it is busy with another action
- **THEN** the app says so and invites the user to retry, rather than reporting a failure

#### Scenario: Denied and timed-out scans explained differently
- **WHEN** an authorizing scan is denied
- **THEN** the app's message differs from the one it shows when no scan arrived in time

### Requirement: Self-Affecting Changes Are Warned Before Submission

The Web App SHALL warn the user before submitting a change that deletes or demotes the user their own session is bound to, stating that their session will lose its authority. It SHALL NOT block the change, since another admin correcting a mistake needs the same operation.

#### Scenario: Self-demotion warned
- **WHEN** the user submits a permission change targeting their own bound user, lowering it below admin
- **THEN** the app warns that their session will lose admin authority before submitting

#### Scenario: Self-deletion warned
- **WHEN** the user submits a delete targeting their own bound user
- **THEN** the app warns that their session will lose authority and their account will be destroyed

#### Scenario: Warning does not prevent the change
- **WHEN** the user confirms the warning
- **THEN** the app submits the request

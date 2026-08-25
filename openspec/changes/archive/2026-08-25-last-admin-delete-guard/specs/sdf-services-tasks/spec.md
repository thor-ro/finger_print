## ADDED Requirements

### Requirement: Last Remaining Admin Cannot Be Deleted

`sdf_services_delete_user()` SHALL reject deletion of a user whose permission is admin when that user is the only enrolled admin, and SHALL report the rejection distinctly from a sensor failure. The rejection SHALL be decided before any fingerprint sensor operation is issued, so that a refused delete costs no sensor round-trip and cannot leave the sensor and the cached enrolled-user record disagreeing.

The admin count SHALL be computed from the cached enrolled-user bitmap and packed permissions, consistent with `sdf_services_change_user_permission()`, and SHALL NOT issue a sensor query.

This guard protects the single admin-fingerprint gate on which every admin action depends. Losing the last admin leaves the pairing window, Enroll-Admin, Nuki re-pair, Zigbee join and Web Registration Authorization permanently unreachable, recoverable only by factory reset.

`sdf_services_clear_all_users()` SHALL NOT be subject to this guard. It is a deliberate bulk erase on the factory-reset path, where removing the last admin is the intended outcome.

#### Scenario: Deleting the only admin is refused

- **WHEN** a caller requests deletion of an enrolled user with admin permission
- **AND** that user is the only enrolled admin
- **THEN** the request is rejected
- **AND** no fingerprint sensor delete is issued
- **AND** the cached enrolled-user record is unchanged

#### Scenario: Deleting an admin while another admin remains succeeds

- **WHEN** a caller requests deletion of an enrolled user with admin permission
- **AND** at least one other enrolled user also has admin permission
- **THEN** the deletion proceeds
- **AND** the cached enrolled-user record and its NVS persistence are updated

#### Scenario: Deleting a non-admin user is unaffected

- **WHEN** a caller requests deletion of an enrolled user whose permission is not admin
- **AND** exactly one admin is enrolled
- **THEN** the deletion proceeds regardless of the admin count

#### Scenario: Admin count is read from the cache

- **WHEN** the guard evaluates how many admins are enrolled
- **THEN** the count is derived from the cached bitmap and packed permissions
- **AND** no fingerprint sensor query is issued to obtain it

#### Scenario: Clear-all is exempt

- **WHEN** all users are cleared through the bulk clear-all path
- **THEN** the operation proceeds even though it removes the last admin

### Requirement: User Deletion Validates Enrolment Before Touching The Sensor

`sdf_services_delete_user()` SHALL report a request to delete a user that is not present in the cached enrolled-user record as not-found, distinctly from a sensor failure, and SHALL NOT issue a fingerprint sensor delete for that user.

#### Scenario: Deleting an unenrolled user id

- **WHEN** a caller requests deletion of a user id that is not set in the cached enrolled-user bitmap
- **THEN** the request is reported as not found
- **AND** no fingerprint sensor delete is issued

#### Scenario: Not-found is distinguishable from sensor failure

- **WHEN** a caller receives a rejection for deleting an unenrolled user
- **THEN** the reported result differs from the result reported when the sensor rejects a delete for an enrolled user

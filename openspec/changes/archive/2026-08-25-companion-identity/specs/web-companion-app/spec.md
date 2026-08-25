## MODIFIED Requirements

### Requirement: User Registration
The Web App SHALL provide a registration form that hashes the user's password locally (e.g., using SHA256) before sending it over BLE to the bridge. The bridge is responsible for salting and stretching the received hash before persisting it; the Web App does not need to know or apply the bridge's key-derivation parameters at registration time.

The name submitted at registration is the user's name on the device, not a separate account username. The Web App SHALL present it as such.

Registration is authorized by an Admin fingerprint scan, and the resulting account belongs to the admin whose finger confirmed it. The Web App SHALL make this ownership explicit to the user, so that it is clear the account is not anonymous and that a different admin's scan would create or replace a different account.

During first-time setup, registration SHALL be offered only after an Admin fingerprint has been enrolled, since the bridge authorizes registration with an Admin fingerprint scan.

When the confirming admin already holds an account, registration replaces that account's credential. The Web App SHALL present this as the supported way to reset a forgotten password, and SHALL warn the user before submitting that the existing credential for that admin will be replaced.

#### Scenario: Secure Registration
- **WHEN** user submits the registration form
- **THEN** web app computes SHA256 hash of password
- **AND** transmits name and hash to the bridge
- **AND** prompts the user to "Please scan the Admin Finger on the device to confirm."

#### Scenario: Registration unavailable before an admin exists
- **WHEN** the app is connected to a device in the setup phase with no enrolled users
- **THEN** the registration form is not offered

#### Scenario: Ownership of the account is stated
- **WHEN** the registration form is presented
- **THEN** the app states that the account will belong to the admin who confirms it with a fingerprint scan

#### Scenario: Re-registration presented as password reset
- **WHEN** the user opens registration on a device where accounts already exist
- **THEN** the app explains that registering again with an admin's scan replaces that admin's existing password
- **AND** the app warns before submitting that the existing credential for the confirming admin will be replaced

#### Scenario: Permission levels presented
- **WHEN** the app displays or offers a user's permission
- **THEN** only Admin and Standard are presented
- **AND** the reserved intermediate level is not offered as a choice

# Spec: web-companion-app

## Purpose
This specification covers the Web Companion App, a static web application that connects to the Smart Door Bridge via Web Bluetooth to allow users to register, manage device configuration, and trigger OTA firmware updates.

## Requirements

### Requirement: Web Bluetooth Connection
The Web App SHALL use the Web Bluetooth API (WebBLE) to scan for and connect to the Smart Door Bridge's Companion Service.

#### Scenario: Device Discovery
- **WHEN** user clicks "Connect" in the web app
- **THEN** browser prompts with Web Bluetooth device picker filtering for the Smart Door service UUID

### Requirement: Static App Location
The Web App SHALL be maintained as source in the repository's `web-companion/` directory and SHALL be compiled by that directory's build to static assets that require no server-side runtime and no third-party origin at run time, suitable for GitHub Pages deployment.

The build output — not the source directory — SHALL be the deployment artifact. The source directory SHALL carry the instructions needed to install the toolchain, build the app, and deploy the result.

#### Scenario: GitHub Pages deployment assets are present
- **WHEN** the repository is prepared for deployment
- **THEN** `web-companion/` contains the source, build configuration, and deployment instructions needed to produce and host the app
- **AND** the assets uploaded to the host are the build's output rather than the source files

#### Scenario: Built assets need no runtime dependencies
- **WHEN** the built assets are served by a static file host
- **THEN** the app runs without a server-side component
- **AND** it loads no code, styles or fonts from any other origin

### Requirement: First-Time Setup Wizard
The Web App SHALL provide a first-time setup wizard that guides the user through claiming an unclaimed device. The wizard SHALL be the only supported path for first-time setup; the device exposes no physical alternative for enrolling the first Admin or pairing the Nuki lock.

The wizard SHALL read the device's setup state before login and resume the user at the step that state implies, so that a user who reconnects mid-setup is not asked to repeat completed steps within the same setup phase.

#### Scenario: Wizard entered on an unclaimed device
- **WHEN** the app connects to a device reporting that setup has not started
- **THEN** the app presents the setup wizard rather than the login form

#### Scenario: Wizard skipped on a completed device
- **WHEN** the app connects to a device reporting setup complete
- **THEN** the app presents the ordinary login form

#### Scenario: Wizard resumes at the reported step
- **WHEN** the app connects to a device in the setup phase reporting that a companion account is registered but Nuki is not paired
- **THEN** the wizard resumes at the Nuki pairing step

#### Scenario: Wizard resumes at registration when no account exists
- **WHEN** the app connects to a device in the setup phase reporting that an Admin is enrolled but no companion account has been registered
- **THEN** the wizard resumes at the account registration step
- **AND** it does not skip ahead to Nuki pairing

### Requirement: Wizard Step Order
The wizard SHALL guide the user through Admin fingerprint enrolment first, then companion account registration, then Nuki pairing, then explicit completion. Account registration SHALL NOT be offered before an Admin fingerprint has been enrolled, because registration is authorized by an Admin fingerprint scan that would otherwise have nothing to match.

#### Scenario: Admin enrolment precedes registration
- **WHEN** the wizard starts on a device with no enrolled users
- **THEN** the first step presented is Admin fingerprint enrolment
- **AND** the registration form is not reachable until enrolment succeeds

#### Scenario: Registration authorized by the newly enrolled admin
- **WHEN** the user submits the registration form during the wizard
- **THEN** the app prompts the user to scan the Admin finger on the device to confirm
- **AND** the scan is matched against the Admin enrolled in the previous step

#### Scenario: Nuki pairing guided with physical instructions
- **WHEN** the wizard reaches the Nuki pairing step
- **THEN** the app instructs the user to put the Nuki lock into pairing mode before proceeding
- **AND** reports the pairing outcome returned by the device

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

### Requirement: User Login
The Web App SHALL log a user in using a two-step challenge-response exchange rather than sending a static password hash. The app SHALL first request a login challenge for the entered username, then locally derive a stretched credential from the password using the key-derivation parameters (salt and iteration count) returned in that challenge, and finally send a response computed from that stretched credential and the challenge's nonce.

#### Scenario: Login challenge requested
- **WHEN** user submits the login form
- **THEN** web app sends the entered username to the bridge to request a login challenge
- **AND** app receives a salt, an iteration count, and a nonce in response

#### Scenario: Login response computed and sent
- **WHEN** web app has received a login challenge
- **THEN** app derives a stretched credential from the entered password using the received salt and iteration count
- **AND** app computes a response value from the stretched credential and the received nonce
- **AND** app sends that response value to the bridge to complete login

#### Scenario: Successful login
- **WHEN** the bridge accepts the app's login response
- **THEN** app transitions to the authenticated state and enables access to Config, Enrollment, and OTA features

#### Scenario: Failed login
- **WHEN** the bridge rejects the app's login response
- **THEN** app reports a login failure to the user without revealing whether the entered username exists

### Requirement: OTA Battery Warning
The Web App SHALL warn the user before triggering an OTA update about potential battery drain and SHALL stream the selected firmware image as a chunked binary transfer over the OTA characteristic using the begin/chunk/end opcode contract.

The warning SHALL state the device's reported battery level, taken from the device health report, rather than asking the user to establish it by other means. Where the battery level is unknown, the warning SHALL say so, and SHALL NOT imply that the level has been checked.

The app SHALL NOT re-send a chunk whose acknowledgement did not arrive. A chunk message carries no offset, so a device that wrote the chunk and lost only the acknowledgement would append the same bytes twice. The app SHALL instead re-establish the device's confirmed offset by re-sending the begin-transfer message, and SHALL continue from the offset the device reports.

#### Scenario: A lost acknowledgement never duplicates bytes
- **WHEN** a chunk message draws no response within the app's response timeout
- **THEN** app does not re-send that chunk
- **AND** app re-sends the begin-transfer message with the same declared image size and continues from the offset the device reports in its `ready` response

#### Scenario: A late acknowledgement is not read as the resume response
- **WHEN** the acknowledgement of a timed-out chunk arrives while the app is waiting for its begin-transfer response
- **THEN** app discards that acknowledgement rather than treating it as the device's confirmed resume offset

#### Scenario: OTA Warning
- **WHEN** user selects a firmware file and starts an OTA update
- **THEN** app displays a warning: "Ensure your battery is above 20%. OTA transfer over Bluetooth draws significant power and firmware is large — keep the app open and the device nearby until it completes."
- **AND** app writes a begin-transfer message (`0x01`, 4-byte little-endian image size) to the OTA characteristic after user confirmation
- **AND** the warning states the device's reported battery level alongside that text

#### Scenario: Unknown battery stated as unknown
- **WHEN** user starts an OTA update and the device reports no battery measurement
- **THEN** app states that the battery level is unknown
- **AND** app does not display a battery percentage in the warning

#### Scenario: Chunked transfer with progress
- **WHEN** the begin-transfer message is acknowledged with a `ready` status
- **THEN** app writes the firmware image as a sequence of chunk messages (`0x02` + raw bytes), each sized to the connection's negotiated ATT MTU minus 4 bytes of overhead
- **AND** app updates a progress indicator from each `chunk_ack` notification's confirmed byte offset against the declared image size
- **AND** app writes an end-transfer message (`0x03`, no payload) once all bytes have been sent

#### Scenario: Transfer completion is inferred, not just notified
- **WHEN** the app has written the end-transfer message
- **THEN** app shows a "verifying and installing, device will restart" status and starts a bounded grace period
- **AND** if a `failed` notification arrives within the grace period, app reports the failure and its error to the user
- **AND** if instead the BLE connection drops within the grace period without a `failed` notification, app treats this as a presumed success (the device's successful-commit path reboots before it can send a `success` notification) and prompts the user to reconnect to confirm the new firmware version
- **AND** if neither a `failed` notification nor a disconnect occurs before the grace period elapses, app reports an ambiguous/unknown outcome rather than guessing

#### Scenario: Resume after disconnect
- **WHEN** the BLE connection drops mid-transfer and the app reconnects and re-authenticates before the device's OTA idle timeout elapses
- **THEN** app re-sends the begin-transfer message with the same declared image size
- **AND** app resumes chunk-sending from the byte offset returned in the `ready` response instead of restarting the transfer from byte 0

### Requirement: Explicit Setup Completion
The Web App SHALL complete setup by issuing an explicit completion request over its authenticated session, and SHALL inform the user that completing setup locks the device to the currently connected companion device.

#### Scenario: User completes setup
- **WHEN** the user confirms the final wizard step
- **THEN** the app issues the setup-completion request
- **AND** on success reports that the device is now claimed and paired to this browser

#### Scenario: Completion rejected
- **WHEN** the device rejects the completion request because prerequisites are unmet
- **THEN** the app reports which step is still outstanding and returns the user to it

#### Scenario: Completion fails on a device fault
- **WHEN** the device rejects the completion request because of an internal fault rather than an unmet prerequisite
- **THEN** the app reports a device error and offers to retry completion
- **AND** it does not send the user back to redo a step that already succeeded

### Requirement: Setup Timeout Is Communicated
The Web App SHALL inform the user that the setup phase is time-bounded and that a lapse discards all progress, and SHALL report a lapse when the connection is lost to a setup timeout.

#### Scenario: Timeout is disclosed before setup starts
- **WHEN** the wizard is presented on an unclaimed device
- **THEN** the app states that setup must be completed within the device's setup window and that a lapse erases all progress

#### Scenario: Lapsed setup is reported
- **WHEN** the device disconnects and stops advertising without setup having completed
- **THEN** the app reports that the setup window elapsed, that progress was discarded, and that the user must press the device button to start again

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

Every outcome the device is capable of reporting SHALL have a message written for it, including its generic failure and unavailable outcomes. The app SHALL NOT present a raw protocol token to the user as the explanation of a refusal.

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

#### Scenario: Every device outcome has a message
- **WHEN** the device reports any outcome its firmware can produce, including a generic failure or an unavailable subsystem
- **THEN** the app shows a message written for that outcome
- **AND** the raw protocol token is not shown to the user as the explanation

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

### Requirement: Device Health View

The Web App SHALL present the device health report to an authenticated session, covering the lock state, the battery level, active alarms, fingerprint sensor readiness, the lock link, the radio's network state, the running firmware version and the OTA state.

The view SHALL update from the device's health notifications rather than by polling, and SHALL be available to any authenticated user, not only an admin.

#### Scenario: Health shown after authentication
- **WHEN** a user authenticates
- **THEN** the app presents the device health report

#### Scenario: View updates on notification
- **WHEN** the device notifies a changed health report
- **THEN** the displayed values update without the user reloading or re-requesting

#### Scenario: Available to a standard user
- **WHEN** a user whose bound user holds standard permission authenticates
- **THEN** the health view is available to them

### Requirement: The App Displays Only Values The Device Measured

The Web App SHALL display a device value only where the device reported it as a measurement. Where the device reports a value as unknown or not applicable, the app SHALL display that condition, and SHALL NOT display a number, a placeholder reading, or a value carried over from an earlier report as though it were current.

The app SHALL NOT display any configuration setting as a device measurement. In particular it SHALL NOT display the configured default battery percentage as the device's battery level.

#### Scenario: Unknown displayed as unknown
- **WHEN** the device reports a value as unknown
- **THEN** the app displays it as unknown
- **AND** the app displays no number for it

#### Scenario: Not applicable distinguished from unknown
- **WHEN** the device reports a subsystem as not applicable
- **THEN** the app's display of it differs from how it displays an unknown value

#### Scenario: Configuration is not displayed as measurement
- **WHEN** the app displays the device's battery level
- **THEN** the value shown came from the health report
- **AND** it did not come from the configured default battery percentage

#### Scenario: Stale value is not shown as current
- **WHEN** a previously reported value is no longer available
- **THEN** the app stops presenting the earlier value as the current one

### Requirement: Assumed Lock State Is Shown As Unconfirmed

Where the device reports a lock state it has assumed from a command rather than one the lock confirmed, the Web App SHALL show it as awaiting confirmation. It SHALL NOT present an assumed state identically to a confirmed one.

Where the device reports the age of a reading and that reading is old enough to be misleading, the app SHALL show how old it is.

#### Scenario: Assumed state marked
- **WHEN** the device reports a lock state marked as assumed
- **THEN** the app shows it as awaiting confirmation from the lock

#### Scenario: Confirmed state not marked
- **WHEN** the device reports a lock state confirmed by the lock
- **THEN** the app shows it without the awaiting-confirmation marking

#### Scenario: Old reading's age surfaced
- **WHEN** the device reports a reading old enough to be misleading
- **THEN** the app shows how old the reading is

### Requirement: An Action's Outcome Is Reported Where The Action Was Taken

The Web App SHALL report the outcome of an action in the part of the interface from which that action was initiated, so that a message cannot be read as belonging to an unrelated operation.

In particular, the outcome of reading or writing device configuration SHALL NOT be reported in the firmware-update area of the interface.

#### Scenario: Configuration outcome appears with the configuration controls
- **WHEN** the user reads or applies device configuration
- **THEN** the resulting message is shown with the configuration controls
- **AND** it does not appear in the firmware-update area

#### Scenario: Firmware-update area reports only firmware updates
- **WHEN** the firmware-update area shows a status message
- **THEN** that message concerns the firmware update



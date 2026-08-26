## MODIFIED Requirements

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

## ADDED Requirements

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

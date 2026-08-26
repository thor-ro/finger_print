# Web Companion Tooling

## Purpose

Defines toolchain, build gates, bundle budgets, static hosting, testing, and behavioral requirements for the Web Companion application.

## Requirements

### Requirement: Built From Source With A Pinned Toolchain

The Web Companion SHALL be built from source by a committed toolchain, and the exact dependency versions SHALL be pinned by a lockfile committed to the repository. A clean clone SHALL be buildable with a documented install command followed by a documented build command, without any manually installed global tooling beyond a stated Node.js version.

Continuous integration SHALL install from the lockfile rather than re-resolving dependency ranges, so that the versions CI builds against are the versions a developer builds against.

#### Scenario: Clean clone builds
- **WHEN** the repository is cloned fresh and the documented install and build commands are run
- **THEN** the build completes and produces the deployable output directory

#### Scenario: CI installs from the lockfile
- **WHEN** the deployment or verification workflow installs dependencies
- **THEN** it installs the versions recorded in the committed lockfile
- **AND** it fails if the lockfile and the manifest disagree, rather than resolving newer versions silently

#### Scenario: Node version is stated
- **WHEN** a developer or a workflow prepares to build the app
- **THEN** the required Node.js version is recorded in the repository
- **AND** the workflow uses that version rather than an unpinned default

### Requirement: Shipped Output Is Static And Free Of Runtime Dependencies

The build SHALL produce prerendered static assets that can be served by any static file host with no origin server, no server-side rendering and no runtime backend.

The shipped assets SHALL NOT fetch code, styles, fonts, or telemetry from any third-party origin at runtime. Every asset the app loads SHALL be served from the same origin as the app itself.

#### Scenario: Served from a static host
- **WHEN** the build output is served by a plain static file server
- **THEN** the app loads and reaches its connection screen without any server-side component

#### Scenario: No third-party requests
- **WHEN** the app is loaded and used
- **THEN** it issues no network request to an origin other than the one it was served from

#### Scenario: Build output is not committed
- **WHEN** the repository is inspected
- **THEN** the build output directory is not tracked in version control
- **AND** the deployment builds it rather than reading a committed copy

### Requirement: Deployed Correctly Under The Project Site Base Path

The deployed build SHALL resolve all of its asset URLs under the path at which the app is published, which for this repository is a GitHub Pages project subpath rather than the origin root.

The published output SHALL disable the host's Jekyll processing, so that build directories whose names begin with an underscore are not stripped from the deployment.

#### Scenario: Assets resolve under the subpath
- **WHEN** the deployed app is opened at its published project-site URL
- **THEN** its scripts, styles and other assets load successfully from under that subpath
- **AND** none of them are requested from the origin root

#### Scenario: Underscore-prefixed asset directories survive deployment
- **WHEN** the build output is published to the static host
- **THEN** directories whose names begin with an underscore are present in the deployed site

#### Scenario: Local development uses no base path
- **WHEN** a developer runs the development server locally
- **THEN** the app is served at the local root without the deployment subpath

### Requirement: A Bundle Budget Is Declared And Enforced

A maximum transferred size for the app's initial load — its HTML, CSS and JavaScript, measured compressed — SHALL be declared in a file in the repository, and SHALL be checked against the built output by continuous integration.

Exceeding the declared budget SHALL fail the build rather than emit a warning.

Code that the app defers to a lazily loaded chunk SHALL also be covered by a declared budget, so that moving weight behind a deferred import reduces the measured initial load without escaping measurement.

Recording a measurement SHALL NOT raise a declared budget. A budget may be tightened at any time; raising one SHALL be a deliberate, separately justified change rather than a by-product of measuring what the build currently produces.

#### Scenario: Budget is reviewable
- **WHEN** the budget is changed
- **THEN** the change is visible as a diff to a declared value in the repository, not as an edit buried in workflow steps

#### Scenario: Oversized build fails
- **WHEN** a build produces an initial load larger than the declared budget
- **THEN** the workflow fails
- **AND** it reports the measured size alongside the budget

#### Scenario: Budget is checked on the built output
- **WHEN** the budget check runs
- **THEN** it measures the compressed size of the assets the browser actually downloads on first load, not the size of the source files

#### Scenario: Deferred code is measured too
- **WHEN** part of the interface is moved behind a lazily loaded import
- **THEN** that chunk is measured against a declared budget of its own
- **AND** the total the app loads to reach its main view remains bounded by a declared figure

#### Scenario: Measuring does not loosen the gate
- **WHEN** a measured size is recorded after a build
- **THEN** the declared budget is left unchanged or tightened
- **AND** it is not raised to sit above the size just measured

### Requirement: Device-Supplied Values Are Escaped By The Rendering Layer

The app SHALL render every device-supplied and user-supplied value through the framework's escaping text interpolation. It SHALL NOT construct markup by string concatenation for such values, and SHALL NOT pass them through a raw-HTML rendering facility.

The absence of raw-HTML rendering in the application source SHALL be enforced by a lint rule that runs in continuous integration, so that a regression fails the build rather than relying on review.

#### Scenario: A name containing markup renders as text
- **WHEN** the device reports a user whose name contains HTML markup or a quotation mark
- **THEN** the app displays those characters literally as part of the name
- **AND** no part of the name is interpreted as markup or as script

#### Scenario: Raw-HTML rendering fails lint
- **WHEN** application source introduces a raw-HTML rendering construct
- **THEN** the lint step fails the build

#### Scenario: Handlers are bound without global names
- **WHEN** the app renders a control that acts on a listed device record
- **THEN** the handler is bound by the framework
- **AND** the record's values are not embedded into a markup attribute to carry them to the handler

### Requirement: Protocol Logic Is Testable Without A Browser

The encoding and decoding of the device protocol — the login challenge and response derivation, the OTA begin/chunk/end framing and resume offset handling, user-management request encoding and reply decoding, health report parsing, and setup-state decoding — SHALL live in modules that depend on neither the DOM nor the Web Bluetooth API.

Those modules SHALL be covered by automated unit tests that run headlessly in continuous integration. Access to Web Bluetooth SHALL be confined behind a single transport interface that tests can substitute.

#### Scenario: Protocol tests run without a browser
- **WHEN** the test suite runs in continuous integration
- **THEN** the protocol tests execute without launching a browser and without any Bluetooth device present

#### Scenario: Protocol regression fails the build
- **WHEN** a change alters the encoding or decoding of a protocol message incompatibly
- **THEN** the test suite fails

#### Scenario: Transport is substitutable
- **WHEN** the app's session behaviour is exercised in tests
- **THEN** a substituted transport supplies the device's responses
- **AND** no test depends on `navigator.bluetooth` being available

### Requirement: Type Checking, Linting And Tests Gate The Build

Continuous integration SHALL run type checking, linting and the unit test suite for the Web Companion, and SHALL fail on any error in any of them. A deployment SHALL NOT publish output produced by a build that failed any of these gates.

#### Scenario: Type error blocks deployment
- **WHEN** the source contains a type error
- **THEN** the workflow fails
- **AND** no new output is published

#### Scenario: Failing test blocks deployment
- **WHEN** a unit test fails
- **THEN** the workflow fails
- **AND** the previously deployed site is left in place rather than replaced by a partial build

### Requirement: Development Runs Against A Secure Context

The toolchain SHALL provide a local development server that serves the app over a context in which the Web Bluetooth API is available, so that a developer can exercise a live device session without deploying.

#### Scenario: Web Bluetooth available in development
- **WHEN** a developer runs the development server and opens the app locally
- **THEN** the browser permits the app to request a Bluetooth device

### Requirement: The Rebuild Preserves Companion Behaviour

The rebuilt app SHALL satisfy every requirement of the `web-companion-app` capability. The rebuild SHALL NOT change user-facing behaviour beyond what those requirements state, and SHALL NOT change the BLE protocol or any device-side contract.

The rebuilt app SHALL be verified against a physical device across the flows that cannot be covered automatically: the first-time setup wizard including resume after reconnect, login, each user-management verb with real fingerprint scans, and an OTA transfer including a mid-transfer disconnect and resume. The results of that verification SHALL be recorded in the repository, per flow, stating what was exercised and what was observed.

Where the deployment is switched to the built output, or the legacy hand-written assets are removed, before that verification is complete, the change SHALL record which flows remain unverified and that the residual risk was accepted. The verification SHALL be completed before the change is archived, so that no unverified claim of parity is written into the project's specifications.

#### Scenario: Parity results are recorded per flow
- **WHEN** the hardware verification is performed
- **THEN** the outcome of each named flow is recorded in the repository
- **AND** a flow that failed is fixed and re-verified rather than recorded as passed

#### Scenario: Switching ahead of verification is recorded as a deviation
- **WHEN** the deployment is switched to the built output or the legacy assets are removed while hardware verification is still outstanding
- **THEN** the change records which flows remain unverified and that the residual risk was accepted
- **AND** the change is not archived until those flows have been verified and recorded

#### Scenario: Protocol unchanged
- **WHEN** the rebuilt app connects to a device running firmware that the legacy app supported
- **THEN** the session, authentication, user management and OTA transfer succeed without any firmware change

#### Scenario: Legacy assets are not left as a second copy
- **WHEN** deployment publishes the built output
- **THEN** the hand-written legacy script and page are removed from the repository
- **AND** they are not left as a second, unbuilt copy of the app

### Requirement: Deferred Code Loading Fails Visibly

Where the app defers part of its interface to a lazily loaded chunk, a failure to load that chunk SHALL be reported to the user as a failure, with a way to retry. The app SHALL NOT render an empty or partial view when a deferred chunk fails to load.

#### Scenario: A chunk that fails to load is reported
- **WHEN** a lazily loaded part of the interface cannot be fetched
- **THEN** the app states that the view could not be loaded
- **AND** it offers the user a way to retry rather than showing an empty pane

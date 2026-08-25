## MODIFIED Requirements

### Requirement: GitHub Actions Deployment Trigger
The system SHALL deploy the web-companion application to GitHub Pages whenever changes are pushed to its corresponding directory or workflow file. The workflow SHALL install the pinned toolchain, build the application, and upload the build's output as the Pages artifact. It SHALL NOT upload the source directory.

The workflow SHALL fail without publishing when the install, the build, or any of the build's quality gates fails, so that a broken build leaves the previously deployed site in place.

#### Scenario: Code changes pushed to web-companion directory
- **WHEN** a user pushes commits that modify files under `web-companion/**` to the `main` branch
- **THEN** the deployment workflow is triggered automatically
- **THEN** the workflow installs dependencies from the committed lockfile and builds the application
- **THEN** the build's output directory is uploaded as a Pages artifact and deployed

#### Scenario: Workflow file changes pushed
- **WHEN** a user pushes commits that modify `.github/workflows/deploy-web-companion.yml` to the `main` branch
- **THEN** the deployment workflow is triggered automatically

#### Scenario: Failing build is not published
- **WHEN** the install, the build, or a quality gate fails during the workflow
- **THEN** no Pages artifact is uploaded
- **AND** the previously deployed site remains in place

### Requirement: Manual Deployment Trigger
The system SHALL allow manual triggering of the web-companion deployment workflow.

#### Scenario: Manual trigger via workflow_dispatch
- **WHEN** a user triggers the workflow manually from the GitHub Actions tab
- **THEN** the deployment workflow is executed, builds the application, and deploys the build's output

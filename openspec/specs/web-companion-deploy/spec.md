# Web Companion Deployment

## Purpose

Defines GitHub Pages CI/CD workflow for deploying the web companion static application.

## Requirements

### Requirement: GitHub Actions Deployment Trigger
The system SHALL deploy the web-companion static application to GitHub Pages whenever changes are pushed to its corresponding directory or workflow file.

#### Scenario: Code changes pushed to web-companion directory
- **WHEN** a user pushes commits that modify files under `web-companion/**` to the `main` branch
- **THEN** the deployment workflow is triggered automatically
- **THEN** the `web-companion` directory is uploaded as a Pages artifact and deployed

#### Scenario: Workflow file changes pushed
- **WHEN** a user pushes commits that modify `.github/workflows/deploy-web-companion.yml` to the `main` branch
- **THEN** the deployment workflow is triggered automatically

### Requirement: Manual Deployment Trigger
The system SHALL allow manual triggering of the web-companion deployment workflow.

#### Scenario: Manual trigger via workflow_dispatch
- **WHEN** a user triggers the workflow manually from the GitHub Actions tab
- **THEN** the deployment workflow is executed and deploys the `web-companion` directory

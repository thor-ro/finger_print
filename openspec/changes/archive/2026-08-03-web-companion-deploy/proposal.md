## Why

Currently, the web companion is a local static application. We need to deploy it to GitHub Pages so that users can access it online without running a local server. This enables easier distribution and testing.

## What Changes

- Add a GitHub Actions workflow that automatically deploys the `web-companion` directory to GitHub Pages.
- Trigger deployment on pushes to the `web-companion` directory or workflow file.
- Add support for manual triggering (`workflow_dispatch`).

## Capabilities

### New Capabilities
- `web-companion-deploy`: GitHub Actions workflow to deploy static files to GitHub Pages.

### Modified Capabilities

## Impact

- No changes to application code.
- New file `.github/workflows/deploy-web-companion.yml` will be added.
- The repository will need appropriate Settings enabled to allow GitHub Actions to deploy to GitHub Pages.

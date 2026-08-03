## Context

The `web-companion` app is currently entirely static (HTML, CSS, JS) and runs locally. Deploying it to GitHub pages allows users to run it from a persistent URL.

## Goals / Non-Goals

**Goals:**
- Provide a simple GitHub Actions workflow to deploy the contents of `web-companion/` to GitHub Pages.
- Deploy automatically on changes to `web-companion/` or manually via `workflow_dispatch`.

**Non-Goals:**
- Implementing a build step (e.g. Webpack, Vite). The app is currently static and doesn't need compilation.
- Deploying a backend API.

## Decisions

- **GitHub Pages via actions/deploy-pages:** We will use the modern GitHub Actions integration for Pages (`actions/upload-pages-artifact` and `actions/deploy-pages`) instead of the legacy `gh-pages` branch deployment. This is cleaner and official.
- **Trigger Paths:** We will restrict automatic deployment triggers to paths strictly within `web-companion/` or the workflow file itself to avoid unnecessary runs when other parts of the repository change.
- **Artifact Path:** The deployment will upload the `./web-companion` directory as the static site artifact.

## Risks / Trade-offs

- **Risk:** The repository settings must allow Actions to deploy to Pages.
  **Mitigation:** Provide clear instructions to the repository owner to enable "GitHub Actions" as the source for GitHub Pages in the repository settings.
- **Trade-off:** Hardcoding the upload path to `web-companion`. If the app eventually adopts a build tool (like Vite), this workflow will need an update to upload a `dist/` directory instead of the source directory. This is acceptable for the current simplicity.

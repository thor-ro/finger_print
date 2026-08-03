## 1. Setup Deployment Workflow

- [x] 1.1 Create `.github/workflows/deploy-web-companion.yml` file
- [x] 1.2 Configure triggers (`push` to `main` with specific paths, and `workflow_dispatch`)
- [x] 1.3 Add build job to checkout repository and configure pages
- [x] 1.4 Add step to upload `web-companion` directory as an artifact
- [x] 1.5 Add deploy job using `actions/deploy-pages` with required permissions (`pages: write`, `id-token: write`)

## 2. Documentation and Instructions

- [x] 2.1 Update `web-companion/README.md` to mention the GitHub Pages deployment

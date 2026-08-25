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

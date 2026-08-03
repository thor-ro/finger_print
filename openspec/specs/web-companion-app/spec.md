# Spec: web-companion-app

## Overview
This specification covers the Web Companion App, a static web application that connects to the Smart Door Bridge via Web Bluetooth to allow users to register, manage device configuration, and trigger OTA firmware updates.

## Requirements

### Requirement: Web Bluetooth Connection
The Web App SHALL use the Web Bluetooth API (WebBLE) to scan for and connect to the Smart Door Bridge's Companion Service.

#### Scenario: Device Discovery
- **WHEN** user clicks "Connect" in the web app
- **THEN** browser prompts with Web Bluetooth device picker filtering for the Smart Door service UUID

### Requirement: Static App Location
The Web App SHALL be maintained as dependency-free static assets in the repository's `web-companion/` directory, suitable for GitHub Pages deployment.

#### Scenario: GitHub Pages deployment assets are present
- **WHEN** the repository is prepared for deployment
- **THEN** `web-companion/` contains the HTML, client-side assets, and deployment instructions needed to host the app

### Requirement: User Registration
The Web App SHALL provide a registration form that hashes the user's password locally (e.g., using SHA256) before sending it over BLE to the bridge.

#### Scenario: Secure Registration
- **WHEN** user submits the registration form
- **THEN** web app computes SHA256 hash of password
- **AND** transmits username and hash to the bridge
- **AND** prompts the user to "Please scan the Admin Finger on the device to confirm."

### Requirement: OTA Battery Warning
The Web App SHALL warn the user before triggering an OTA update about potential battery drain and SHALL send the selected HTTPS firmware URL together with Wi-Fi credentials in the OTA request.

#### Scenario: OTA Warning
- **WHEN** user selects an OTA update to install
- **THEN** app displays a warning: "Ensure your battery is above 20%. OTA uses Wi-Fi and draws significant power."
- **AND** app sends a bounded JSON request containing `ssid`, `password`, and `firmwareUrl` after user confirmation

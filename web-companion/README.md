# Smart Door Web Companion

This directory contains the static Web Companion app for the Smart Door Bridge.

## Features
- **Web Bluetooth (WebBLE)**: Connects directly to the ESP32-C6 over BLE.
- **Authentication**: Local SHA256 hashing for secure login and registration.
- **Device Dashboard**: Trigger HTTPS OTA updates natively via the browser.

## Deployment to GitHub Pages

Since this is a dependency-free static web application, it is automatically deployed to GitHub Pages via a GitHub Actions workflow (`.github/workflows/deploy-web-companion.yml`).

To enable this in your repository:
1. Go to your repository **Settings** on GitHub.
2. Under **Pages** > **Build and deployment** > **Source**, select **GitHub Actions**.
3. Push changes to the `web-companion/` directory, or trigger the workflow manually from the **Actions** tab.

## Browser Support
Requires a browser with Web Bluetooth support (e.g., Chrome, Edge, Chrome for Android). iOS Safari does not support Web Bluetooth natively, but specialized apps like WebBLE can be used.

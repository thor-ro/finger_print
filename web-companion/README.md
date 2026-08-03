# Smart Door Web Companion

This directory contains the static Web Companion app for the Smart Door Bridge.

## Features
- **Web Bluetooth (WebBLE)**: Connects directly to the ESP32-C6 over BLE.
- **Authentication**: Local SHA256 hashing for secure login and registration.
- **Device Dashboard**: Trigger HTTPS OTA updates natively via the browser.

## Deployment to GitHub Pages

Since this is a dependency-free static web application, it can be deployed directly to GitHub Pages:
1. Go to your repository settings on GitHub.
2. Under "Pages", select the `main` branch.
3. Select `/web-companion` as the source folder (or if using standard deployment, ensure your actions deploy this folder).
4. Save and wait for the deployment to complete.

## Browser Support
Requires a browser with Web Bluetooth support (e.g., Chrome, Edge, Chrome for Android). iOS Safari does not support Web Bluetooth natively, but specialized apps like WebBLE can be used.

# Smart Door Web Companion

The Web Companion app for the Smart Door Bridge: a SvelteKit single-page
application, compiled by Vite to prerendered static assets and served from
GitHub Pages — no origin server, no runtime backend, no third-party origins.

## Toolchain

Requirements:

- Node.js, version pinned in the repository root [`.nvmrc`](../.nvmrc)
  (use `nvm use` or any matching Node).

Commands, run from this directory:

```bash
npm ci          # install exactly the versions in package-lock.json
npm run dev     # dev server at http://localhost:5173 (secure context;
                # localhost may use Web Bluetooth without HTTPS)
npm run check   # svelte-check / TypeScript type checking
npm run lint    # {@html} ban, protocol-layer purity, BLE confinement
npm test        # Vitest suite, headless (protocol codecs + component tests)
npm run build   # prerendered static output into build/ (+ CSP hash pinning)
npm run budget  # compare the built initial load against budget.json
npm run gate    # all of the above gates, in CI order
```

`npm run build` emits `.nojekyll` (via `static/`) so GitHub Pages keeps the
`_app` directory, and pins the SHA-256 of SvelteKit's inline bootstrap script
into the CSP meta tag (`scripts/csp.mjs`). `build/` is not committed.

Production builds for GitHub Pages must carry the project-site subpath:

```bash
BASE_PATH=/finger_print npm run build
```

A fork with a different repository name changes this variable (in
`.github/workflows/deploy-web-companion.yml`), not the source.

## Architecture

```
src/lib/protocol/    PURE codecs: auth, ota, usermgmt, health, setup.
                     No DOM, no navigator, no SvelteKit imports — unit-tested
                     headlessly in CI.
src/lib/transport/   BleTransport interface; WebBluetoothTransport is the
                     only place navigator.bluetooth is referenced;
                     FakeTransport replays scripted device responses in tests.
src/lib/state/       session store: the single owner of connection, auth,
                     wizard, health, user-management and OTA state. The
                     visible pane is derived from it.
src/lib/components/  one component per view/section; all rendering goes
                     through Svelte's escaped text interpolation ({@html} is
                     banned by lint).
src/routes/          one prerendered route.
```

## Deployment to GitHub Pages

Automatic via `.github/workflows/deploy-web-companion.yml`: it installs from
the lockfile (`npm ci`), runs the gates (type check, lint, tests), builds,
checks the bundle budget, and publishes `web-companion/build`. A failed gate
publishes nothing — the previously deployed site stays up.

To enable it in your repository:
1. Repository **Settings** > **Pages** > **Build and deployment** >
   **Source**: **GitHub Actions** (the workflow also creates this itself via
   `enablement: true` on first run).
2. Push changes to `web-companion/`, or trigger the workflow manually from
   the **Actions** tab.

## Features

- **Web Bluetooth**: connects directly to the ESP32-C6 over BLE.
- **First-Time Setup Wizard**: mandatory, guided flow for claiming an
  unclaimed device (Admin enrolment → account registration → Nuki pairing →
  explicit finish), resuming at the device's reported setup step.
- **Challenge-response login**: PBKDF2-HMAC-SHA256 credential stretching
  client-side, HMAC response over the wire; the device never sees the
  password.
- **Admin-Bound Accounts**: a companion account is an attribute of a
  fingerprint user, not a standalone record. The name you submit at
  registration is your name **on the device** (and must be unique), and the
  account belongs to the admin whose fingerprint scan confirms it. Session
  authority is derived live from that admin's current permission: demoting or
  deleting the bound admin immediately removes the account's access.
- **Re-Registration = Password Reset**: registering again with an admin's
  scan replaces that admin's existing password in place — this is the
  supported way to reset a forgotten password. The app warns before
  submitting for exactly this reason.
- **Device Dashboard**: device health, config, enrollment, user management,
  Nuki re-pair, Zigbee join and OTA updates natively via the browser.

## Browser Support

Requires a browser with Web Bluetooth support (e.g., Chrome, Edge, Chrome for
Android). iOS Safari does not support Web Bluetooth natively, but specialized
apps like WebBLE can be used.

## Firmware Compatibility (OTA)

The OTA (Firmware Update) flow streams the selected `.bin` file directly over
the BLE GATT connection using an opcode-prefixed
BEGIN(`0x01`)/CHUNK(`0x02`)/END(`0x03`) chunked transfer. This is **not**
compatible with firmware built before the `replace-wifi-ota-with-ble-transfer`
change (archived at
`openspec/changes/archive/2026-08-07-replace-wifi-ota-with-ble-transfer/`),
which expected a `{ssid, password, firmwareUrl}` JSON request and joined Wi-Fi
to download the image over HTTPS.

**Firmware version floor:** devices must be running firmware built from
`replace-wifi-ota-with-ble-transfer` (2026-08-07) or later before this app
version is deployed against them. Deploying this app against older,
pre-BLE-OTA firmware will make OTA triggering fail (the device will reject
the new chunked-transfer opcodes it does not understand). Coordinate the web
app deploy (GitHub Pages) and the firmware release so devices only ever pair
against the app version that matches their OTA protocol.

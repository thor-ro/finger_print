## Why

The companion app is 1 586 lines of untested, hand-wired JavaScript in one file, and it is the only remote surface a claimed device has.

- **One file, one scope.** `web-companion/app.js` is 65 912 bytes of module-scope state — `bluetoothDevice`, `gattServer`, six characteristic handles, `setupCompleted`, `otaResumeState`, `otaPendingNotification`, the user-management request counter — all mutable globals that any of the 110 `getElementById` call sites can reach. There is no boundary between "speaks BLE" and "paints the screen."
- **The DOM is the state.** `switchView()` (`app.js:171`) toggles `style.display` across four hand-managed views in `index.html` (`connection-view`, `wizard-view`, `auth-view`, `dashboard-view`), and the wizard adds a second layer of step divs on top. Which pane is visible is derived from nothing; it is set imperatively from a dozen places and can disagree with the device's reported setup state.
- **Handlers are global names.** `window.umDelete`, `window.umPromptRename`, `window.umPromptPermission` (`app.js:1016`, `:1046`, `:1067`) exist because markup built by string concatenation has no other way to bind a click.
- **Escaping is manual, and it has already failed once.** `renderUmUsers()` interpolated a device-supplied user name straight into `innerHTML` *and* into an `onclick` attribute — a stored XSS reachable by anyone who can name a fingerprint user. It was fixed by hand this session with an `escapeHtml()` helper and event delegation. The helper now guards five `innerHTML` sites (`app.js:579`, `:586`, …) and nothing prevents the sixth from forgetting it.
- **Nothing is tested.** The firmware has 410 host tests. The companion has zero — not for the login challenge-response derivation, not for the OTA begin/chunk/end framing, not for the user-management reply mapping, not for health-report parsing. All of that logic is only reachable through a browser holding a live BLE connection to a physical device, so in practice it is verified by hand or not at all.
- **"Dependency-free" bought simplicity and is now paying for it.** No build step also means no type checking, no linting, no dead-code elimination, no dev server, and no way to split the protocol from the presentation without inventing a module system by hand.

Every one of these is a structural property of "one script tag, no build", not a bug anyone can fix in place.

## What Changes

- **Rebuild the companion as a SvelteKit application** in `web-companion/`, compiled by Vite to prerendered static assets. Svelte's compiler emits direct DOM updates rather than shipping a virtual-DOM runtime, which is what makes a framework affordable here.
- **Extract the BLE protocol into DOM-free modules** — auth challenge/response derivation, OTA opcode framing and resume, user-management request/reply codecs, health-report parsing — each unit-tested with Vitest in CI. The Web Bluetooth surface is confined to one adapter behind an interface the tests can substitute.
- **Make escaping structural.** Svelte escapes text interpolation by default; `{@html}` becomes the single audited escape hatch and is banned by lint. The class of bug that produced the user-list XSS stops being reachable.
- **Add gates: type check, lint, unit tests, and a declared bundle budget**, all failing the build rather than warning. The budget exists because a framework's cost has to be measured, not asserted.
- **Deploy the build output, not the source directory.** The Pages workflow gains an install-and-build step, publishes `web-companion/build`, and sets the project-site base path so assets resolve under `/finger_print/`.
- **No new user-facing features.** This is a re-platforming: every requirement in `web-companion-app` must hold afterwards, verified against a real device before this change is archived.

### Capabilities

- **New:** `web-companion-tooling`
- **Modified:** `web-companion-app`, `web-companion-deploy`

### Dependencies

None on the firmware side — the GATT contract is unchanged, and this change ships no firmware. It should land after `companion-device-health` and `companion-user-mgmt` are in the app (they are), so the rewrite has one stable target rather than a moving one.

### Non-goals

- Changing the BLE protocol, the GATT service, or any device behaviour.
- Adding offline caching, a service worker, or installability (PWA).
- Supporting browsers that lack Web Bluetooth. iOS Safari remains unsupported.
- Server-side rendering, an origin server, or any runtime backend. The artifact stays a static bundle.

### Accepted risks

- **A dependency tree where there was none.** The shipped output keeps zero runtime dependencies, but the build gains a lockfile and a supply chain. Mitigated by pinning through the committed lockfile, installing with `npm ci` in CI, and keeping every dependency a devDependency.
- **SvelteKit brings a router the app barely uses.** The companion is one page with modal-ish views. The router is paid for regardless; the budget check is there to keep that cost visible and bounded. See `design.md` for why SvelteKit is still preferred over bare Svelte + Vite.
- **A rewrite can silently drop behaviour.** The wizard's resume logic and the OTA resume/grace-period handling are the two places where that is most likely and least visible. Parity is a task-level checklist against the existing spec, verified on hardware and recorded per flow before the change is archived. Where deployment was switched ahead of that verification, the outstanding flows and the accepted risk are recorded rather than assumed away.

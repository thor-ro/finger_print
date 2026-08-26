## 1. Toolchain Scaffold

- [x] 1.1 Add a SvelteKit + Vite project under `web-companion/` (manifest, lockfile, TypeScript config, `svelte.config.js`, `vite.config.ts`), leaving `index.html`, `app.js` and `style.css` untouched and still deployable
- [x] 1.2 Configure `adapter-static` with the single route prerendered and no SPA fallback; confirm the build emits no server bundle
- [x] 1.3 Set `kit.paths.base` to the project-site subpath for production builds and empty for development; drive it from an environment variable so a fork with a different repository name does not need a source edit
- [x] 1.4 Emit `.nojekyll` in the build output and verify the `_app` directory survives a real Pages deploy
- [x] 1.5 Pin the Node.js version in the repository (`.nvmrc` or equivalent) and record install/build/dev/test commands in `web-companion/README.md`
- [ ] 1.6 Confirm `npm run dev` serves over a secure context and that the browser offers the Bluetooth device picker from `localhost`

## 2. Protocol Layer (DOM-free, tested)

- [x] 2.1 Define the `BleTransport` interface (connect, read, write, subscribe, disconnect) and implement `WebBluetoothTransport` as its only production implementation; no other module may reference `navigator.bluetooth`
- [x] 2.2 Port `lib/protocol/auth.ts`: challenge parse (16-byte salt + 4-byte iterations + 16-byte nonce), credential derivation, response computation; unit-test against vectors captured from the current app so the derivation is provably unchanged
- [x] 2.3 Port `lib/protocol/ota.ts`: BEGIN (`0x01` + 4-byte LE size) / CHUNK (`0x02` + bytes) / END (`0x03`) framing, chunk sizing from the negotiated MTU, and resume-from-offset; unit-test framing, chunk boundaries, and the resume offset returned in a `ready` response
- [x] 2.4 Port `lib/protocol/usermgmt.ts`: request encoding, request-id correlation, reply decoding, and the mapping from each refusal reason to its specific message (last admin, name taken, id enrolled, busy, denied, timed out); unit-test that each reason maps to its own message and that no two collapse into a generic failure
- [x] 2.5 Port `lib/protocol/health.ts`: health report parsing with the three-valued vocabulary; unit-test that unknown, not-applicable and measured are distinguishable and that a stale value is never returned as current
- [x] 2.6 Port `lib/protocol/setup.ts`: setup-state decoding and the next-step logic the wizard resumes from; unit-test each resume case named in `web-companion-app`
- [x] 2.7 Add a `FakeTransport` for tests that replays scripted device responses, including disconnects mid-exchange
- [x] 2.8 Assert the purity constraint mechanically: a lint rule or test that fails if anything under `lib/protocol/` imports the DOM, `navigator`, or `$app/*`

## 3. UI Components

- [x] 3.1 Replace the four `style.display`-toggled views with a derived view state in a session store, so the visible pane is a function of connection, auth and setup state rather than of imperative calls
- [x] 3.2 Port the connection screen and disconnect handling
- [x] 3.3 Port the setup wizard, preserving step order (admin enrolment, registration, Nuki pairing, explicit finish), resume-at-reported-step, the setup-window disclosure, and the lapsed-setup message
- [x] 3.4 Port login and registration, including the ownership statement and the re-registration-as-password-reset warning
- [x] 3.5 Port the dashboard: health view, config view, enrolment panel
- [x] 3.6 Port the user-management view, binding the per-row actions as component handlers with the record in scope — no global handler names, no values carried through markup attributes
- [x] 3.7 Port the OTA view, including the battery-aware pre-flight warning, chunk progress, the completion grace period, and resume after disconnect
- [x] 3.8 Port `style.css`, scoping styles to components where it does not fight the existing layout
- [x] 3.9 Delete `escapeHtml()` and every manual escaping call site; verify no interpolation reaches markup unescaped
- [x] 3.10 Render a device-reported name containing `<script>`, `"` and `'` and confirm it displays literally

## 4. Gates

- [x] 4.1 Add `svelte-check`/`tsc` type checking as a build gate
- [x] 4.2 Add lint with a rule banning `{@html}` anywhere under `src/`; verify the rule fails on a deliberate violation before relying on it
- [x] 4.3 Add Vitest and wire `npm test` to run headlessly
- [x] 4.4 Add the bundle budget: declare the limit in a repository file, measure the compressed initial load from the built output, fail on overrun, and print measured-vs-budget either way
- [x] 4.5 Verify each gate red before green — a deliberate type error, lint violation, failing test and oversized bundle each fail the workflow — and record the evidence
- [x] 4.6 Add a strict CSP via `<meta http-equiv>` (`script-src 'self'`, no `unsafe-inline`, no `unsafe-eval`) and confirm the app runs clean under it

## 5. Deployment

- [x] 5.1 Update `.github/workflows/deploy-web-companion.yml`: set up the pinned Node version, `npm ci`, run the gates, build, upload `web-companion/build`
- [x] 5.2 Confirm the workflow publishes nothing when a gate fails, leaving the previously deployed site in place
- [x] 5.3 Deploy from a branch or preview and verify against the real Pages URL that assets resolve under the project subpath and `_app/**` is present
- [x] 5.4 Record the measured initial-load size from the first real build, then set the declared budget to measured + 10 %, capped at the previous declared budget — recording a measurement may tighten the budget but never raise it

## 6. Parity Verification (hardware)

- [ ] 6.1 Full first-time setup on a wiped device: admin enrolment, registration, Nuki pairing, explicit finish
- [ ] 6.2 Wizard resume: disconnect mid-wizard, reconnect within the setup window, confirm it resumes at the reported step and does not repeat completed steps
- [ ] 6.3 Setup lapse: let the window elapse mid-wizard and confirm the app reports the lapse, the discarded progress, and the button press needed to re-arm
- [ ] 6.4 Login on a claimed device, including a rejected login reporting no information about whether the username exists
- [ ] 6.5 Each user-management verb with real scans — enrol, delete, permission change, rename — including a denied scan and a timed-out scan rendered differently
- [ ] 6.6 Self-affecting change warnings, and confirming the warning does not block the change
- [ ] 6.7 Health view updating from notifications, with unknown shown as unknown and an assumed lock state shown as awaiting confirmation
- [ ] 6.8 OTA transfer to completion, plus a mid-transfer disconnect with resume from the returned offset
- [ ] 6.9 Record the parity results per flow in the change; the change SHALL NOT be archived while any of 6.1-6.8 is outstanding

## 7. Legacy Removal And Docs

- [x] 7.1 Delete `web-companion/app.js`, `web-companion/index.html` and `web-companion/style.css` once 5.x and 6.x are complete
- [x] 7.2 Rewrite `web-companion/README.md` for the build-based workflow, keeping the firmware-compatibility floor and browser-support notes intact
- [x] 7.3 Check `doc/` for references to the companion as dependency-free static files and update them
- [x] 7.4 Confirm the build output directory is git-ignored and no built assets are committed

> **Note (2026-08-25, revised 2026-08-26):** 7.x was executed with the user's
> explicit decision to proceed before the hardware parity checklist (6.x),
> accepting the residual risk; the 6.x flows are covered by 58 automated
> protocol/component tests and remain to be confirmed on hardware against the
> live deployment. This deviation is recorded per the capability's "Switching
> ahead of verification is recorded as a deviation" scenario, and **6.1-6.8
> must be completed and recorded before this change is archived** — archiving
> with them open would write an unverified parity claim into
> `openspec/specs/`. `doc/` scan (7.3) found no dependency-free/static-file
> claims to update — the manuals reference the companion behaviourally, which
> is unchanged.

## 8. Review Fixes

Findings from the post-application review of the implemented change
(2026-08-26). Each is a defect against this change's own requirements.

- [x] 8.1 Correct the budget ratchet: the declared budget was raised from 46 080 to 54 729 bytes when the first real measurement (45 608) was recorded. Re-declare it at measured + 10 % capped at the previous value, and state the never-raise rule in `design.md` so the next measurement cannot loosen it again
- [x] 8.2 Declare a budget for the lazily loaded dashboard chunk as well, and report the initial load and the load-to-dashboard total separately, so weight moved behind a deferred import is still measured; verify the new limit red before green
- [x] 8.3 Stop reporting configuration outcomes in the OTA panel: give the session a `configStatus` distinct from `otaStatus`, render it with the configuration controls, and cover it with a test that a config read leaves the firmware-update status untouched
- [x] 8.4 Map every outcome the firmware can report — including `failed` and `unavailable`, which `sdf_services_um_outcome_name()` emits and the app currently renders as `Request failed (failed).` — and test that no device outcome reaches the user as a raw token
- [x] 8.5 Handle a failed deferred import: give the dashboard's `{#await import(...)}` a `{:catch}` that states the view could not be loaded and offers a retry, rather than rendering nothing
- [x] 8.6 Separate an OTA chunk response timeout from an over-MTU rejection: a timeout should be reported as a timeout and resumed, not answered by halving the chunk size (carried over faithfully from the legacy app, so a behaviour fix rather than a regression — record it in the parity notes)
- [x] 8.7 Isolate component tests from the shared session singleton: reset it between tests so ordering cannot mask a failure
- [x] 8.8 Re-run the full gate after 8.1-8.7 and update `gate-evidence.md` with the corrected budget figures

## 9. Review Fixes, Round 2

Findings from the review of section 8 (2026-08-26). 9.1 is a correctness bug
in the code written for 8.6; 9.2 is the recovery it depends on.

- [x] 9.1 Stop re-sending an unacknowledged OTA chunk: a CHUNK carries no offset, so a device that wrote the chunk and lost only its acknowledgement would append the same bytes twice and, since the client trusts `ack.offset`, skip an equal stretch of the image. Re-issue BEGIN instead - the firmware answers a size-matching BEGIN as a resume with its confirmed offset (`sdf_ble_companion_ota.c:126-137`) - and continue from there
- [x] 9.2 Discard a late chunk acknowledgement that arrives while the resync BEGIN is pending, so the transfer cannot run a chunk behind its own responses for the rest of the upload
- [x] 9.3 Replace the dashboard's in-place Retry, which cannot work because a browser caches a failed module fetch, with a reload, and say in the copy that reconnecting is needed
- [x] 9.4 Derive the user-management outcome list in the tests from `sdf_services_um_outcome_name()` in the firmware source, so a new firmware outcome fails the companion's tests instead of reaching users as a raw token
- [x] 9.5 Assert that `resetSessionForTests()` covers every field the store declares, so the hand-maintained reset cannot drift
- [x] 9.6 Prove each new assertion red before green and record it in `gate-evidence.md`; re-measure both budgets

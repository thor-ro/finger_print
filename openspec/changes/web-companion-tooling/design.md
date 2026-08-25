# Design: web-companion-tooling

## Context

The companion is a static page that talks to an ESP32-C6 over Web Bluetooth. It has no backend, no accounts of its own, no data at rest. Its entire job is: negotiate a GATT connection, run a challenge-response login, render device-reported state, and marshal four kinds of admin request that are authorized by a fingerprint scan happening at the device.

That shape matters for tooling choices. There is no server to render on, no SEO to serve, no route space to speak of, and the only untrusted input is what the device sends back over BLE.

## Goals

- Separate protocol from presentation so the protocol can be tested without a browser.
- Make escaping a property of the rendering layer rather than a habit of the author.
- Keep the shipped artifact static, self-hosted, and small enough that the framework's cost is justified and measured.
- Fail the build on type errors, lint violations, failing tests, and budget overruns.

## Non-goals

- Any change to the GATT contract or device behaviour.
- Automated end-to-end coverage of the BLE session (see "What stays manual").

## Decision: SvelteKit over bare Svelte, and over staying vanilla

The user's stated intent is SvelteKit for lightweight code. Weighing it against the alternatives honestly:

| Option | Ships at runtime | Escaping | Build/test story | Fit |
|---|---|---|---|---|
| Stay vanilla | ~21 KB gz, zero framework | manual `escapeHtml()` at every site | none | the status quo that produced the XSS and 0 tests |
| Svelte + Vite (no Kit) | smallest — compiler output only | automatic | Vite + Vitest, hand-rolled static output | leanest, but every static-hosting detail is manual |
| **SvelteKit + adapter-static** | compiler output + Kit client runtime/router | automatic | conventions for build, prerender, base path, testing | **chosen** |
| React/Preact | VDOM runtime always present | automatic | mature | heavier for no gain here |

SvelteKit's win is not features, it is that `adapter-static`, `prerender`, `paths.base` and the dev server solve exactly the four deployment problems this app has (static output, no server, a GitHub Pages subpath, and a secure-context dev environment) with configuration instead of scripts. The cost is a router that a single-page app does not need.

**The cost is bounded, not ignored:** the budget gate below exists specifically so that "SvelteKit is lightweight" is a measurement in CI rather than a claim in a proposal. If the router's overhead proves unacceptable against the budget, the escape route is dropping to bare Svelte + Vite — the component code and the protocol modules port unchanged, because neither imports from `$app/*`. Keeping that escape route open is a design constraint: **framework-specific imports stay in the route/layout files, not in components or protocol modules.**

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  routes/+page.svelte        one route, prerendered, csr     │
│    └── components/          Connection · Wizard · Auth      │
│                             Dashboard · Health · UserMgmt   │
│                             · Config · Ota                  │
├─────────────────────────────────────────────────────────────┤
│  lib/state/                 session store: connection,      │
│                             auth, setup step, health        │
├─────────────────────────────────────────────────────────────┤
│  lib/transport/             ┌──────────────────────────┐    │
│    BleTransport (interface) │ WebBluetoothTransport    │    │
│                             │  navigator.bluetooth     │    │
│                             ├──────────────────────────┤    │
│                             │ FakeTransport (tests)    │    │
│                             └──────────────────────────┘    │
├─────────────────────────────────────────────────────────────┤
│  lib/protocol/              PURE. no DOM, no navigator.     │
│    auth.ts       challenge parse · derive · response        │
│    ota.ts        BEGIN/CHUNK/END framing · resume offset    │
│    usermgmt.ts   request encode · reply decode · reasons    │
│    health.ts     health report parse · unknown / n-a        │
│    setup.ts      setup-state decode · next-step logic       │
└─────────────────────────────────────────────────────────────┘
                         ▲
                         │ Vitest exercises everything above
                         │ this line with no browser at all
```

The rule that makes the tests possible: **`lib/protocol/` may import nothing.** No `navigator`, no `document`, no `$app/*`. It takes `Uint8Array` and plain objects in, and returns `Uint8Array` and plain objects out. `crypto.subtle` is available in Node's test environment, so the login derivation is testable as-is.

`lib/transport/` is the only place `navigator.bluetooth` appears. Components never touch it directly; they call the session store, which calls the transport.

## Decision: escaping becomes structural, and `{@html}` is banned

Svelte escapes `{value}` interpolation. The only way to reintroduce the user-list XSS is `{@html}`, which the app has no reason to use — every value it renders is either a literal string or a device-reported value that must be shown as text.

So: a lint rule fails the build on `{@html}` anywhere in `src/`. Not a review convention, a gate. If a future need arises, it is a deliberate rule suppression with a comment, which is exactly the review signal that was missing before.

This also enables a strict CSP (`script-src 'self'`, no `'unsafe-inline'`, no `'unsafe-eval'`) because a compiled bundle evaluates no strings. Whether the CSP is enforced via `<meta http-equiv>` (GitHub Pages serves no custom headers) is a task-level detail; the meta form covers script execution, which is the part that matters here.

## Decision: bundle budget, with numbers

Measured baseline of the current app (gzip -9):

| Asset | raw | gzip |
|---|---|---|
| `index.html` | 14 129 | 3 257 |
| `app.js` | 65 912 | 16 304 |
| `style.css` | 6 015 | 1 524 |
| **total** | **86 056** | **~21 KB** |

A minimal SvelteKit client runtime plus router is on the order of 15 KB gz before any app code. Compiled Svelte components are typically smaller than the equivalent imperative DOM code, so the app's own share should come in under today's 16 KB.

**Initial budget: 45 KB gzip for the initial load (HTML + CSS + JS).** That is deliberately loose for the first landing — it exists to catch a dependency being added carelessly, not to make the first commit hard. A follow-up task tightens it to *measured + 20 %* once the real figure is known, so the budget ratchets down rather than drifting up.

The budget is checked in CI on the built output and fails the job. It is declared in a file in the repo, not buried in a workflow step, so the number is reviewable in a diff.

## Decision: GitHub Pages base path

The repository publishes to a **project** site (`thor-ro/finger_print`), so the app lives at `https://<owner>.github.io/finger_print/`, not at the origin root. Absolute asset URLs like `/_app/immutable/...` would 404.

`kit.paths.base` must therefore be set to the repository path for the deployed build, and left empty for local development. A `.nojekyll` marker must be emitted so Pages does not strip the `_app` directory — Jekyll ignores paths beginning with an underscore, which is precisely where SvelteKit puts every hashed asset.

```
  local dev            base = ''            http://localhost:5173/
  Pages deploy         base = '/finger_print'
                       https://thor-ro.github.io/finger_print/
                       + .nojekyll   <- without this, /_app/** is dropped
```

This is called out because both failure modes are silent-ish: a missing base path yields a blank page with 404s in the console, and a missing `.nojekyll` yields the same symptom from a completely different cause.

## Decision: rebuild in place, delete the legacy app last

The old and new apps cannot run side by side in one page, and there is no partial-migration path — the entire app is one script.

```
  step 1   add src/, config, tooling      legacy app.js still deployed
  step 2   port protocol modules + tests  legacy app.js still deployed
  step 3   port UI components             legacy app.js still deployed
  step 4   parity check on hardware       <- gate
  step 5   switch workflow to build/      legacy files deleted
```

Steps 1-3 add files without touching `index.html` or `app.js`, so `main` stays deployable throughout: the Pages workflow keeps publishing the directory as-is until step 5 flips it to publish `build/`. Only step 5 is a one-way door, and it is behind a hardware parity check.

The rollback for step 5 is reverting one commit, which restores both the workflow and the legacy assets together.

## What stays manual

Web Bluetooth cannot be driven from headless CI: there is no automatable device picker, and the flows that matter (fingerprint scans, the setup window, an OTA that reboots the device) are physical. So the test pyramid here is deliberately lopsided —

- **Automated:** protocol codecs, state transitions, reply-to-message mapping, component rendering with a fake transport.
- **Manual, on hardware, before step 5:** the full wizard on a wiped device, login, user management with real scans, an OTA transfer including a mid-transfer disconnect and resume.

Pretending otherwise would be the failure mode of this change. The parity checklist in `tasks.md` is the artifact that makes the manual half accountable.

## Risks

| Risk | Mitigation |
|---|---|
| Kit's runtime blows the budget | budget gate is in CI from the first build; bare Svelte + Vite is a supported fallback, kept viable by the no-`$app`-in-components rule |
| Supply chain enters a zero-dependency project | all deps are devDependencies; `npm ci` from a committed lockfile; shipped output makes no third-party requests |
| Rewrite drops wizard-resume or OTA-resume behaviour | those two are named explicitly in the parity checklist and covered by protocol unit tests |
| Base path / Jekyll breakage discovered only after merge | verified on a branch or preview deploy before step 5 |

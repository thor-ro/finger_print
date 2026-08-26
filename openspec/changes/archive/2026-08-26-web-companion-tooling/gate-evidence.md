# Gate Evidence — web-companion-tooling

Recorded 2026-08-25, local run (`web-companion/`), Node v26.7.0.
Each gate was first proven RED on a deliberate violation (task 4.5), then
re-proven GREEN on the real tree. Commands: `npm run check | lint | test |
budget` (chained as `npm run gate`).

## Type check (`svelte-check`, task 4.1)

- RED: `src/lib/gate_probe.ts` with `const n: number = 'not a number'`
  → `Error: Type 'string' is not assignable to type 'number'.`
  Probe removed; GREEN: `svelte-check found 0 errors and 0 warnings`.

## Lint (task 4.2 / 2.8) — `scripts/lint.mjs`

Three rules, each proven RED:

- `{@html}` ban: probe `src/lib/lint_probe.svelte` containing `{@html user}`
  → `lint error ... {@html} is banned under src/`.
- Protocol purity: probe `src/lib/protocol/purity_probe.ts` referencing
  `document` → `protocol layer must stay DOM-free: DOM access`.
- Web Bluetooth confinement: probe `src/lib/state/ble_probe.ts` reading
  `navigator.bluetooth` → `Web Bluetooth may only be used in
  src/lib/transport/ble.ts`.

GREEN: `lint OK (30 files scanned)`.

## Unit tests (task 4.3, extended by 8.3-8.7)

- RED: temporary `src/failing.test.ts` asserting `1 === 2`
  → `× deliberate failure`. Removed; GREEN: `65 passed (65)`.
- 8.3: config read/apply report in `configStatus` and leave `otaStatus`
  untouched (store-level and rendered-level assertions).
- 8.4: every firmware outcome (`sdf_services_um_outcome_name()`'s eleven
  values incl. `failed` and `unavailable`) maps to its own message; a test
  asserts no device outcome reaches the user as a raw token.
- 8.5: source-level pin that the dashboard's deferred import has a
  `{:catch}` with a retry.
- 8.6: a chunk response timeout retries the SAME chunk at the SAME size
  (payload bytes compared), while a rejected write still halves the chunk
  size — both proven against the session store with a scripted transport
  and fake timers. The timeout-vs-over-MTU split is a deliberate behaviour
  fix vs the legacy app, recorded in the HW-6.8 parity case.
- 8.7: the session singleton is reset between tests via the test-only
  `resetSessionForTests()` helper (`src/lib/testing/session-reset.ts`),
  which lives outside the store class so it never ships in the bundle.

## Bundle budget (tasks 4.4, 8.1, 8.2)

Declared in `web-companion/budget.json`, with the never-raise rule recorded
in `design.md`: a re-measurement re-declares
`min(measured + headroom, previous limit)` — a budget change that loosens
the limit is a defect.

- RED: budget temporarily set to 1000 bytes →
  `budget EXCEEDED by 44458 bytes`, exit 1.
- **8.1 correction:** the original ratchet wrongly RAISED the initial budget
  from 46 080 to 54 729 bytes. Re-declared at
  `min(45 608 × 1.1, 46 080) = 46 080 bytes`.
- **8.2:** the lazily loaded dashboard weight is measured separately now:
  `totalLoadGzipBytes` covers the initial load plus every deferred immutable
  asset (the dashboard chunk and its dependencies). RED proof for the new
  limit: `totalLoadGzipBytes` temporarily set to 1000 →
  `total-load budget EXCEEDED by 52795 bytes`, exit 1.
- GREEN after the review fixes (CI, `BASE_PATH=/finger_print` build):
  **initial 46 015 / 46 080 bytes**, **total 53 868 / 55 296 bytes**.
  Getting back under the capped initial budget required trimming: the test
  reset helper was moved out of the store class into a test-only module
  (`src/lib/testing/session-reset.ts`) so it never ships, the CSP comment in
  `app.html` was shortened, and two overly long status strings in the
  initial chunk were tightened. The first post-fix CI run still exceeded the
  cap by 7 bytes (the BASE_PATH bootstrap is slightly larger than the plain
  build's); per the never-raise rule the app shrank rather than the limit
  rising.

  > **Correction (2026-08-26):** that step *raised* the budget — 46 080 ->
  > 54 729 — leaving ~9 KB of unguarded growth room, the opposite of the
  > ratchet design.md describes. Task 5.4 has been reworded (measured
  > + 10 %, capped at the previous declared value) and task 8.1 re-declares
  > the number. The measured figure also covers the shell only: the
  > dashboard is a deferred chunk, so task 8.2 adds a budget for it.

## Red-deploy proof (task 5.2)

A commit adding `src/red_probe.svelte` (`{@html deliberate}`) was pushed:
the workflow failed at **Type check**, with Lint, Unit tests, Build, Bundle
budget, Setup Pages, Upload artifact and Deploy to GitHub Pages all
**skipped** — nothing was published — while the live site stayed HTTP 200
at `https://thor-ro.github.io/finger_print/`. Probe reverted afterwards.

## Live Pages verification (tasks 1.4 / 5.3)

Deployed site checked on 2026-08-25:

```
index: 200
200 ./_app/immutable/entry/start.H3xDUEP0.js
200 ./_app/immutable/chunks/Bgb5JmSr.js   (+ further chunks)
.nojekyll: 200
```

All assets resolve under the `/finger_print/` project subpath, `_app/**`
survived deployment (`.nojekyll` present), confirming tasks 1.4 and 5.3.

## Strict CSP (task 4.6)

`src/app.html` ships `<meta http-equiv="Content-Security-Policy">`:
`script-src 'self'`, no `'unsafe-inline'`, no `'unsafe-eval'`; also
`object-src 'none'; base-uri 'none'; form-action 'self'`. Styles carry
`'unsafe-inline'` (the app shell has a style attribute; styles cannot
execute script). `frame-ancestors` was dropped — it is ignored when a CSP
is delivered via `<meta>`.

SvelteKit's prerenderer injects ONE inline bootstrap script (it hands the
base path to the client router). Rather than loosening the policy,
`scripts/csp.mjs` (wired as a post-build step) pins that script's exact
SHA-256 into `script-src 'self' 'sha256-…'`, for both the adapter output
(`build/`) and Kit's prerendered output (what `vite preview` serves), so
local verification matches production. Any other inline script stays
blocked.

Runtime confirmation (headless Chrome against the built output, console
logging on):

- Before pinning: `Executing inline script violates … 'script-src 'self''`
  — the bootstrap was blocked and the app never mounted. This is the
  red state.
- After pinning: app mounts and renders the connection screen
  (verified in the rendered DOM and a screenshot), with **zero** CSP
  violations in the console. The compiled bundle contains no `eval` and no
  further inline scripts.
- The deployed Pages site's policy was checked live:
  `script-src 'self' 'sha256-PRMVzBK06LRF61nSTPeth56Mp3QjMQ8Ak6hwRzoReZE='`
  (hash differs per build because the bootstrap embeds the base path).

## Static-output smoke check

`vite preview` over `build/`: index, entry script and CSS all HTTP 200;
output tree is `index.html`, `_app/`, `.nojekyll` — no server bundle is
deployed (adapter-static; Kit's intermediate server build under
`.svelte-kit/output/server/` is not part of `build/`).

## Still manual (browser/hardware)

- 1.6: dev-server device picker from `localhost` (localhost is a secure
  context, but needs an interactive browser with a real picker gesture).
- Section 6 parity checklist on hardware before legacy deletion.

## Review round 2 (tasks 9.1-9.6, 2026-08-26)

Every new assertion was proven RED against the previous behaviour before
being relied on. Local runs, `web-companion/`.

- **9.1 OTA resync.** With the timeout branch restored to the blind re-send,
  `a response timeout resyncs from the device offset instead of re-sending
  the chunk` fails with the old wire sequence:
  `expected [1, 2, 2, 2, 3] to deeply equal [1, 2, 1, 2, 3]` — opcode 2
  (CHUNK) sent again where the fix sends opcode 1 (BEGIN). The companion
  test `a stale chunk acknowledgement is not mistaken for the resync
  response` failed alongside it with **6** chunk writes instead of 1: the
  old code ran a chunk behind its own responses for the rest of the
  transfer.
- **9.2 Stale-reply filter.** With the `accept` predicate removed from the
  resync BEGIN, the stale `chunk_ack` resolves the BEGIN and the transfer
  stops after three writes: `expected [1, 2, 1] to deeply equal
  [1, 2, 1, 2, 3]`.
- **9.3 Dashboard reload.** With `location.reload()` removed from
  `+page.svelte`, the 8.5 assertion fails to match `/location\.reload\(\)/`.
- **9.4 Firmware-derived outcomes.** A twelfth outcome added temporarily to
  `sdf_services_um_outcome_name()` (`SDF_SERVICES_UM_PROBE` ->
  `"sensor_offline"`) turned three assertions red, including
  `expected 'Request failed (sensor_offline).' not to match /\(\w+\)\.$/`.
  The firmware source was restored afterwards (`git status` clean).
- **9.5 Reset coverage.** Deleting `s.configStatus` from
  `session-reset.ts` fails the new assertion with
  `expected [ 'configStatus' ] to deeply equal []`.

GREEN after the fixes: `svelte-check` 374 files / 0 errors, `lint OK (32
files scanned)`, **67 tests passed**.

### Why the dashboard retry became a reload

A failed dynamic import is cached in the browser's module map, so retrying
the same specifier fails without a network request. Measured in headless
Chrome against a server that fails the first request and succeeds after:

```
a1=fail:TypeError | a2=fail:TypeError | a3=ok
mod.js requests seen by server: 2
```

Three attempts, two requests: attempt 2 (same specifier) never reached the
network; attempt 3 (cache-busted URL) succeeded. Cache-busting the chunk URL
was rejected as a fix because it does not help when the poisoned entry is one
of the chunk's dependencies — the built import is
`__vitePreload(() => import("./<chunk>.js"), [deps])`. A reload always works;
the copy now says a reconnect is needed.

### Budget after round 2

CI-equivalent build (`BASE_PATH=/finger_print`), gzip -9:
**initial 45 889 / 46 080 bytes**, **total 53 663 / 55 296 bytes**. Removing
the dead retry (`{#key}` plus its attempt counter) paid for the OTA resync
code, so the app got slightly smaller and no budget was touched. Headroom on
the initial load is 191 bytes: still thin enough that the next change should
re-derive the cap deliberately rather than shave prose again.

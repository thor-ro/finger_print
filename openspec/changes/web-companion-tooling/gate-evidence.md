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

## Unit tests (task 4.3)

- RED: temporary `src/failing.test.ts` asserting `1 === 2`
  → `× deliberate failure`. Removed; GREEN: `58 passed (58)`.

## Bundle budget (tasks 4.4)

Declared in `web-companion/budget.json`: **45 KB gzip** initial load
(`initialLoadGzipBytes: 46080`), per design.md's initial landing budget.

- RED: budget temporarily set to 1000 bytes →
  `budget: 1.0 KB   measured: 44.4 KB (45458 bytes)` /
  `budget EXCEEDED by 44458 bytes`, exit 1.
- GREEN measured from the built output (`build/index.html` + every
  script/stylesheet it references, gzip -9): **44.4 KB** locally — passes with
  the dashboard view split into a lazily-loaded chunk.
- First real CI build (run 32862110964, `BASE_PATH=/finger_print`) measured
  **45 608 bytes**; per task 5.4 the declared budget was tightened to
  **54 729 bytes (measured + 20 %)**.

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

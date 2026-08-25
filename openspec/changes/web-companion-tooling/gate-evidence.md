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
  script/stylesheet it references, gzip -9): **44.4 KB** — passes with the
  dashboard view split into a lazily-loaded chunk.
- Task 5.4 will tighten the declared budget to measured + 20 % after the
  first CI build confirms the figure.

## Strict CSP (task 4.6)

`src/app.html` ships `<meta http-equiv="Content-Security-Policy">`:
`script-src 'self'`, no `'unsafe-inline'`, no `'unsafe-eval'`; also
`object-src 'none'; base-uri 'none'; frame-ancestors 'none'`.

SvelteKit's prerenderer injects ONE inline bootstrap script (it hands the
base path to the client router). Rather than loosening the policy,
`scripts/csp.mjs` (wired as a post-build step) pins that script's exact
SHA-256 into `script-src 'self' 'sha256-…'`. Verified on the current build:

```
script-src 'self' 'sha256-40/5VHCLFtwSb7+X9GNbZ88ERrk73xZcEgo951hCp6g='
```

and recomputed independently — hashes match. Any other inline script stays
blocked. The compiled bundle contains no `eval` and no further inline
scripts (checked against `build/index.html`).

## Static-output smoke check

`vite preview` over `build/`: index, entry script and CSS all HTTP 200;
output tree is `index.html`, `_app/`, `.nojekyll` — no server bundle is
deployed (adapter-static; Kit's intermediate server build under
`.svelte-kit/output/server/` is not part of `build/`).

## Still manual (browser/hardware)

- 1.6: dev-server device picker from `localhost` (localhost is a secure
  context, but needs an interactive browser).
- 4.6 runtime confirmation: app exercised in a real browser under the CSP.
- 5.2 / 5.3: workflow red-deploy behaviour and real Pages subpath +
  `_app/**` verification.
- Section 6 parity checklist on hardware before legacy deletion.

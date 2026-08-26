# Design: web-companion-theming

## Context

The companion renders device state, and several of its requirements are *about* distinguishing states from one another: measured versus unknown versus not-applicable (`companion-device-health`), a lock state confirmed by the lock versus one assumed from a command, a specific refusal reason versus a generic failure (`companion-user-mgmt`). Appearance is therefore not decoration here — it is part of what the app is specified to communicate.

The starting point after `web-companion-tooling`: 11 tokens in `src/app.css`, five colour literals past them, all type from a system font stack, no third-party origins, a strict CSP with hash-pinned inline scripts, and a 53.4 KB bundle budget with 44.5 KB measured.

## Decision: a theme is a token set, not a stylesheet

```
  ┌─ A: theme = stylesheet ─────────────────┐   ┌─ B: theme = tokens ────────────┐
  │  .dashboard-section { ... }             │   │  :root[data-theme='x'] {       │
  │  .primary-btn { ... }                   │   │    --surface: ...;             │
  │  .status-msg { ... }                    │   │    --text: ...;                │
  │                                         │   │  }                             │
  │  contract = every class name in the app │   │  contract = the token names    │
  │  rename a class -> every theme breaks   │   │  rename a class -> themes fine │
  │  theme can set display:none on a control│   │  theme cannot select anything  │
  └─────────────────────────────────────────┘   └────────────────────────────────┘
                                                            chosen
```

Under B the contract is a list of names that fits on one screen and can be checked mechanically. Under A the contract is the entire DOM, and no check is possible.

The consequence for the axolotl file: its `:root` block survives (renamed off the `--axo-` prefix and extended); `.axo-nav`, `.axo-hero`, `.axo-glass-card`, `.axo-btn-primary` and the `@media (max-width: 900px)` block do not. Where those rules express something the companion wants — pill-radius buttons, glass surfaces, the gradient background — the *values* become tokens and the *rules* move into the app's own components, written once.

## The token layers

```
  ┌──────────────────────────────────────────────────────────────┐
  │ theme file        palette values only, one :root[data-theme] │
  │                   --surface  --text  --accent  --danger …    │
  └───────────────────────────┬──────────────────────────────────┘
                              │  consumed by
  ┌───────────────────────────▼──────────────────────────────────┐
  │ app.css           element defaults: body, input, button,     │
  │                   table, .status-msg                          │
  ├──────────────────────────────────────────────────────────────┤
  │ components        scoped <style>, tokens only, no literals   │
  └──────────────────────────────────────────────────────────────┘
```

Proposed contract (names indicative, the point is the coverage):

| Group | Tokens | Why it is in the contract |
|---|---|---|
| Surfaces | `--bg`, `--bg-image`, `--surface`, `--surface-raised`, `--surface-blur` | axolotl's background is a gradient, so the backdrop needs its own token — a gradient is not a valid `border-color` |
| Lines | `--border`, `--shadow` | |
| Text | `--text`, `--text-muted`, `--text-on-accent` | `#04121f` and `#fff` are hard-coded today, and both are text-on-accent |
| Accent | `--accent`, `--accent-strong`, `--accent-gradient` | axolotl's buttons are gradients; the dark theme's are flat — one token, two kinds of value |
| Status | `--ok`, `--warn`, `--danger`, `--info` + a surface tint per status | `rgba(245,158,11,0.12)` is a hard-coded warn tint today |
| Device vocabulary | `--unknown`, `--not-applicable`, `--assumed` | the three conditions the health view must not collapse |
| Shape | `--radius`, `--radius-lg`, `--radius-pill` | axolotl already tokenises these |
| Type | `--font-body`, `--font-weight-strong` | keeps the font stack a theme concern without letting a theme fetch one |

## Decision: state distinctions never rely on colour alone

The app already renders the health vocabulary as text — `Unknown`, `N/A`, `(awaiting confirmation)` (`health.ts:57-79`) — and refusals as distinct sentences (`usermgmt.ts:64-74`). That is what makes the requirements robust today, and theming must not quietly become the mechanism instead:

```
  measured        47%
  unknown         Unknown              ← text carries the distinction
  not applicable  N/A                    colour only reinforces it
  assumed         Locked (awaiting confirmation)
```

So the tokens `--unknown` / `--not-applicable` / `--assumed` exist to *reinforce* a distinction that survives without them — never to become the only signal. A theme that made all three the same colour would be ugly, not incorrect; a theme that removed the text would be neither, because it cannot: a theme cannot select an element.

## Decision: contrast is checked, not eyeballed

A light theme against a dark-built app is where contrast regressions live. CI computes WCAG contrast for each declared text-token/surface-token pair in every shipped theme, failing below 4.5:1 for body text and 3:1 for large text and UI borders.

Two subtleties the check has to respect, or it will be theatre:

1. **Translucent surfaces.** `--surface: rgba(255,255,255,0.65)` over `--bg-image` is not a colour. The check composites the surface over the background's dominant stop before measuring; where a theme's background is a gradient, it measures against the worst stop, not the average.
2. **Gradient accents.** `--accent-gradient` has two ends; `--text-on-accent` must clear the bar against both.

## Decision: pre-paint theme application rides the existing CSP step

Applying the stored choice after hydration means a visible flash of the default theme on every load. The cure is a tiny script in `app.html` that reads `localStorage` and sets `data-theme` on `<html>` before the body renders — an inline script, which `script-src 'self'` blocks.

This is already solved: `scripts/csp.mjs` hashes **every** src-less `<script>` in the built HTML and pins each hash into `script-src`. Adding one more inline script needs no policy change and no new mechanism — the hash appears at build time.

```
  app.html  <script>theme init</script>   +  SvelteKit bootstrap
                        │
              vite build │
                        ▼
  scripts/csp.mjs  →  script-src 'self' 'sha256-…theme' 'sha256-…kit'
```

The fallback if that ever fails is `@media (prefers-color-scheme)` in CSS, which needs no script but cannot honour an explicit choice that differs from the system preference.

## Decision: which theme is the default

Both shipped themes have a colour scheme, and they are opposites: the current palette is dark, axolotl is light. So the default is not a preference to be argued about — it is the system's:

```
  first run, prefers-color-scheme: dark    →  dark (today's appearance)
  first run, prefers-color-scheme: light   →  axolotl
  explicit choice                          →  that choice, persisted
```

This keeps the deployed appearance for anyone whose system is dark, gives the axolotl palette a reason to exist beyond a menu entry, and leaves the picker as an override rather than a required first step. **Open call for the user:** if axolotl is meant to be the product's identity rather than one of two options, it should be the default in both cases and the dark palette becomes the alternative.

## Decision: fonts stay first-party or system

Axolotl asks for `'Poppins', 'Nunito'`. Fetching either from a font CDN would reintroduce the third-party origin that `web-companion-tooling` removed (the legacy `index.html:8` `fonts.googleapis.com` link).

`--font-body` therefore holds a stack, and a theme may only name families that are either self-hosted in `static/` or present on the system. Self-hosting a weight-subset of one family is allowed but counts against the bundle budget — a decision to take with the measured number in hand, not up front. Until then axolotl falls back to the system stack, which changes its feel and should be said out loud rather than discovered.

## Decision: an unmeasured pair is a failure, not a pass

The contrast gate's first implementation skipped any pair it could not parse and printed `contrast OK` for the theme. Two real values fell through that hole and shipped: `--text-on-accent` on axolotl's `--accent-gradient` (a gradient, so it was skipped — measured, it is **1.00:1**, white text on a near-white stop of the primary button) and an `hsl()` panel behind near-white text (`hsl()` was unparseable, so it too was skipped).

```
  value the gate cannot read
            │
    ┌───────┴────────┐
    │                │
  skip  ───────▶  "contrast OK"        ← what shipped: silence reads as success
    │
  fail  ───────▶  "cannot read --panel: hsl(…)"   ← chosen
```

A gate whose failure mode is silence is worse than no gate, because it is *believed*. So: every colour token is resolved eagerly, an unreadable value is a failure naming the token and its value, gradients are measured at **every** stop rather than at a guessed worst one, and each theme reports its own result — the previous global failure flag labelled every theme printed after the first failure as failing, untouched ones included.

The corollary is a syntax restriction, recorded in `themes/CONTRACT.md`: colour tokens are hex, `rgb()`/`rgba()` or `hsl()`/`hsla()`. `color-mix()` and `oklch()` are not banned because they are bad, but because measuring them means implementing their colour spaces; until someone needs one, failing is honest and cheap.

## Decision: the contract is exact in both directions

Completeness ran one way — every contract token must be declared. Nothing stopped the contract from carrying tokens no component consumed, and it did: `--accent-strong`, `--info` and `--ok-tint` were declared by both themes, gated for contrast, documented, and read by nothing. Unused tokens are the mechanism by which a token contract becomes decoration: they cost every theme a line and every author a decision, and no gate can tell a wrong value from a right one when nothing renders it.

Two of the three were mandated by this change's own spec ("the accent and its emphasis", "the status colours for success, warning, danger and information"), so they get consumers rather than deletion:

| Token | Now consumed by | Gated at |
|---|---|---|
| `--accent-strong` | the `:focus-visible` ring in `app.css` | 3:1 as a non-text UI edge, against panel, dashboard section and page background |
| `--info` | the informational callout's border (`.register-note`), matching how `.alert.warning` and `.refusal` already read | 3:1 as a UI edge |
| `--ok-tint` | nothing — no success callout exists | removed from the contract |

Giving `--accent-strong` a consumer immediately earned its keep: axolotl's value (`#0ea5e9`, identical to `--accent`) measures **2.04:1** against the pastel background, below the 3:1 an outline needs. It is now `#0284c7`. That failure existed before this change; it was simply unmeasurable while the token was decorative.

And the check now runs the other way too: a theme declaring a token outside the contract fails. Otherwise this exact drift regrows.

## Decision: the budget rises once, deliberately

`scripts/budget.mjs` says a measurement may never raise a limit — re-measuring may only re-declare `min(measured + headroom, previous limit)` — precisely so that "the number went up, so the budget goes up" cannot happen quietly. This change breaks the previous limit and therefore owes an argument rather than a re-measurement:

```
                    before        measured now      declared now
  initial load      46,080         48,225            49,255   ← raise
  total load        55,296         55,483            56,641   ← raise
```

What the 2,365 B (initial, gzip) bought:

| Cost | Bytes (approx, gzip) | What it is |
|---|---|---|
| Both token sets in the entry CSS | +0.7 KB | the second theme's values; unavoidable once two themes ship |
| Pre-paint script + its pinned CSP hash in `index.html` | +0.4 KB | the alternative is a visible flash of the wrong theme on every load |
| Header theme picker | +1.0 KB | the spec requires the theme to be selectable, and before authentication |

What was weighed against it:

- **Ship one theme.** Cheapest, and refuses the change outright — the palette was supplied to be used.
- **Media-query-only theming, no picker.** Saves the picker and the pre-paint script (~1.4 KB) but cannot honour a choice that differs from the system preference, which the spec requires.
- **A `<select>` or an `{#each}` picker.** Nicer source, ~2.5 KB *worse*: it drags Svelte's selection and each-block runtime into the initial bundle. The picker is deliberately plain buttons for this reason.
- **Self-hosted Poppins/Nunito.** ~1.5 KB more for typeface identity; declined (see the fonts decision above) — that is the kind of spend the budget exists to refuse, and refusing it is what makes this raise credible.

Headroom stays at the usual 1 KB, so the raise is bounded by the measurement rather than rounded up to a comfortable number. The next change to touch this budget faces the same rule from the new floor: measurement alone still cannot move it.

## Decision: the Enroll-Admin dashboard section goes away

The dashboard's "Enroll Admin" section was removed inside this change (`AdminActionsSection.svelte`), which is not theming. It is recorded here because it shipped: the reasoning lived only in a code comment where the section used to be, and no task or spec delta covered it.

What it did and what replaces it:

```
  removed:  "Request Enroll Admin" button
            → Config characteristic {"action":"enroll_admin"}
            → sdf_services_request_admin_action(ENROLL_ADMIN)
            → admin scans on the device; the DEVICE picks the user id

  remains:  Enroll Fingerprint panel, Permission = Admin
            → UM characteristic, ENROLL verb (sdf_app.c:1236-1260)
            → sdf_services_request_remote_enrollment(user_id, permission)
            → terminal reply withheld until the authorizing admin scan
              (s_um_pending_gate); the connection must already hold admin
              authority (sdf_ble_companion.c:797)
```

Verified rather than assumed: the surviving path accepts permission 3 (admin) and is gated by the same authorizing-scan requirement, so removing the button removes no capability and weakens no authorization. The security property the old section's prose stated — "an existing Admin must scan their fingerprint; a BLE request alone is never enough" — is enforced by the firmware, not by the removed UI.

One difference is real and was unrecorded: `enroll_admin` let the device choose the user id, while the Enroll panel makes the operator pick a slot (1-10). Picking an occupied slot is refused specifically (`SDF_SERVICES_UM_ID_OCCUPIED`) and that refusal is already rendered as its own message, so the difference costs a retry, not a failure — but it is a change in who chooses, and it belonged in a task.

## Risks

| Risk | Mitigation |
|---|---|
| Light theme exposes contrast failures | contrast gate in CI; expect and fix real failures rather than raising the threshold |
| Translucent surfaces measured against the wrong background | composite over the worst gradient stop, not the token |
| Theme files drift into stylesheets | purity check rejects any selector or layout property in a theme file |
| Bundle budget consumed by themes | budget gate unchanged; themes measured as part of the initial load |
| Appearance change confuses hardware parity results | run `web-companion-tooling` 6.1-6.9 before this lands — **not honoured**: this change landed with those tasks still open (and `web-companion-tooling` 1.6 with them), so the first hardware parity run will exercise a re-themed app and will have to separate appearance regressions from behavioural ones itself. Neither change can be archived until they run. |

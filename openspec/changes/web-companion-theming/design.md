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

## Risks

| Risk | Mitigation |
|---|---|
| Light theme exposes contrast failures | contrast gate in CI; expect and fix real failures rather than raising the threshold |
| Translucent surfaces measured against the wrong background | composite over the worst gradient stop, not the token |
| Theme files drift into stylesheets | purity check rejects any selector or layout property in a theme file |
| Bundle budget consumed by themes | budget gate unchanged; themes measured as part of the initial load |
| Appearance change confuses hardware parity results | run `web-companion-tooling` 6.1-6.9 before this lands |

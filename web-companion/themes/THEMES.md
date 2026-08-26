# Shipped Themes

Two themes ship, both declaring the full contract (`themes/CONTRACT.md`):

| Theme | File | Origin |
|---|---|---|
| `dark` | `dark.css` | The companion's original dark palette, tokenised. |
| `axolotl` | `axolotl.css` | The provided palette (the former `themes/axolotl-theme.css`). |

Selection: `<html data-theme='…'>`, set before first paint by an inline
script in `src/app.html` (hash-pinned into the CSP by `scripts/csp.mjs`),
persisted in `localStorage['sdf-theme']`. With no stored choice the system
colour-scheme preference decides: dark → `dark`, light → `axolotl`.

## Gates

`scripts/check-themes.mjs` runs in CI (`npm run gate`) and enforces, per
theme:

- **Purity** — the file contains exactly one rule,
  `:root[data-theme='<theme-id>']` matching its own file name, holding
  custom-property declarations only.
- **Completeness** — every contract token is declared.
- **Contrast** — WCAG ratios measured with translucent surfaces composited
  over the background beneath and gradients at their least favourable stop:
  4.5:1 for body/status text pairs, 3:1 for borders.

The no-colour-literal rule for `src/` lives in `scripts/lint.mjs`.

## Bundle budget (task 5.6)

Measured with both themes shipped (gzip -9, `scripts/budget.mjs`):

- initial load: 48,231 B (was 45,860 B before this change)
- total load: 55,617 B

The declared budget was re-set to measured + 1 KB headroom
(`49,255` / `56,641`). The growth is: both token sets in the entry CSS
(+~0.7 KB), the pre-paint theme script and its pinned hash in
`index.html` (+~0.4 KB), and the header theme picker (+~1.0 KB). The
picker is deliberately plain buttons — a bound `<select>` or an `{#each}`
block would drag Svelte's selection/each runtime into the initial bundle
for ~2.5 KB more.

## Where the old literals went (task 1.4)

The five colour literals that predated the contract each became a token:

| Old literal | Token now consumed |
|---|---|
| `app.css` `.primary-btn` `color: #04121f` | `--text-on-accent` |
| `app.css` `.danger-btn` `color: #fff` | `--danger` (the destructive button is now a danger-tinted ghost; see CONTRACT.md) |
| `app.css` `.alert.warning` `background: rgba(245,158,11,0.12)` | `--warn-tint` |
| `AuthView.svelte` `.tab-btn.active` `color: #04121f` | `--text-on-accent` |
| `AuthView.svelte` `.register-note` `background: rgba(56,189,248,0.08)` | `--info-tint` |

## Decisions and deviations

### Fonts: system stack, Poppins/Nunito not shipped (task 3.3)

The axolotl palette asked for `'Poppins', 'Nunito'`. Self-hosting even a
weight-subset would spend the bundle budget's small headroom (~1.5 KB gzip
at declaration time) on decoration, and fetching them from a CDN would
reintroduce a third-party origin — banned outright. Both themes therefore
declare the system stack via `--font-body`. Axolotl keeps its identity
through colour, gradient and radius, not typeface.

### Axolotl values changed to pass the gates (task 3.5)

| Original value | Shipped value | Reason |
|---|---|---|
| `--border: rgba(255,255,255,0.8)` | `rgba(51,65,85,0.45)` (translucent slate) | White-on-white borders fail the 3:1 UI-edge contrast requirement against the glass panels and pastel background. |
| Status hues from Tailwind's 500s (`#22c55e`, `#f59e0b`, `#ef4444` family intent) | 700-range equivalents (`#15803d`, `#b45309`, `#b91c1c`) | Status colours render as body-size text on light panels; the 500s measure below 4.5:1 on the composited glass surface. |
| `'Poppins', 'Nunito'` | System stack | See above. |
| Hover lifts (`translateY`) and the gradient-hover button variant | Not ported | Component styling does not belong in themes; motion is kept minimal rather than added per-theme. |
| `.axo-nav`, `.axo-hero`, `.axo-glass-card`, `.axo-btn-primary`, the `@media (max-width: 900px)` block | Deleted | They style markup the companion does not have; a theme selects nothing. What the app wanted (glass surfaces, gradient accent, pill radii) survives as token values consumed by the app's own components. |
| `--text-accent: #0ea5e9` (as link/text colour) | folded into `--accent` | No separate accent-text token exists in the contract. |

The original file was deleted once its transferable part became
`axolotl.css`; there is nothing left to select application elements with.

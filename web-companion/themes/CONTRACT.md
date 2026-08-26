# Token Contract

A theme is a set of CSS custom properties and nothing else (see `THEMES.md`
for the shipped themes and the checks that keep this honest). Every colour,
gradient, shadow, radius and font family the companion renders comes from a
token on this list; the app contains no colour literals outside theme files
(enforced by `scripts/lint.mjs`) and every shipped theme must declare every
token here (enforced by `scripts/check-themes.mjs`).

Themes are declared in their own root scope:

```css
:root[data-theme='<theme-id>'] { /* tokens only */ }
```

## The tokens

### Surfaces

| Token | Meaning | Must not be used for |
|---|---|---|
| `--bg` | Page background colour (fallback beneath `--bg-image`; also the inset surface inputs sit on) | Text or border colour |
| `--bg-image` | Page background image layered over `--bg` (`none` for flat themes) | Anything except `background-image` |
| `--panel` | Main panel surface (the `<main>` card) | |
| `--panel-2` | Raised/nested panel surface (dashboard sections) | |
| `--surface-blur` | Backdrop blur applied to panels (`none` allowed) | Anything except `backdrop-filter` |

Panels may be translucent (glassmorphism). Legibility must not depend on the
blur being available: pick alphas so content reads correctly with
`backdrop-filter` unsupported (the contrast gate composites, never credits
blur).

### Lines

| Token | Meaning |
|---|---|
| `--border` | Hairline borders on panels, inputs, tables, secondary buttons |
| `--shadow` | Panel drop shadow (`none` allowed) |

### Text

| Token | Meaning |
|---|---|
| `--text` | Primary text on any surface |
| `--muted` | Secondary text: labels, hints, status lines, headers' subtitle |
| `--text-on-accent` | Text rendered on an accent fill (primary/danger buttons, active tab) |

`--text-on-accent` is a single token shared by every accent fill that
carries text: the primary button and the active tab. The destructive button
deliberately does NOT use a solid `--danger` fill — no single
`--text-on-accent` value could clear 4.5:1 against both a light pastel
gradient and a saturated danger red across dark and light themes — so it is
rendered as a danger-tinted ghost (see `--danger-tint`).

### Accent

| Token | Meaning |
|---|---|
| `--accent` | Accent colour for fills that carry no text (progress bar), links if any appear |
| `--accent-strong` | Emphasis variant of the accent: the keyboard focus ring (`:focus-visible` in `app.css`) and hover edges |
| `--accent-gradient` | Fill of primary actions; may be a gradient or a flat value |

`--accent-strong` never carries text, so the gate holds it to the 3:1
UI-component threshold against every surface a focused control can sit on —
panel, dashboard section and page background — rather than to 4.5:1.

### Status

| Token | Meaning |
|---|---|
| `--ok` | Success state text/accents |
| `--warn` | Warning state text/accents |
| `--danger` | Danger/failure/refusal text, destructive button fill |
| `--info` | Informational state text/accents |
| `--warn-tint` | Translucent surface tint behind warning alerts (`.alert.warning`) |
| `--danger-tint` | Translucent surface tint behind refusal/error callouts (`.refusal`, `.danger-btn`) |
| `--info-tint` | Translucent surface tint behind informational callouts (`.register-note`) |

Status colours are used as *text* at body size in status lines and refusals,
so each must clear 4.5:1 against `--panel` — they are decorative accents
nowhere. Each tint is measured with the text that lands on it, composited
over both panel surfaces.

There is deliberately no `--ok-tint`: no component renders a success
callout, and the contract is exact (see below), so carrying an unconsumed
token in every theme buys nothing. Add it back in the change that adds the
callout.

### Device vocabulary

The three device-state conditions the app is required to distinguish. These
tokens only ever **reinforce** distinctions the rendered text already makes
(`Unknown`, `N/A`, `(awaiting confirmation)`); they are never a replacement
for it, and no state distinction relies on them alone.

| Token | Applied to |
|---|---|
| `--unknown` | Values the device holds no reading for ("Unknown") |
| `--not-applicable` | Subsystems absent by build/config ("N/A") |
| `--assumed` | Lock states derived from a command rather than confirmed ("(awaiting confirmation)") |

### Shape

| Token | Used by |
|---|---|
| `--radius` | Inputs, buttons, small controls, alert callouts |
| `--radius-lg` | Main panel, dashboard sections |
| `--radius-pill` | Pill-shaped controls (tab buttons) |

### Type

| Token | Meaning |
|---|---|
| `--font-body` | The body font stack |

A theme may only name families that are self-hosted in the app's assets or
expected to be present on the system. Fetching fonts from any origin at
runtime is banned (no third-party origins).

### Rendering intent

| Token | Meaning |
|---|---|
| `--color-scheme` | The theme's colour-scheme hint (`dark` or `light`), consumed by `color-scheme` on the root element |

## Gradient-valued tokens

Two tokens can hold gradients: `--bg-image` and `--accent-gradient`.
Gradient values are valid only where CSS accepts an `<image>`:

- `background-image`, `background` shorthand

They MUST NOT be fed to properties that cannot take one — notably
`color`, `border-color`, `outline-color`, `fill`, `text-decoration-color`.
Components that need a solid colour derived from a gradient accent consume
`--accent` / `--accent-strong` instead. For this reason a theme's
`--accent-gradient`, even when it is a gradient, must stay visually
consistent with its `--accent` family, and filled controls consume the
gradient token via `background` only.

## Writing colour values

Colour-valued tokens must be written as hex, `rgb()`/`rgba()` or
`hsl()`/`hsla()`. That is not stylistic: `scripts/check-themes.mjs` measures
contrast from these values, and a syntax it cannot read (`color-mix()`,
`oklch()`, a named colour) **fails the build** rather than being skipped — a
skipped pair used to report "contrast OK", which is exactly the outcome the
gate exists to prevent.

## Adding or changing a token

The contract is exact in both directions: every shipped theme must declare
every token listed here, and a theme declaring anything *else* fails too —
an unconsumed token is weight every theme has to carry.

Adding a token here therefore obliges every shipped theme to declare it and
some component to consume it. Removing or renaming one is a diff to this
file plus both theme files and the consuming components.

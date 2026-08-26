# Gate Evidence — web-companion-theming

Every new gate proven red on a deliberate violation, then green once the
violation was removed. All runs from `web-companion/`, 2026-08-26.

## 1. No colour literal under `src/` (`scripts/lint.mjs`, task 5.1)

Red — appended `.sneak { color: #123456; }` to `src/app.css`:

```
lint error  src/app.css:203  colour literal banned under src/ (hex colour
literal) - use a theme token (themes/CONTRACT.md)
lint FAILED
```

Green after removing the line: `lint OK  (35 files scanned)`

## 2. Theme purity (task 5.2)

Red — `themes/broken.css` with an element selector and a visibility/layout
property:

```css
:root[data-theme='broken'] { --bg: #000000; }
body { display: none; }
```

```
theme error  themes/broken.css: nested braces are not allowed in a theme
theme check FAILED
```

(A file may hold only the single root rule; any second selector, at-rule or
non-custom declaration is rejected.)

## 3. Token completeness (task 5.3)

Red — `themes/broken.css` declaring only `--bg` and `--text`:

```
theme error  themes/broken.css: missing contract token(s): --bg-image,
--panel, --panel-2, --surface-blur, --border, --shadow, --muted,
--text-on-accent, --accent, --accent-strong, --accent-gradient, --ok,
--warn, --danger, --info, --ok-tint, --warn-tint, --danger-tint,
--info-tint, --unknown, --not-applicable, --assumed, --radius,
--radius-lg, --radius-pill, --font-body, --color-scheme
```

## 4. Contrast (task 5.4)

Red — `themes/broken.css` as a light-grey-on-white palette:

```
theme error  themes/broken.css: --text on --panel measures 1.41:1 (needs 4.5:1), composited at the worst gradient stop
theme error  themes/broken.css: --text on --panel-2 measures 1.38:1 (needs 4.5:1), composited at the worst gradient stop
theme error  themes/broken.css: --text on page background measures 1.61:1 (needs 4.5:1), composited at the worst gradient stop
theme error  themes/broken.css: --muted on --panel measures 1.19:1 (needs 4.5:1), composited at the worst gradient stop
...
```

Green after removing every violation:

```
theme: themes/axolotl.css purity OK, completeness 29/29, contrast OK
theme: themes/dark.css purity OK, completeness 29/29, contrast OK
theme check OK
lint OK  (35 files scanned)
```

## 5. CSP pinning of the pre-paint script (tasks 4.4–4.5)

`npm run build` output shows both inline scripts pinned by hash; no policy
loosening:

```
csp: build/index.html <- 'sha256-CgsJIHldTL5S1LcX02F/+EmlAJWcW5Vh7pXiTCgQ7k8=' 'sha256-FYsy+qglhPlwDor4ZUgFGhkvYDPS0CHo1Fe+fFqymUs='
```

Built `script-src`: `script-src 'self' 'sha256-…' 'sha256-…'` — no
`unsafe-inline`, no `unsafe-eval`. The theme script sits in `<head>` before
the stylesheets and SvelteKit bootstrap, so `data-theme` is set before
first paint (no flash of a non-chosen theme).

## 6. View walkthrough in both themes (tasks 7.1, 7.2, 6.2, 6.3)

Performed 2026-08-26 with Playwright against (a) the real built app served
by `vite preview` and (b) `theme-walkthrough.html` — a faithful single-file
replica (tokens verbatim from `themes/*.css`, element styles from
`app.css` + component `<style>` blocks) that can render the device-bound
views without hardware.

Real-app checks:
- First run with no stored choice and `prefers-color-scheme: light` applied
  **axolotl** before first paint; switching to Dark updated
  `<html data-theme>`, `localStorage['sdf-theme']`, body background and the
  primary button's gradient/text; reload restored Dark pre-paint.
- Built `script-src` carries the two pinned hashes, no `unsafe-inline`/`unsafe-eval`.

Replica walkthrough (screenshots per view × theme): connection, wizard
steps 1–4, auth, and the full dashboard (status cards, health with
Unknown/N/A/(awaiting confirmation) vocabulary rows, config, enrolment
with progress + refusal, user management incl. a 16-char device name and a
refusal callout, Nuki re-pair, Zigbee join, OTA with warning, ghost danger
button and progress bar). **No unreadable or invisible element in either
theme.** Additional passes:

- Greyscale (colour rendered identically) over the dashboard in both
  themes: every state distinction survives on its text (task 6.3).
- Blur off on the axolotl glass panels: all content legible (task 6.2).

## 7. Bundle budget re-measure (task 5.6)

Baseline before this change (built from `HEAD` in a scratch worktree):
initial 45,860 B gzip / total 53,636 B.

With both themes shipped: initial **48,231 B** / total **55,617 B**
(measured via `scripts/budget.mjs`; the run first EXCEEDED the old limits
by 2,151/321 bytes). `budget.json` re-declared to measured + 1 KB headroom:
49,255 / 56,641. Growth breakdown in `themes/THEMES.md`.

The budget gate itself stayed untouched and still gates the build.

## 8. Review fixes (section 8 of tasks.md)

Every changed or added check re-proven red before green, 2026-08-26.
Mutations were applied to working copies and restored from a scratchpad
backup afterwards; `git status` confirms a clean tree apart from the
intended changes.

### 8a. Gradients are measured at every stop (task 8.1)

Red — axolotl `--text-on-accent: #f8fafc` (the value the old gate silently
skipped, because a gradient is not a single colour):

```
theme error  themes/axolotl.css: --text-on-accent on --accent measures 2.65:1 (needs 4.5:1), composited at the worst gradient stop
theme error  themes/axolotl.css: --text-on-accent on --accent-gradient (worst stop) measures 1.72:1 (needs 4.5:1), composited at the worst gradient stop
theme: themes/axolotl.css purity OK, completeness 28/28, contrast FAILED
theme: themes/dark.css purity OK, completeness 28/28, contrast OK
theme check FAILED
```

Note the second line: the pair the old gate skipped is now measured. Note
also the fourth: `dark` is reported OK in the same run — the per-theme
reporting fix (task 8.3), which the old global flag would have printed as
`contrast FAILED` for both.

### 8b. An unreadable value fails instead of being skipped (task 8.2)

Red — `dark`'s `--panel` in three syntaxes the check cannot read:

```
theme error  themes/dark.css: --panel: "color-mix(in srgb, #0b1522 80%, white)" uses color-mix(), which this check cannot read - use hex, rgb()/rgba(), hsl()/hsla() or a gradient of them (a partially read value would be measured as the wrong colour)
theme error  themes/dark.css: --panel: "oklch(0.28 0.03 250)" uses oklch(), which this check cannot read - …
theme error  themes/dark.css: --panel: "whitesmoke" holds no colour this check can read - use hex, rgb()/rgba() or hsl()/hsla() (an unreadable value cannot be measured)
```

The `color-mix()` case is why the check rejects unknown *functions* rather
than only values it finds no colour in: the first attempt at this fix
extracted `#0b1522` from inside the `color-mix()` arguments and measured
that, passing while rendering something else. Found by running this probe,
not by review.

Green (unmutated): `theme check OK`, both themes `contrast OK`.

`hsl()` is now read rather than skipped — red with
`--panel: hsl(210 40% 92%)` in the dark theme:

```
theme error  themes/dark.css: --text on --panel measures 1.02:1 (needs 4.5:1), …
theme error  themes/dark.css: --muted on --panel measures 2.12:1 (needs 4.5:1), …
(+ 8 further pairs on the same surface)
```

### 8c. The contract is exact in both directions (task 8.5)

Red — `--sneaky: #ff00ff` added to `themes/dark.css`:

```
theme error  themes/dark.css: token(s) outside the contract: --sneaky - add them to CONTRACT.md (and to every theme) or drop them
theme: themes/dark.css purity OK, completeness 28/28 +1 outside the contract, contrast OK
theme check FAILED
```

The report line names the completeness failure as a completeness failure —
a first attempt printed it as `contrast FAILED`, also found by this probe.

### 8d. `--accent-strong` gated as a focus ring (task 8.5)

Not a synthetic probe: making the token live failed the *shipped* axolotl
theme, which is the point.

```
theme error  themes/axolotl.css: --accent-strong as a focus ring on --panel measures 2.55:1 (needs 3:1), …
theme error  themes/axolotl.css: --accent-strong as a focus ring on --panel-2 measures 2.42:1 (needs 3:1), …
theme error  themes/axolotl.css: --accent-strong as a focus ring on page background measures 2.04:1 (needs 3:1), …
```

Green after `#0ea5e9` → `#0284c7` (recorded in `themes/THEMES.md`).

### 8e. Named CSS colours banned under `src/` (task 8.6)

Red — appended `.sneak { color: red; }` to `src/app.css`:

```
lint error  src/app.css:210  colour literal banned under src/ (named CSS colour) - use a theme token (themes/CONTRACT.md)
lint FAILED
```

Green after removing the line: `lint OK  (36 files scanned)`.

The rule matches a named colour only as the value of a colour-bearing
property, so prose and identifiers containing colour words (`red-team`,
`--danger`, a comment mentioning "white") do not trip it.

### 8f. Theme selection tests (task 8.7)

`src/lib/state/theme.test.ts`, 8 tests. Each was proven red by a mutation
of the code it covers, then green unmutated:

| Mutation | Assertion that fired |
|---|---|
| `setTheme` writes the id to `session.transport` | `expected [ …(2) ] to deeply equal []` (and the `write` spy) |
| drop the `localStorage.setItem` in `setTheme` | `expected null to be 'axolotl'` |
| remove the `try`/`catch` around `setItem` | `expected [Function] to not throw an error but 'Error: storage disabled' was thrown` |
| `initialTheme()` ignores `document.documentElement.dataset.theme` | `expected 'dark' to be 'axolotl'` |
| picker emits `data-pressed` instead of `aria-pressed` | `expected null to be 'true'` |
| add `themes/ghost.css` with no `THEMES` entry | `expected [ 'axolotl', 'dark', 'ghost' ] to deeply equal [ 'axolotl', 'dark' ]` |
| pre-paint script reads `'sdf-theme-v2'` | `expected 'sdf-theme-v2' to be 'sdf-theme'` |
| pre-paint default maps light → `dark` | `expected '<script> // Pre-paint theme applicati…' to match /light\)'\)\.matches \? 'axolotl' : 'd…/` |

### 8g. Full gate after the fixes

```
lint OK  (36 files scanned)
theme: themes/axolotl.css purity OK, completeness 28/28, contrast OK
theme: themes/dark.css purity OK, completeness 28/28, contrast OK
theme check OK
Test Files  9 passed (9)
     Tests  75 passed (75)
initial:  47.1 KB measured / 48.1 KB budget
total:    54.2 KB measured / 55.3 KB budget
budget OK
```

Exact figures for the record: initial **48,225 B**, deferred **7,258 B**,
total **55,483 B** gzip — 6 B / 134 B below the figures section 7 measured
(a hex value changed, a token left the contract). `budget.json` stays at
49,255 / 56,641: the measurement is inside the limits, and those limits are
the deliberate values argued in `design.md`, "Decision: the budget rises
once, deliberately", not a rounding of any single measurement. The rule
caps a re-declaration at min(measured + headroom, previous limit), so a
future re-declaration ratchets *down* — to 49,249 / 56,507 on today's
figures — and can never move up without an argument like that one.

Section 7 above describes the raise as a *re-measurement*, which is the
framing the rule exists to forbid. The design decision is the correct
record of it; that wording stands as written for the history.

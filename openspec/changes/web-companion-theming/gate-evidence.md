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

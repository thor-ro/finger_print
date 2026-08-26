## Why

The companion has one hard-coded appearance, and the palette it would need to adopt is not expressible as a drop-in file.

- **`themes/axolotl-theme.css` cannot be applied.** It selects `.axo-nav`, `.axo-hero`, `.axo-glass-card`, `.axo-btn-primary` — none of which exist in the app, whose markup is `.app-container`, `.dashboard-section`, `.status-msg`, `.primary-btn`. Dropped in as-is it changes `body` and nothing else. Its top 25 lines (the token block) are the part that transfers; the remaining 145 are component styling and landing-page layout for a page the companion does not have.
- **The rewrite tokenised most of the palette but not all of it.** `src/app.css` declares 11 tokens and then hard-codes past them: `color: #04121f` (`:121`), `color: #fff` (`:131`), `background: rgba(245, 158, 11, 0.12)` (`:156`), plus `#04121f` and `rgba(56, 189, 248, 0.08)` in `AuthView.svelte`. A theme that set every existing token would still leave those five values dark-palette. That is precisely how a light theme becomes unreadable.
- **The token set is too small to carry the app's meaning.** `--ok`, `--warn` and `--danger` exist; there is no token for the distinctions the companion is *specified* to make — a value the device measured versus one it reports as unknown versus one it reports as not applicable (`companion-device-health`), or a lock state the lock confirmed versus one the device assumed. Today those are rendered as text only (`Unknown`, `N/A`, `(awaiting confirmation)`), so a theme cannot break them — but neither can a theme reinforce them, and any theme-time attempt to colour them would invent its own values.
- **There is no contract, so there is nothing to keep a theme honest.** If a theme is "a stylesheet", every theme has to know the app's class names, the app can never rename one, and nothing stops a theme from setting `display: none` on a control. If a theme is "a token set", the contract is small, reviewable, and mechanically checkable.
- **Two constraints from `web-companion-tooling` are easy to violate by accident.** Axolotl asks for `'Poppins', 'Nunito'` — loading those from a font CDN would break "no third-party origin at runtime", which the rebuild only just achieved by dropping the legacy `fonts.googleapis.com` link. And a pre-paint theme script is the ordinary cure for a flash of the wrong theme, which collides with `script-src 'self'` unless it goes through the existing hash-pinning step.

## What Changes

- **Define a token contract**: a documented, complete set of CSS custom properties covering surfaces, borders, text (including text-on-accent), accent, the four status colours, and the health vocabulary — unknown, not-applicable, assumed — plus radii and font stacks. The app renders from tokens only.
- **A theme is a token declaration and nothing else.** It sets tokens; it selects no application element, sets no layout, adds no selector beyond its own root scope. Enforced by a CI check, not by review — the same shape as the `{@html}` ban.
- **Finish the tokenisation**: remove the five remaining colour literals, and add the tokens the app's semantics need but the current set lacks.
- **Ship two themes**: the current dark palette, and the axolotl palette expressed in the contract — its gradients, glass surfaces and pill radii carried over as token values.
- **Add a theme picker** that persists locally and defaults to the system colour-scheme preference, applied before first paint through the existing CSP hash-pinning step rather than by loosening the policy.
- **Gate what a theme can break**: every shipped theme is checked in CI for token completeness and for text/surface contrast, and every state distinction the app depends on must survive in every theme.
- **Honour `prefers-reduced-motion`**, and stay legible where `backdrop-filter` is unsupported — axolotl's hover transforms and blur are the reason this is not theoretical.

### Capabilities

- **New:** `web-companion-theming`

### Dependencies

Depends on `web-companion-tooling`, which supplies the component structure, the lint harness the theme checks extend, the bundle budget the themes must fit, and the CSP hash-pinning step the pre-paint script needs. Tokenising the legacy `style.css` first would have meant doing the work twice.

Note that `web-companion-tooling`'s hardware parity checklist (tasks 6.1-6.9) is still open. This change alters appearance across every view, which is exactly what that checklist looks at — running it before this change lands gives a clean baseline; running it after conflates two sets of differences.

### Non-goals

- Per-user or device-stored theme preference. The choice is local to the browser and never travels over BLE.
- Layout, spacing or information-architecture changes. Tokens carry colour, radius, shadow and type — not structure.
- A theme authoring UI, user-supplied themes, or loading themes from a URL.
- Restyling toward the axolotl file's landing-page layout (`.axo-hero`, `.axo-nav`); the companion has no such page.

### Accepted risks

- **A light theme surfaces contrast bugs the dark theme hid.** That is the point of the contrast gate; expect the first run to fail on real values.
- **Adding themes adds bytes to the initial load.** Token blocks are small, but the budget is at 53.4 KB with 44.5 KB measured, so the headroom is finite and shared with everything else. The budget stays the gate.
- **Glass surfaces depend on `backdrop-filter`.** Supported in the browsers the companion supports at all (Chrome/Edge), but a translucent surface over a gradient is a legibility risk independent of support, which the contrast check must evaluate against the effective background, not the token in isolation.

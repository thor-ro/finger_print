## 1. Token Contract

- [x] 1.1 Write the token contract into the repository (`web-companion/themes/CONTRACT.md` or equivalent): every token, its meaning, and what it must not be used for
- [x] 1.2 Extend the current 11 tokens in `src/app.css` with the missing groups: `--text-on-accent`, per-status surface tints, `--info`, the device vocabulary (`--unknown`, `--not-applicable`, `--assumed`), radii, `--bg-image`, `--accent-gradient`, `--font-body`
- [x] 1.3 Decide and document how gradient-valued tokens are consumed, so a gradient is never fed to a property that cannot take one (`border-color`, `color`)
- [x] 1.4 Record which token each of today's five literals becomes: `app.css:121` `#04121f`, `app.css:131` `#fff`, `app.css:156` `rgba(245,158,11,0.12)`, `AuthView.svelte:68` `#04121f`, `AuthView.svelte:75` `rgba(56,189,248,0.08)`

## 2. Tokenise The App

- [x] 2.1 Replace those five literals with tokens; confirm no colour literal remains anywhere under `src/`
- [x] 2.2 Move the default theme's values out of `src/app.css`'s `:root` into a theme file, leaving `app.css` holding element defaults that reference tokens only
- [x] 2.3 Apply the device-vocabulary tokens in the health view as reinforcement of the existing text (`Unknown`, `N/A`, `(awaiting confirmation)`), never as a replacement for it
- [x] 2.4 Apply the status tokens to user-management refusals so a refusal reads differently from ordinary status text in every theme

## 3. Shipped Themes

- [x] 3.1 Author the dark theme file from the current palette, declaring the full contract
- [x] 3.2 Author the axolotl theme from `themes/axolotl-theme.css`'s token block, carrying its gradient background, glass surfaces, gradient accent and pill radii into the contract
- [x] 3.3 Resolve axolotl's `'Poppins', 'Nunito'` request: fall back to the system stack, or self-host a weight subset and re-measure the budget; record which was chosen and why
- [x] 3.4 Delete the parts of `themes/axolotl-theme.css` that cannot be a theme (`.axo-nav`, `.axo-hero`, `.axo-glass-card`, `.axo-btn-primary`, the `@media` block), moving any styling the app actually wants into the relevant component
- [x] 3.5 Record any axolotl value that had to change to pass a gate, with the reason

## 4. Selection And Persistence

- [x] 4.1 Add a theme picker reachable from every view, including before authentication
- [x] 4.2 Persist the choice in `localStorage`; confirm no write reaches any characteristic when the theme changes
- [x] 4.3 Default to `prefers-color-scheme` when no choice is stored — dark to the dark theme, light to axolotl (see design.md; revisit if axolotl is to be the identity default in both cases)
- [x] 4.4 Apply the stored theme before first paint via an inline `<script>` in `src/app.html`, and confirm `scripts/csp.mjs` pins its hash automatically alongside the SvelteKit bootstrap
- [x] 4.5 Verify no flash of the non-chosen theme on reload, and that the built policy still carries neither `unsafe-inline` nor `unsafe-eval` for scripts

## 5. Gates

- [x] 5.1 Extend `scripts/lint.mjs` (or add a sibling) with the no-colour-literal rule for `src/`
- [x] 5.2 Add the theme-purity check: a theme file may declare tokens in its own root scope only — no element/class selectors, no layout, positioning, sizing or visibility properties
- [x] 5.3 Add the token-completeness check: every shipped theme declares every contract token
- [x] 5.4 Add the contrast check, compositing translucent surfaces over the background beneath and measuring gradients at their least favourable stop; thresholds 4.5:1 body, 3:1 large text and UI edges
- [x] 5.5 Verify each new gate red before green on a deliberate violation, and record the evidence alongside `gate-evidence.md`
- [x] 5.6 Re-measure the bundle budget with both themes shipped; adjust the declared budget only with the measured number in hand

## 6. Accessibility

- [x] 6.1 Honour `prefers-reduced-motion`: suppress hover transforms and transitions
- [x] 6.2 Confirm panel content stays legible where `backdrop-filter` is unsupported or disabled
- [x] 6.3 Confirm every state distinction survives with colour rendered identically (greyscale pass over the health view and the user-management refusals)

## 7. Verification And Docs

- [x] 7.1 Walk every view in both themes — connection, wizard (all four steps), auth, dashboard with health, config, enrolment, user management and OTA — and confirm nothing is unreadable or invisible
- [x] 7.2 Confirm a long device-reported user name and a refusal message render correctly in both themes
- [x] 7.3 Update `web-companion/README.md` with the theme picker, the token contract, and how to add a theme
- [x] 7.4 Deploy and confirm both themes on the live Pages site

## 8. Review Fixes

- [x] 8.1 Measure gradient-valued tokens at every stop instead of skipping them: `--text-on-accent` on `--accent-gradient` was never measured, so a 1.00:1 primary button passed the gate
- [x] 8.2 Make a token value the contrast check cannot read a failure rather than a skipped pair, and read `hsl()`/`hsla()` (an `hsl()` panel behind near-white text also passed)
- [x] 8.3 Report contrast per theme: a global failure flag labelled every theme printed after the first failure as failing, including untouched ones
- [x] 8.4 Measure the text that lands on `--info-tint`, and measure every status tint over both panel surfaces rather than the dashboard surface alone
- [x] 8.5 Give the contract's unconsumed tokens a consumer or drop them: `--accent-strong` becomes the `:focus-visible` ring (gated at 3:1 as a UI edge), `--info` edges the informational callout, `--ok-tint` leaves the contract; make the contract exact by failing a theme that declares anything outside it
- [x] 8.6 Extend the no-colour-literal rule to named CSS colours (`color: red` passed lint), matched only as the value of a colour-bearing property so prose stays safe
- [x] 8.7 Cover theme selection in tests, not only in a one-off walkthrough: persistence, the pre-paint starting point, storage being unavailable, and that a theme change writes nothing to the device; pin the four places a theme is declared (`themes/<id>.css`, `THEMES`, the pre-paint script, the picker) against drift
- [x] 8.8 Record the budget raise (46,080 → 49,255 / 55,296 → 56,641) as a deliberate, separately justified decision, and reconcile `scripts/budget.mjs`'s "never rises" comment with it
- [x] 8.9 Record the removal of the Enroll-Admin dashboard section: it arrived inside this change with no task of its own, and its rationale lived only in a code comment
- [x] 8.10 Re-prove every changed and added gate red before green, and record the evidence
- [x] 8.11 Record that this change landed before `web-companion-tooling` 6.1-6.9, contrary to its own risk row

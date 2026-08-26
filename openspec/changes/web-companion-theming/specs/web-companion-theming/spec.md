## ADDED Requirements

### Requirement: The App Renders Only From Theme Tokens

Every colour, gradient, shadow, border radius and font family the Web Companion renders SHALL be expressed as a theme token. Neither the global stylesheet nor any component SHALL contain a colour literal, a font family, or a radius outside the token definitions.

The absence of such literals SHALL be enforced by a check that runs in continuous integration, so that a regression fails the build rather than relying on review.

#### Scenario: A colour literal fails the build

- **WHEN** application source introduces a colour literal outside the token definitions
- **THEN** the check fails the build
- **AND** it reports the file and the literal

#### Scenario: Switching theme changes every surface

- **WHEN** a different theme is selected
- **THEN** every view, control, table, status line and panel takes its appearance from that theme
- **AND** no element keeps a colour from the previous theme

### Requirement: A Theme Declares Tokens And Nothing Else

A theme SHALL consist of token declarations within its own root scope. A theme SHALL NOT select application elements, SHALL NOT declare layout, positioning, sizing or visibility, and SHALL NOT introduce rules that apply to anything other than the token scope it owns.

This SHALL be enforced by a check in continuous integration.

#### Scenario: A theme that selects an application element is rejected

- **WHEN** a theme file contains a selector for an application element or class
- **THEN** the check fails the build

#### Scenario: A theme cannot hide or move a control

- **WHEN** a theme file declares a layout, positioning, sizing or visibility property
- **THEN** the check fails the build

#### Scenario: Renaming an application class does not break themes

- **WHEN** a component's class name is changed
- **THEN** no theme file requires an edit

### Requirement: The Token Contract Is Complete And Documented

The token contract SHALL be documented in the repository and SHALL cover, at minimum: page background, panel surfaces, borders and shadows, primary text, muted text, text placed on an accent, the accent and its emphasis, the status colours for success, warning, danger and information, the device-state vocabulary of unknown, not-applicable and assumed, border radii, and the font stack.

Every shipped theme SHALL declare every token in the contract. A theme missing a token SHALL fail the build rather than silently inheriting another theme's value.

#### Scenario: Incomplete theme fails

- **WHEN** a shipped theme omits a token named in the contract
- **THEN** the build fails and names the missing token

#### Scenario: Adding a token obliges every theme

- **WHEN** a new token is added to the contract
- **THEN** the build fails until every shipped theme declares it

#### Scenario: Contract is reviewable

- **WHEN** the token contract changes
- **THEN** the change appears as a diff to the documented contract in the repository

### Requirement: Themes Preserve The Distinctions The App Depends On

Every shipped theme SHALL keep distinguishable the conditions the Web Companion is required to distinguish: a measured value from an unknown one and from a not-applicable one, an assumed lock state from a confirmed one, and a specific refusal from ordinary status text.

These distinctions SHALL NOT be carried by colour alone. The text the app renders SHALL remain sufficient to tell the conditions apart with colour removed.

#### Scenario: Distinctions survive with colour removed

- **WHEN** the app is viewed with theme colours rendered identically
- **THEN** measured, unknown and not-applicable values remain distinguishable from their text
- **AND** an assumed lock state remains distinguishable from a confirmed one

#### Scenario: Every theme keeps the states apart

- **WHEN** a shipped theme is applied
- **THEN** unknown, not-applicable and assumed states are visually distinguishable from a measured value
- **AND** from each other

### Requirement: Text Contrast Is Verified For Every Theme

Continuous integration SHALL verify, for every shipped theme, that each text token meets a contrast ratio of at least 4.5:1 against the surfaces it is rendered on, and that large text, borders and control outlines meet at least 3:1. A theme below either threshold SHALL fail the build.

Where a surface is translucent, the check SHALL measure the composited result over the background beneath it. Where a background or accent is a gradient, the check SHALL measure against its least favourable stop rather than an average.

#### Scenario: Low-contrast theme fails

- **WHEN** a theme declares a text token that falls below the threshold against its surface
- **THEN** the build fails and reports the token pair and the measured ratio

#### Scenario: Translucent surface measured composited

- **WHEN** a theme declares a translucent panel surface
- **THEN** the check measures text against the surface composited over the page background
- **AND** not against the translucent value alone

#### Scenario: Gradient measured at its worst stop

- **WHEN** a theme declares a gradient background or a gradient accent
- **THEN** the check measures against the stop that yields the lowest contrast

### Requirement: The User Chooses A Theme And The Choice Persists

The Web Companion SHALL let the user choose among the shipped themes, and SHALL restore that choice on subsequent visits from the same browser.

On first use, with no stored choice, the app SHALL follow the system colour-scheme preference. The choice SHALL be stored locally in the browser only; it SHALL NOT be written to the device, and SHALL NOT be sent over the BLE connection.

#### Scenario: Choice persists across reloads

- **WHEN** the user selects a theme and later reloads the app
- **THEN** the selected theme is applied

#### Scenario: First run follows the system preference

- **WHEN** the app is opened with no stored theme choice
- **THEN** the theme applied matches the system colour-scheme preference

#### Scenario: Choice never reaches the device

- **WHEN** the user changes theme
- **THEN** no write is made to any characteristic
- **AND** the device holds no record of the choice

#### Scenario: Theme is selectable before authentication

- **WHEN** the app is showing the connection or login view
- **THEN** the theme can still be changed

### Requirement: The Chosen Theme Is Applied Before First Paint

The stored theme SHALL be applied before the first paint, so that no other theme is briefly visible on load.

The mechanism SHALL NOT weaken the app's Content Security Policy: no `unsafe-inline` and no `unsafe-eval` shall be introduced for scripts. Any inline script used for this purpose SHALL be pinned by hash in the policy at build time.

#### Scenario: No flash of another theme

- **WHEN** the app is loaded with a stored theme that differs from the default
- **THEN** the stored theme is in effect on first paint

#### Scenario: Policy is not loosened

- **WHEN** the built output is inspected
- **THEN** the script policy contains neither `unsafe-inline` nor `unsafe-eval`
- **AND** every inline script present is covered by a pinned hash

### Requirement: Themes Respect Motion And Legibility Preferences

Where a theme's appearance involves transitions, transforms or blur, the app SHALL honour a user preference for reduced motion by suppressing those transitions and transforms.

Surfaces SHALL remain legible where backdrop blur is unavailable, rather than depending on the blur to separate content from the background.

#### Scenario: Reduced motion suppresses movement

- **WHEN** the user's system requests reduced motion
- **THEN** hover transforms and transitions are not applied

#### Scenario: Legible without blur

- **WHEN** backdrop blur is unavailable
- **THEN** panel content remains legible against the page background

### Requirement: Theming Introduces No Third-Party Origin And Fits The Budget

A theme SHALL NOT cause the app to load anything from a third-party origin. A theme may name only font families that are either self-hosted in the app's own assets or expected to be present on the system.

Every shipped theme SHALL be counted in the bundle budget check, and the declared budget SHALL continue to gate the build.

#### Scenario: No third-party font request

- **WHEN** the app is loaded with any shipped theme
- **THEN** it issues no request to an origin other than the one it was served from

#### Scenario: Themes counted against the budget

- **WHEN** the budget check runs
- **THEN** the bytes contributed by the shipped themes are included in the measured initial load

### Requirement: The Provided Palette Ships As A Theme

The palette supplied in `web-companion/themes/axolotl-theme.css` SHALL ship as one of the selectable themes, expressed in the token contract, preserving its palette intent — its gradient background, translucent surfaces, gradient accent and pill radii — while meeting the contrast, distinctness, motion and origin requirements above.

Where the palette's intent cannot be met within those requirements, the app SHALL adopt the nearest value that does meet them, and the deviation SHALL be recorded rather than left implicit.

#### Scenario: Palette selectable

- **WHEN** the user opens the theme picker
- **THEN** the axolotl palette is offered
- **AND** selecting it applies its background, surfaces, accent and radii

#### Scenario: Palette meets the same gates

- **WHEN** the theme checks run
- **THEN** the axolotl theme passes token completeness, contrast and theme-purity like every other shipped theme

#### Scenario: Deviation recorded

- **WHEN** a palette value is altered to satisfy a contrast, motion or origin requirement
- **THEN** the deviation and its reason are recorded in the repository

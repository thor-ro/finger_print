---
applyTo: "openspec/changes/**/*.md,openspec/specs/**/*.md,openspec/specs/**/*.yaml"
---

# OpenSpec Artifact Conventions

These conventions apply when editing OpenSpec artifacts (proposals, specs, designs, tasks) under `openspec/`. They complement the repo-wide standards in `.github/copilot-instructions.md`. Detailed per-artifact rules live in `openspec/config.yaml` under `rules:`.

## Requirement Format

- Use `### Requirement:` headers (3 hashtags) with `#### Scenario:` sub-headers (exactly 4 hashtags)
- Scenarios use GIVEN/WHEN/THEN format for behavior specification
- Proposals should be ≤2 pages
- Every `### Requirement:` heading MUST embed its REQ-ID in the form `### Requirement: REQ-<SCOPE>-NNN — <text>` (em-dash U+2014). Enforced by `scripts/validate-req-ids.sh`.

## Requirement ID Naming

All requirement IDs follow the canonical regex `REQ-[A-Z0-9]+(?:-[A-Z][A-Z0-9]*)*-(?:AC)?\d+(?:-AC\d+)?`. The authoritative capability → scope mapping lives in [openspec/capabilities.yaml](../../openspec/capabilities.yaml); the validator `scripts/validate-req-ids.sh` is the enforcement point.

| Format | Pattern | Example | When to Use |
|--------|---------|---------|-------------|
| Scoped | `REQ-SCOPE-NNN` | `REQ-MMTR-UI-001` | Feature requirements scoped to a capability — **preferred for new specs** |
| Scoped + sub | `REQ-SCOPE-SUB-NNN` | `REQ-MMTR-UI-001` | Sub-component scope (the scope segment may contain hyphens) |
| Scoped + AC | `REQ-SCOPE-NNN-ACNN` | `REQ-MMTR-UI-001-AC03` | Acceptance criterion decomposing a scoped requirement |
| Legacy numeric | `REQ-NNN-ACNN` | `REQ-016-AC05` | **Deprecated for new specs**; preserved for backward compatibility |

- The scope of a REQ-ID MUST match the capability's `reqScope` in `openspec/capabilities.yaml`.
- `AC` is reserved as the acceptance-criterion suffix marker.

### Adding a new capability

1. Create the capability directory `openspec/specs/<slug>/` (or a delta under `openspec/changes/<change>/specs/<slug>/`).
2. Add an entry to [openspec/capabilities.yaml](../../openspec/capabilities.yaml) with the canonical `reqScope` BEFORE any `### Requirement:` heading lands.
3. Run `bash scripts/validate-req-ids.sh` locally and confirm exit 0.

> **Spec merge fidelity (sync/archive).** When a sync/archive operation **creates a
> brand-new capability main spec** (no existing `openspec/specs/<slug>/spec.md`), copy the
> delta's `## Variables` table(s) **verbatim** and give the new spec a **real Purpose**
> (verbatim from the delta if present, otherwise derived from the change's `proposal.md`).
> Never write a `TBD - created by archiving…` stub. Section order MUST be
> Purpose → Variables → Requirements. This corrects the generated `openspec-sync-specs`
> step 3d; see the `spec-merge-fidelity` skill
> (`.github/skills/spec-merge-fidelity/SKILL.md`). The `TBD - created by archiving` stub is
> rejected in CI by `scripts/validate-spec-merge-fidelity.sh`.

### Tracing tests to requirements

Annotate tests with `@Traces("REQ-…", …)` from `com.porsche.sportapps.core.testing.Traces` (module `:core-testing`). The annotation is:
- `vararg` — one test can satisfy multiple REQ-IDs.
- Visible at runtime via reflection.
- Applicable to test methods OR test classes. Kotlin/JVM does NOT propagate class-level annotations to member methods; consumers (the trace lint and the matrix producer) implement the class-to-method fallback themselves.

CI's `check-test-traces` job reports per-module `@Test` vs `@Traces` coverage as a warning-only signal in v1. The forthcoming `produce-aspice-evidence-bundle` change will enforce thresholds.

## Task List Structure

Tasks are organized in three groups: Core Implementation → DevOps & Quality → Testing. Each group includes verification comments.

## Design Constraints

- The legacy codebase is NOT an architectural reference — use the new stack only (Compose, Koin)
- All Accepted ADRs MUST be read before writing design.md — cite relevant ADRs
- Edge-Only Mock Principle (ADR-010; source-set ownership updated by ADR-019): mock only external boundaries (VHAL, network, persistence)
- Cross-cutting decisions should generate separate ADRs, not stay in design.md

## Full Rules Reference

Detailed per-artifact rules (proposal, specs, design, tasks, apply phases) are defined in `openspec/config.yaml` under the `rules:` section. Consult that file for the authoritative project-specific enforcement rules.

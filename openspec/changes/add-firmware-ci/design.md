## Context

`.github/workflows/deploy-web-companion.yml` is the only workflow in the repo, and it's a static Pages deploy triggered on `web-companion/**` changes — it never touches `firmware/`. There is no signal on push or PR that the firmware still compiles or that its unit tests still pass. `AGENTS.md` pins the toolchain to ESP-IDF v5.5.3 and documents `idf.py build` (from `firmware/`) and a separate `test_runner` build for tests. This change assumes `fix-test-runner-build` has already landed, so `test_runner` builds and links cleanly for `IDF_TARGET=linux` and its host binary exits non-zero on test failure.

GitHub-hosted runners have no ESP32-C6 hardware attached, so on-device flashing/HIL testing (`tests/hil/`) is out of reach for this workflow regardless of design choices here.

## Goals / Non-Goals

**Goals:**
- Every push/PR touching `firmware/**` gets an automatic compile check against the real target (`esp32c6`) and an automatic host-side unit test run (`linux` target via `test_runner`).
- Both checks use the pinned ESP-IDF v5.5.3 toolchain `AGENTS.md` already documents, so CI behavior matches what a contributor would see locally.
- Failures are visible as a failed GitHub check on the commit/PR, fast enough to be useful in a normal review loop.

**Non-Goals:**
- Flashing real hardware or running `tests/hil/` — no hardware exists in GitHub-hosted runners; a self-hosted runner with attached hardware is a possible future change, not this one.
- Enforcing branch protection / required status checks — that's a repo-settings change, mentioned as a follow-up but not part of this workflow file.
- Building `web-companion/` (already covered by the existing deploy workflow) or wiring artifact upload/release automation.

## Decisions

**Use the official `espressif/idf` Docker image, pinned to `v5.5.3`, for both jobs.**
This matches `AGENTS.md`'s documented `source /Users/thorstenropertz/.espressif/v5.5.3/esp-idf/export.sh` toolchain exactly, avoids a slow `install.sh` step on every run, and is the standard approach ESP-IDF projects use in CI. Alternative considered: `espressif/esp-idf-ci-action` — adds an abstraction layer over what's fundamentally just "run idf.py build in the right container"; going with the raw Docker image keeps the workflow legible and easy to reproduce locally (`docker run --rm -v $PWD:/project -w /project/firmware espressif/idf:v5.5.3 idf.py build`).

**Two independent jobs, not one.**
A `build-firmware` job (`idf.py build` for `esp32c6` from `firmware/`) and a `test-firmware` job (`idf.py set-target linux && idf.py build && ./build/sdf_test_runner.elf` — exact binary name confirmed during implementation — from `firmware/test_runner/`) run in parallel. Keeps failures attributable (compile break vs. test regression) and keeps either job's runtime from blocking the other.

**Path-filtered triggers, mirroring `deploy-web-companion.yml`'s pattern.**
`on: push: paths: ['firmware/**', '.github/workflows/firmware-ci.yml']` plus `pull_request` with the same path filter. Avoids running a ~firmware-toolchain-heavy workflow on unrelated doc-only or web-companion-only changes.

**Rely on process exit code for the test job, not log scraping.**
`fix-test-runner-build` (design.md, "Fail loud, not silent") wires `test_runner`'s host binary to exit non-zero on any Unity failure. The CI step just runs the binary and lets the shell's non-zero exit fail the step — no fragile regex over Unity's printed output.

## Risks / Trade-offs

- [ESP-IDF Docker image + full firmware build may be slow (multi-minute) on every push] → Path-filter the trigger so unrelated changes (docs, web-companion) don't pay the cost; consider caching `~/.espressif` / component manager cache in a follow-up if it proves too slow in practice.
- [`test_runner`'s `linux`-target coverage is a subset of the full component tree (per `fix-test-runner-build`'s Non-Goals, `sdf_app`/lock-flow stays hardware-only) — CI gives a false sense of full coverage] → Document the coverage gap in this workflow's job name/summary (e.g. job named "unit-tests (partial — see AGENTS.md)") and in `AGENTS.md`, so it's not mistaken for full regression coverage.
- [Depends on `fix-test-runner-build` landing first; merging this before that lands means the test job is immediately red] → Sequence the two changes; don't apply `add-firmware-ci` until `fix-test-runner-build` is merged and verified locally.

## Migration Plan

1. Confirm `fix-test-runner-build` is merged and `test_runner` builds/runs cleanly for `linux` locally.
2. Add `.github/workflows/firmware-ci.yml` with the two jobs.
3. Push a branch, confirm both jobs run and pass on a known-good commit.
4. Push a branch with a deliberate compile error and a deliberate test failure (separately) to confirm each job actually fails when it should; revert the deliberate breakage.
5. Update `AGENTS.md` Gotchas.
6. (Follow-up, outside this change) Enable required status checks on `main` for both new jobs.

## Open Questions

- Exact `test_runner` host binary output path/name for `IDF_TARGET=linux` — confirm during implementation once `fix-test-runner-build` lands.
- Whether to also build the `debug`/`release` sdkconfig profile variants in CI, or just the base `sdkconfig.defaults` — leaning toward base-only for now to keep CI time down, revisit if profile-specific regressions occur.

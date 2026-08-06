## Why

`.github/workflows/` contains only `deploy-web-companion.yml`, scoped to `web-companion/**`. Nothing builds the firmware or runs its unit tests on push. Recent commits ("various fixes", 7325d6c) have shipped bugs (referred to as A4/A7) that a build-and-test gate would have caught before merge. This is only viable once `test_runner` itself builds and links cleanly (see `fix-test-runner-build`) — this change depends on that one landing first.

## What Changes

- Add `.github/workflows/firmware-ci.yml`, triggered on push/PR touching `firmware/**` (mirroring the path-scoping pattern already used by `deploy-web-companion.yml`).
- **Build job**: compiles the main firmware for `esp32c6` (`idf.py build` from `firmware/`) using a pinned ESP-IDF v5.5.3 Docker image (`espressif/idf:v5.5.3`), matching `AGENTS.md`'s documented toolchain version. Fails the check on any compile error.
- **Unit test job**: builds `firmware/test_runner` for `IDF_TARGET=linux` and executes the resulting host binary, treating any Unity test failure (or non-zero exit) as a CI failure.
- Both jobs run in parallel, gate PRs to `main` (branch protection / required status checks configured separately, outside this change's scope), and complete in CI-reasonable time (no hardware flashing, no self-hosted runner).
- Update `AGENTS.md` Gotchas to remove "No CI workflows exist yet" once the workflow lands.

## Capabilities

### New Capabilities
- `firmware-ci`: defines the CI workflow's triggers, jobs (build + unit test), toolchain pinning, and pass/fail semantics for firmware changes.

### Modified Capabilities
(none)

## Impact

- New file: `.github/workflows/firmware-ci.yml`
- `AGENTS.md` (Gotchas section)
- Depends on `fix-test-runner-build` landing first (this change assumes `test_runner` builds cleanly for `linux`); sequence proposals accordingly.
- Does not cover on-hardware/HIL testing (`tests/hil/` stays out of CI scope — no hardware attached to GitHub-hosted runners).

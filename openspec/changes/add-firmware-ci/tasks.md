## 1. Prerequisite Check

- [ ] 1.1 Confirm `fix-test-runner-build` is merged and `firmware/test_runner` builds/links/runs cleanly for `IDF_TARGET=linux` locally
- [ ] 1.2 Confirm the `test_runner` host binary's exact output path (e.g. `build/sdf_test_runner.elf`) and exit-code behavior

## 2. Workflow Scaffolding

- [ ] 2.1 Create `.github/workflows/firmware-ci.yml`
- [ ] 2.2 Configure `on.push.paths` and `on.pull_request.paths` to `['firmware/**', '.github/workflows/firmware-ci.yml']`

## 3. Build Job

- [ ] 3.1 Add `build-firmware` job using `espressif/idf:v5.5.3` Docker container
- [ ] 3.2 Checkout repo, run `idf.py build` from `firmware/`
- [ ] 3.3 Verify job fails on a deliberately introduced compile error (temporary test branch), then revert

## 4. Unit Test Job

- [ ] 4.1 Add `test-firmware` job using the same pinned `espressif/idf:v5.5.3` container
- [ ] 4.2 Checkout repo, run `idf.py set-target linux && idf.py build` from `firmware/test_runner/`
- [ ] 4.3 Execute the resulting host binary as a separate step; let its exit code determine step success/failure
- [ ] 4.4 Verify job fails on a deliberately introduced failing assertion (temporary test branch), then revert

## 5. Verification

- [ ] 5.1 Push a branch touching only `web-companion/` or `doc/` and confirm `firmware-ci.yml` does NOT run
- [ ] 5.2 Push a branch touching `firmware/` and confirm both jobs run and pass on a known-good commit
- [ ] 5.3 Confirm both jobs complete in a reasonable time (note actual runtime for future caching decisions)

## 6. Documentation

- [ ] 6.1 Update `AGENTS.md` Gotchas: remove/revise "No CI workflows exist yet. Tests require hardware."
- [ ] 6.2 Note in `AGENTS.md` that the CI unit test job covers a subset of components (per `fix-test-runner-build`'s scope — `sdf_app`/lock-flow remain hardware-only)

## 7. Follow-Up (Out of Scope, Track Separately)

- [ ] 7.1 Enable required status checks on `main` for `build-firmware` and `test-firmware` in repo branch protection settings

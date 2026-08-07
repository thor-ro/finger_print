## 1. Prerequisite Check

- [x] 1.1 Confirm `fix-test-runner-build` is merged and `firmware/test_runner` builds/links/runs cleanly for `IDF_TARGET=linux` locally
- [x] 1.2 Confirm the `test_runner` host binary's exact output path (e.g. `build/sdf_test_runner.elf`) and exit-code behavior

## 2. Workflow Scaffolding

- [x] 2.1 Create `.github/workflows/firmware-ci.yml`
- [x] 2.2 Configure `on.push.paths` and `on.pull_request.paths` to `['firmware/**', '.github/workflows/firmware-ci.yml']`

## 3. Build Job

- [x] 3.1 Add `build-firmware` job using `espressif/idf:v6.0.2` Docker container
- [x] 3.2 Checkout repo, run `idf.py build` from `firmware/`
- [ ] 3.3 (Deferred — requires pushing a deliberately broken branch to `origin` and watching GitHub Actions; not run in this session) Verify job fails on a deliberately introduced compile error (temporary test branch), then revert

## 4. Unit Test Job

- [x] 4.1 Add `test-firmware` job using the same pinned `espressif/idf:v6.0.2` container
- [x] 4.2 Checkout repo, run `idf.py set-target linux && idf.py build` from `firmware/test_runner/`
- [x] 4.3 Execute the resulting host binary as a separate step; let its exit code determine step success/failure
- [ ] 4.4 (Deferred — requires pushing a deliberately broken branch to `origin` and watching GitHub Actions; not run in this session) Verify job fails on a deliberately introduced failing assertion (temporary test branch), then revert

## 5. Verification

- [ ] 5.1 (Deferred — requires pushing branches to `origin` and watching GitHub Actions; not run in this session) Push a branch touching only `web-companion/` or `doc/` and confirm `firmware-ci.yml` does NOT run
- [ ] 5.2 (Deferred — same as above) Push a branch touching `firmware/` and confirm both jobs run and pass on a known-good commit
- [ ] 5.3 (Deferred — same as above) Confirm both jobs complete in a reasonable time (note actual runtime for future caching decisions)

## 6. Documentation

- [x] 6.1 Update `AGENTS.md` Gotchas: remove/revise "No CI workflows exist yet. Tests require hardware."
- [x] 6.2 Note in `AGENTS.md` that the CI unit test job covers a subset of components (per `fix-test-runner-build`'s scope — `sdf_app`/lock-flow remain hardware-only)

## 7. Follow-Up (Out of Scope, Track Separately)

- [ ] 7.1 Enable required status checks on `main` for `build-firmware` and `test-firmware` in repo branch protection settings

# Spec: firmware-ci

## Purpose

Defines the CI workflow that gates `firmware/**` pushes and pull requests: what triggers it, which pinned toolchain it builds with, what its build and unit-test checks must verify, and how its jobs relate to each other.

## Requirements

### Requirement: Trigger Scope
The firmware CI workflow SHALL run on `push` and `pull_request` events that touch `firmware/**` or the workflow file itself.

#### Scenario: Firmware change triggers workflow
- **WHEN** a push or pull request modifies any file under `firmware/`
- **THEN** the firmware CI workflow runs

#### Scenario: Unrelated change does not trigger workflow
- **WHEN** a push or pull request only modifies files outside `firmware/` (e.g. `web-companion/`, `doc/`)
- **THEN** the firmware CI workflow does not run

### Requirement: Pinned Toolchain
The firmware CI workflow SHALL build using ESP-IDF v6.0.2, matching the version documented in `AGENTS.md`.

#### Scenario: Build environment version
- **WHEN** any CI job in the workflow runs `idf.py`
- **THEN** it runs inside an environment providing ESP-IDF v6.0.2

### Requirement: Firmware Build Check
The workflow SHALL build the main firmware for the `esp32c6` target and fail the check on any compile error.

#### Scenario: Successful compile
- **WHEN** `idf.py build` succeeds in `firmware/`
- **THEN** the build job reports success

#### Scenario: Compile error
- **WHEN** `idf.py build` fails in `firmware/` (e.g. syntax error, missing symbol, unresolved component)
- **THEN** the build job reports failure and the CI check is marked failed

### Requirement: Host Unit Test Check
The workflow SHALL build `firmware/test_runner` for `IDF_TARGET=linux` and execute the resulting binary, failing the check if the process exits non-zero.

#### Scenario: All tests pass
- **WHEN** the `test_runner` host binary exits with status `0`
- **THEN** the unit test job reports success

#### Scenario: A test fails
- **WHEN** the `test_runner` host binary exits with a non-zero status
- **THEN** the unit test job reports failure and the CI check is marked failed

### Requirement: Independent Parallel Jobs
The build check and the unit test check SHALL run as independent jobs so either can fail without blocking or being blocked by the other.

#### Scenario: Test failure does not block build result reporting
- **WHEN** the unit test job fails
- **THEN** the build job still reports its own result independently

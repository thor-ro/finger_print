## Context

The Smart Door Finger (SDF) v2.0 is an existing ESP32-C6 firmware project built with ESP-IDF v5.5.3. The codebase exists and is functional but lacks a formal OpenSpec specification. The project consists of 11 ESP-IDF components with well-defined boundaries (`include/` for public APIs, `src/` for internals), a test runner project for hardware-based Unity tests, and comprehensive documentation in `doc/sdf_sas.md` (software architecture) and `doc/user_manual.md`.

**Current State:**
- 11 components implemented: `sdf_app`, `sdf_services`, `sdf_protocol_ble`, `sdf_protocol_zigbee`, `sdf_drivers`, `sdf_state_machines`, `sdf_storage`, `sdf_power`, `sdf_common`, `sdf_cli`, and partially `sdf_config`/`sdf_platform` (documented but not fully separate components)
- ESP-IDF v5.5.3 with NimBLE (Central) + ESP-Zigbee (ZHA End Device) dual-stack
- Dual-build profiles: debug (verbose logs) and release (optimized)
- Encrypted NVS with `nvs_keys` partition
- Partition table: NVS (24KB), OTA_0/1 (~1.9MB each), nvs_keys (4KB), zb_storage (16KB), zb_fct (4KB)

**Constraints:**
- Single-core ESP32-C6 with FreeRTOS; single `sdf_fp` task handles all fingerprint operations
- Sensor-side template storage (fingerprint templates never leave the sensor)
- On-demand BLE radio (gated for power)
- Zigbee sleepy end device with 15s check-in interval
- No cloud backend; all logic on-device
- Tests require hardware (no CI/CD yet)

## Goals / Non-Goals

**Goals:**
- Create a complete OpenSpec specification that documents all 13 capabilities from the proposal
- Align specs with existing architecture in `doc/sdf_sas.md` and `AGENTS.md`
- Define clear component interfaces, runtime flows, and security requirements
- Enable future development, testing, and compliance verification
- Document build/test procedures and configuration

**Non-Goals:**
- Changing existing implementation (this is a documentation/specification effort)
- Implementing missing components (`sdf_platform`, `sdf_config` as separate components)
- Adding CI/CD pipeline or host-based tests
- Modifying hardware design or PCB

## Decisions

### 1. Specification Structure: One spec per capability (13 specs)

**Rationale:** The proposal identifies 13 distinct capabilities. Each maps to a coherent domain (component, cross-cutting concern, or system-level feature). Separate specs enable independent review, versioning, and compliance checking.

**Alternative considered:** Single monolithic spec — rejected because it would be unwieldy (600+ lines) and prevent granular updates when individual components change.

### 2. Spec Format: Markdown with structured sections

**Rationale:** Follows OpenSpec conventions; human-readable; diff-friendly; renderable in GitHub/GitLab.

**Structure per spec:**
- Overview (purpose, scope)
- Requirements (functional, non-functional)
- Interfaces (public API, events, configuration)
- Behavior (state machines, sequences, invariants)
- Testing (unit, integration, acceptance criteria)

### 3. Component Specs Mirror ESP-IDF Component Boundaries

**Rationale:** The existing codebase already enforces `include/` + `src/` separation per component. Specs should reflect this to maintain traceability from requirements to implementation.

**Mapping:**
| Capability | Primary Component(s) |
|------------|---------------------|
| `sdf-app` | `sdf_app` |
| `sdf-services` | `sdf_services` |
| `sdf-protocol-ble` | `sdf_protocol_ble` |
| `sdf-protocol-zigbee` | `sdf_protocol_zigbee` |
| `sdf-drivers` | `sdf_drivers` |
| `sdf-state-machines` | `sdf_state_machines` |
| `sdf-storage` | `sdf_storage` |
| `sdf-power` | `sdf_power` |
| `sdf-platform` | `sdf_platform` (partial — documented, minimal code) |
| `sdf-common` | `sdf_common` |
| `sdf-cli` | `sdf_cli` |

### 4. Cross-Cutting Specs for Security, Power, Build

**Rationale:** Security policy, power management, and build system are cross-cutting concerns that span multiple components. They need dedicated specs for compliance auditing.

### 5. Enrollment Flow Spec Separate from Component Specs

**Rationale:** Enrollment is a user-facing workflow that spans `sdf_app`, `sdf_services`, `sdf_state_machines`, `sdf_drivers`, and `sdf_protocol_zigbee`. A dedicated spec captures the end-to-end behavior and acceptance criteria.

### 6. Reference Existing Documentation as Authoritative Source

**Rationale:** `doc/sdf_sas.md` (630 lines) and `doc/user_manual.md` already contain detailed architecture, runtime views, and user flows. Specs will reference and align with these rather than duplicating content.

### 7. Kconfig Options Documented in Respective Component Specs

**Rationale:** Configuration is component-local (each component has `Kconfig`). Security defaults in `sdkconfig.defaults` map to `security-policy` spec.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| **Spec drift from implementation** | Documentation sync rule in AGENTS.md mandates updates on architectural changes; specs reference `doc/sdf_sas.md` sections |
| **Missing `sdf_platform`/`sdf_config` components** | Documented as "partial" in proposal; specs note current inline implementation in `sdkconfig.defaults` and `sdf_common`/`sdf_drivers` |
| **Hardware-dependent tests can't run in CI** | Specs include acceptance criteria verifiable on hardware; test runner project exists for manual execution |
| **Placeholder Nuki BLE address** | Specs document the requirement; `AGENTS.md` has critical setup step |
| **Fingerprint LED command variant-specific** | Specs note this as implementation detail in `sdf-drivers`/`sdf-services`; defaults in `sdf_services.c` |

## Migration Plan

Not applicable — this is a greenfield specification effort for an existing codebase. No deployment or rollback needed.

## Open Questions

1. **Should `sdf_platform` and `sdf_config` be split into separate components?** Currently documented in AGENTS.md but implementation is inline. Decision affects spec boundaries.

2. **How to handle OTA updates?** Partition table supports dual OTA slots but OTA logic is not implemented (listed as technical debt in `sdf_sas.md`). Should a spec be created?

3. **Factory reset incomplete** — `sdf_app_on_admin_action(FACTORY_RESET)` has TODO. Should this be a requirement in `sdf-app` spec?

4. **Zigbee check-in interval trade-off** — 15s default balances battery vs. latency. Should this be a configurable Kconfig with documented bounds?

5. **LED command byte tuning** — `Control LED (0x3C)` payload varies by fingerprint module variant. Spec should capture this as a configuration parameter.
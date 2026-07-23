## 1. Specification Review and Validation

- [ ] 1.1 Review proposal.md against existing docs (sdf_sas.md, user_manual.md, AGENTS.md)
- [ ] 1.2 Review design.md decisions with project stakeholders
- [ ] 1.3 Validate all 13 specs against codebase implementation
- [ ] 1.4 Check consistency across specs (shared types, enums, error codes)

## 2. Spec Archive and Integration

- [ ] 2.1 Archive proposal.md to change root
- [ ] 2.2 Archive design.md to change root
- [ ] 2.3 Archive all 13 spec files to openspec/specs/
- [ ] 2.4 Run openspec validate to verify spec completeness

## 3. Documentation Alignment

- [ ] 3.1 Update doc/sdf_sas.md with spec references (sections 5, 6, 8, 9)
- [ ] 3.2 Create cross-reference table in doc/software-architecture.md
- [ ] 3.3 Add OpenSpec spec status section to AGENTS.md
- [ ] 3.4 Verify security defaults in sdkconfig.defaults match security-policy spec

## 4. Test Runner Verification

- [ ] 4.1 Verify all component test directories exist (sdf_app, sdf_services, sdf_protocol_ble, etc.)
- [ ] 4.2 Check test_runner CMakeLists.txt links all components
- [ ] 4.3 Verify Kconfig files present in all component directories
- [ ] 4.4 Document any missing components (sdf_platform, sdf_config - marked as partial in design)

## 5. Gap Analysis

- [ ] 5.1 Document factory reset TODO in sdf_app (technical debt)
- [ ] 5.2 Document OTA update missing (marked in risks)
- [ ] 5.3 Document fingerprint LED command tuning requirement
- [ ] 5.4 Create issue list for incomplete features discovered during spec review

---

*This specification captures the existing SDF v2.0 architecture into the OpenSpec format. No code changes required - this is a documentation effort.*
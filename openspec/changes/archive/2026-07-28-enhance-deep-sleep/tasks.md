## 1. Wake Source API

- [x] 1.1 Add `sdf_wake_source_t` enum and `sdf_power_wake_config_t` struct to sdf_power.h
- [x] 1.2 Add wake config getter/setter APIs to sdf_power.h
- [x] 1.3 Add `sdf_power_prepare_deep_sleep()` and `sdf_power_resume_from_deep_sleep()` to sdf_power.h

## 2. Retention Memory API

- [x] 2.1 Add `sdf_power_retention_t` struct to sdf_power.h
- [x] 2.2 Add retention save/load/valid APIs to sdf_power.h
- [x] 2.3 Extend sdf_platform_sleep.h with retention read/write functions

## 3. Platform Sleep Extensions

- [x] 3.1 Add wake source configuration function to sdf_platform_sleep.h
- [x] 3.2 Implement retention memory functions in sdf_platform_sleep.c
- [x] 3.3 Implement wake source configuration in sdf_platform_sleep.c

## 4. Power Manager Implementation

- [x] 4.1 Add wake source selection logic in sdf_power.c
- [x] 4.2 Add adaptive check-in interval calculation function
- [x] 4.3 Modify sdf_power_task for staged wake sequence
- [x] 4.4 Add retention save/load calls in deep sleep path

## 5. Configuration

- [x] 5.1 Add Kconfig options for wake sources, retention size, adaptive check-in
- [x] 5.2 Update sdf_config.c with new config defaults

## 6. Documentation

- [x] 6.1 Update sdf_sas.md with new power management APIs
- [x] 6.2 Update AGENTS.md with any build/behavior changes if applicable
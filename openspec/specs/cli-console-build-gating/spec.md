# CLI Console Build Gating

## Purpose
Defines the build-time Kconfig option controlling whether the debug UART/USB-JTAG console (`sdf_cli`) is compiled into the firmware, and its default per build profile.

## Requirements

### Requirement: CLI console is build-time optional
The system SHALL provide a Kconfig option controlling whether the debug UART/USB-JTAG console's command handling is compiled into the firmware. When disabled, the console's dependencies (`esp_console`, `argtable3`, `linenoise`) SHALL NOT be linked into the resulting binary, and `sdf_cli`'s public API SHALL fall back to a no-op stub so callers (`sdf_app`) do not need to know whether the console is built in.

#### Scenario: Console compiled in when option enabled
- **WHEN** the firmware is built with the CLI console option enabled
- **THEN** the binary includes the real `sdf_cli` command handling and its authenticated command behavior is unchanged from before this change

#### Scenario: Console excluded when option disabled
- **WHEN** the firmware is built with the CLI console option disabled
- **THEN** `sdf_app` builds and links successfully, calling `sdf_cli_init()` as a no-op that returns success
- **AND** the resulting binary contains no `esp_console`/`argtable3`/`linenoise` symbols
- **AND** the resulting binary is smaller by the size of the console command handling and its dependencies

### Requirement: Build profile defaults
The system SHALL default the CLI console option to enabled for the debug build profile and to disabled for the release/production build profile.

#### Scenario: Debug profile default
- **WHEN** the firmware is built using the debug sdkconfig profile without overriding the CLI console option
- **THEN** the CLI console is enabled

#### Scenario: Release profile default
- **WHEN** the firmware is built using the release sdkconfig profile (or the base defaults) without overriding the CLI console option
- **THEN** the CLI console is disabled

## ADDED Requirements

### Requirement: ESP-IDF Version Pinning
The build system SHALL pin to ESP-IDF v5.5.3.

#### Scenario: Version constraint
- **WHEN** Building firmware
- **THEN** `idf.py` must run with ESP-IDF v5.5.3 sourced
- **THEN** `source /Users/thorstenropertz/.espressif/v5.5.3/esp-idf/export.sh` required

### Requirement: Build Profiles
The build system SHALL support debug and release profiles via SDKCONFIG_DEFAULTS.

#### Scenario: Debug profile
- **WHEN** `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" build`
- **THEN** `CONFIG_LOG_DEFAULT_LEVEL=4` (DEBUG)
- **THEN** `CONFIG_SDF_APP_DEBUG_LOCK_FLOW=1`
- **THEN** `CONFIG_SDF_SERVICES_DEBUG_MATCH_CYCLE=1`
- **THEN** `CONFIG_SDF_PROTOCOL_BLE_DEBUG=1`
- **THEN** `CONFIG_SDF_PROTOCOL_ZIGBEE_DEBUG=1`
- **THEN** Optimizations disabled (`-O0`)

#### Scenario: Release profile
- **WHEN** `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release.defaults" build`
- **THEN** `CONFIG_LOG_DEFAULT_LEVEL=2` (WARN)
- **THEN** `CONFIG_SDF_APP_DEBUG_LOCK_FLOW=0`
- **THEN** Optimizations enabled (`-Os`)
- **THEN** `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`

### Requirement: Default Configuration
The build system SHALL provide a base `sdkconfig.defaults` with all standard settings.

#### Scenario: Base defaults
- **WHEN** `sdkconfig.defaults` applied
- **THEN** Target: ESP32-C6 (`CONFIG_IDF_TARGET_ESP32C6=y`)
- **THEN** Zigbee enabled: `CONFIG_ZIGBEE_ENABLED=y`, ZHA profile
- **THEN** NimBLE enabled: `CONFIG_BT_NIMBLE_ENABLED=y`, Central role
- **THEN** NVS encryption: `CONFIG_NVS_ENCRYPTION=y`
- **THEN** Partition table: `partition_table.csv`
- **THEN** Security defaults (see security-policy spec)
- **THEN** Power defaults (see sdf-power spec)

### Requirement: Partition Table
The build system SHALL use a custom partition table with encrypted NVS support.

#### Scenario: Partition layout
- **WHEN** `partition_table.csv` used
- **THEN** nvs: 0x9000, 24KB
- **THEN** phy_init: 0xF000, 4KB
- **THEN** otadata: 0x10000, 8KB
- **THEN** nvs_keys: 0x12000, 4KB (REQUIRED for NVS encryption)
- **THEN** ota_0: 0x20000, ~1.9MB
- **THEN** ota_1: 0x210000, ~1.9MB
- **THEN** zb_storage: 0x3FB000, 16KB
- **THEN** zb_fct: 0x3FF000, 4KB

### Requirement: Component CMakeLists
Each component SHALL have a CMakeLists.txt defining its sources, includes, and dependencies.

#### Scenario: Component structure
- **WHEN** Component CMakeLists.txt parsed
- **THEN** `idf_component_register(SRCS "src/*.c" INCLUDE_DIRS "include" REQUIRES ...)`
- **THEN** Public API in `include/`, private in `src/`
- **THEN** Mock sources excluded from target build (`src/*_mock_linux.c`)

### Requirement: Test Runner Project
The build system SHALL provide a `test_runner` project that links all components for Unity testing on hardware.

#### Scenario: Test runner build
- **WHEN** `cd firmware/test_runner && idf.py build`
- **THEN** Links all SDF components + Unity test framework
- **THEN** Includes test files from each component's `test/` directory
- **THEN** Produces test firmware for hardware execution

#### Scenario: Test execution
- **WHEN** `idf.py -p <PORT> flash monitor` on test_runner
- **THEN** Interactive menu shows available tests
- **THEN** Run individual tests or all tests
- **THEN** Results printed via Unity output

### Requirement: Flash and Monitor
The build system SHALL support flashing and monitoring via `idf.py`.

#### Scenario: Flash production firmware
- **WHEN** `cd firmware && idf.py -p <PORT> flash monitor`
- **THEN** Builds, flashes, opens serial monitor
- **THEN** Logs show boot, initialization, runtime events

### Requirement: Kconfig Options
Each component SHALL expose configuration via Kconfig.

#### Scenario: Component Kconfig
- **WHEN** `menuconfig` run
- **THEN** Component options under "Smart Door Finger Configuration"
- **THEN** Security: `CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_THRESHOLD`, `CONFIG_SDF_SECURITY_BIOMETRIC_FAIL_WINDOW_MS`, `CONFIG_SDF_SECURITY_BIOMETRIC_LOCKOUT_MS`, `CONFIG_SDF_NUKI_NONCE_WINDOW`
- **THEN** Power: `CONFIG_SDF_POWER_CHECKIN_INTERVAL_MS`, `CONFIG_SDF_POWER_IDLE_BEFORE_SLEEP_MS`, `CONFIG_SDF_POWER_POST_WAKE_GUARD_MS`, `CONFIG_SDF_POWER_LOOP_INTERVAL_MS`, `CONFIG_SDF_POWER_BATTERY_REPORT_INTERVAL_MS`, `CONFIG_SDF_POWER_ENABLE_LIGHT_SLEEP`, `CONFIG_SDF_POWER_ENABLE_BLE_RADIO_GATING`
- **THEN** App: `CONFIG_SDF_APP_LOCK_ACTION_MAX_RETRIES`, `CONFIG_SDF_APP_DEBUG_LOCK_FLOW`
- **THEN** Services: `CONFIG_SDF_SERVICES_FP_TASK_STACK_SIZE`, `CONFIG_SDF_SERVICES_FP_TASK_PRIORITY`, `CONFIG_SDF_SERVICES_DEBUG_MATCH_CYCLE`
- **THEN** Drivers: `CONFIG_SDF_DRIVERS_FP_UART_TIMEOUT_MS`, `CONFIG_SDF_DRIVERS_FP_POWER_ON_DELAY_MS`, `CONFIG_SDF_DRIVERS_MOCK`
- **THEN** BLE: `CONFIG_SDF_PROTOCOL_BLE_DEBUG`
- **THEN** Zigbee: `CONFIG_SDF_PROTOCOL_ZIGBEE_DEBUG`

### Requirement: Documentation Sync
The build system SHALL enforce documentation sync via AGENTS.md rules.

#### Scenario: Doc update triggers
- **WHEN** Component boundaries changed (new/removed/renamed)
- **THEN** Update `doc/sdf_sas.md` sections 5, 6, 8, 9
- **WHEN** Public API changed
- **THEN** Update `doc/sdf_sas.md` sections 5, 6, 8, 9
- **WHEN** User-facing behavior changed
- **THEN** Update `doc/user_manual.md`
- **WHEN** Build commands, component list, security defaults changed
- **THEN** Update `AGENTS.md`
- **WHEN** Version history
- **THEN** Update `version.md`

### Requirement: Critical Setup Steps
The build system SHALL document critical manual setup steps.

#### Scenario: Nuki BLE address
- **WHEN** First deployment
- **THEN** Must set `SDF_NUKI_TARGET_ADDR_TYPE` and `SDF_NUKI_TARGET_ADDR` in `sdf_app.c`
- **THEN** Address format: little-endian (AA:BB:CC:DD:EE:FF → {0xFF,0xEE,0xDD,0xCC,0xBB,0xAA})
- **THEN** All zeros = discovery mode (pairing only)
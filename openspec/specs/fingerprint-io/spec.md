# Fingerprint IO

## Purpose
Governs how concurrent requests to the fingerprint sensor (match, enrollment, admin actions, CLI, factory reset) are serialized so that legitimate overlapping use completes correctly instead of failing due to lock contention.

## Requirements

### Requirement: Fingerprint Operations Serialize Instead of Failing Under Contention
The system SHALL serialize concurrent fingerprint sensor operations issued by different callers so that an operation completes - with a genuine success or a sensor-reported error - rather than being rejected solely because another operation was already using the sensor within the sensor's normal response-time budget.

#### Scenario: Two callers issue operations that overlap
- **WHEN** one caller's fingerprint operation is in progress and a second caller issues another fingerprint operation
- **THEN** the second operation waits for the first to finish before it begins, rather than immediately erroring out because the sensor is busy

#### Scenario: Caller wait bound matches the sensor response budget
- **WHEN** a caller is waiting behind an in-progress operation
- **THEN** the caller only reports a timeout error after waiting at least the sensor's configured response timeout for its own operation to run, not a fixed short window unrelated to that budget

### Requirement: Sensor Power State Does Not Change Mid-Operation
The system SHALL NOT change fingerprint sensor power state while a fingerprint sensor I/O operation is in progress.

#### Scenario: Power-off requested during an active operation
- **WHEN** a task requests the sensor be powered off while a match, enrollment, or admin operation is still in flight
- **THEN** the power-off is applied only after the in-flight operation completes

#### Scenario: Power-on requested during an active operation
- **WHEN** a task requests the sensor be powered on while another in-flight operation is still completing
- **THEN** the power state change is applied only after that in-flight operation completes, avoiding a torn power transition

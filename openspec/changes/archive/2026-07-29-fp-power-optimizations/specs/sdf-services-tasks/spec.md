## ADDED Requirements

### Requirement: Interrupt-driven Match Task
The `sdf_match_task` SHALL rely on the GPIO WAKE interrupt from the fingerprint sensor rather than polling continuously when the device is awake. The task SHALL block indefinitely until a wake event or match request is received.

#### Scenario: Device wakes from interrupt
- **WHEN** the fingerprint sensor asserts the WAKE GPIO pin
- **THEN** an interrupt is triggered which emits a match request event
- **AND** the `sdf_match_task` initiates a match cycle

#### Scenario: Match task idle
- **WHEN** there is no active match request and the WAKE pin is not asserted
- **THEN** the `sdf_match_task` blocks indefinitely, avoiding periodic 400ms wakeups

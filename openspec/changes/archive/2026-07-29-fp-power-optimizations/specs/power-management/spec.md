## ADDED Requirements

### Requirement: Fingerprint UART Baud Rate
The system SHALL configure the fingerprint UART interface to operate at 115200 baud to minimize transaction time and active CPU duty cycle.

#### Scenario: System initialization
- **WHEN** the system initializes the `sdf_drivers` fingerprint UART
- **THEN** it configures the baud rate to 115200 instead of 19200

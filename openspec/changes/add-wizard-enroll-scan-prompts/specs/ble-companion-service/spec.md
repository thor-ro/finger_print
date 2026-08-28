## ADDED Requirements

### Requirement: Enrolment Progress Is Notified Per Scan

While an enrolment is in flight, the system SHALL notify the Enrollment characteristic each time the set of captured scans changes, so a client can tell the user which scan to perform without inferring it from elapsed time. Each progress notification SHALL carry the number of scans captured so far, the scan now expected, and the total number of scans the enrolment requires.

Progress notifications SHALL be distinguishable from terminal replies and from the terminal success/failure notifications, so a client never mistakes progress for an outcome.

Progress notifications SHALL carry the request id of the request that started the enrolment, and SHALL NOT consume it: the terminal reply for that request is still owed and SHALL carry the same id.

Progress SHALL be notified only while the enrolment is in progress. A failed start SHALL NOT be reported as progress; it is already reported by its terminal reply.

Progress notifications SHALL be delivered to setup-phase connections as well as to authenticated ones, because the first Admin enrolment — the one the setup wizard drives — happens before any account exists to authenticate with.

#### Scenario: Each captured scan is notified

- **WHEN** a scan of an in-flight enrolment succeeds and another scan is required
- **THEN** the client is notified with the captured count, the expected scan, and the total

#### Scenario: Enrolment start prompts the first scan

- **WHEN** an enrolment starts and the first scan is expected
- **THEN** the client is notified with zero scans captured and the first scan expected

#### Scenario: Progress does not consume the request id

- **WHEN** a progress notification carrying a request id is delivered
- **THEN** the terminal reply for that request is still delivered afterwards, carrying the same id

#### Scenario: Progress is not confused with an outcome

- **WHEN** a client receives a progress notification
- **THEN** it is marked as progress and is distinguishable from the success and failure notifications

#### Scenario: A failed start is not reported as progress

- **WHEN** an enrolment cannot be started
- **THEN** no progress notification is sent
- **AND** the failure is reported by the request's terminal reply

#### Scenario: The setup wizard receives progress

- **WHEN** a setup-phase connection with no account starts the first Admin enrolment
- **THEN** it receives the progress notifications for that enrolment

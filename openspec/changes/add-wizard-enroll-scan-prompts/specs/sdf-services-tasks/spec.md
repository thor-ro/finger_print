## ADDED Requirements

### Requirement: Enrolment Progress Is Observable Per Scan

The enrol task SHALL announce enrolment progress on the event router each time the enrolment state machine advances to a new scan, carrying the number of scans captured so far and the state the machine has advanced to. A client that observes only the start and the terminal outcome SHALL NOT be the best a subscriber can do: the intermediate scans SHALL be observable as they happen.

The announcement SHALL be emitted after the services state lock is released, consistent with the existing rule that the task does not emit while holding it.

#### Scenario: Successful scan announces the next one

- **WHEN** a scan succeeds and the state machine advances to the next scan
- **THEN** an enrolment step event is emitted carrying the captured-scan count and the newly entered state
- **AND** it is emitted outside the services state lock

#### Scenario: Retried scan does not announce progress

- **WHEN** a scan fails in a way that retries the same step
- **THEN** no progress is announced, because no scan was captured

#### Scenario: Terminal outcomes are not announced as progress

- **WHEN** the enrolment completes or fails
- **THEN** the outcome is announced by the existing complete/failed events, not as a further step advance

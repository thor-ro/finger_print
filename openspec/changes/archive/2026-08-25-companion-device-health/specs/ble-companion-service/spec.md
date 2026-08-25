## ADDED Requirements

### Requirement: Device Status Characteristic

The system SHALL expose a Status characteristic on the Companion Service supporting read and notify, carrying the device health report defined by the `companion-device-health` capability.

The Status characteristic SHALL be a restricted characteristic: until a LOGIN challenge has been successfully verified on the Authentication characteristic, a read of Status SHALL return an insufficient authentication error, in the same way as Config, Enrollment and OTA.

Unlike Config, Enrollment and OTA, Status SHALL NOT additionally require the connection's bound user to hold admin permission. Any authenticated connection SHALL be able to read it, at any permission level, because the report carries no secret material and no ability to change anything.

A connection whose bound user has been deleted SHALL lose access to Status along with its authentication, per `BLE GATT Authentication`.

Notification subscription state (CCCD) is owned by the BLE stack and SHALL NOT be treated as the point where admission is decided: a client may hold a subscription without being entitled to anything. Admission SHALL instead be enforced at delivery. The system SHALL NOT send a Status notification to a connection that is not authenticated, or whose bound user is no longer enrolled, whatever that connection's subscription state. Admission SHALL be re-evaluated per connection for every notification, never captured at the moment a subscription was made.

#### Scenario: Unauthenticated read refused
- **WHEN** an unauthenticated client reads the Status characteristic
- **THEN** system returns an insufficient authentication error

#### Scenario: Unauthenticated subscriber receives no report
- **WHEN** an unauthenticated client holds a Status notification subscription and a reported value changes
- **THEN** system sends that connection no notification and no change indication
- **AND** the client learns nothing about the device state from its subscription

#### Scenario: Authority lost after subscribing stops delivery
- **WHEN** a subscribed connection loses its authentication, or the user it is bound to is deleted, and a reported value afterwards changes
- **THEN** system sends that connection no further notifications
- **AND** the still-open subscription does not keep the report flowing

#### Scenario: Non-admin authenticated read permitted
- **WHEN** an authenticated connection whose bound user holds standard permission reads Status
- **THEN** system returns the health report

#### Scenario: Admin read returns the same report
- **WHEN** an authenticated admin connection reads Status
- **THEN** system returns the same report a standard user receives

#### Scenario: Deleted user loses status access
- **WHEN** the user bound to an open authenticated connection is deleted
- **THEN** subsequent reads of Status on that connection are refused

### Requirement: Status Access Never Blocks The BLE Host Task

A read of the Status characteristic SHALL be served without waiting on any other task, semaphore held across I/O, sensor operation or bus transaction. The access callback SHALL serialize recorded state and return. Producing a notification SHALL be subject to the same rule.

The system SHALL NOT satisfy this by returning an error under contention where a value is available; it SHALL be structured so that the value is available without waiting.

#### Scenario: Read returns without waiting
- **WHEN** a client reads Status
- **THEN** the access callback serializes recorded state and returns
- **AND** it does not wait on another task to produce the value

#### Scenario: Read during a long-running operation still returns
- **WHEN** a client reads Status while an enrolment, a lock action or an OTA transfer is in progress
- **THEN** the read returns the current report without waiting for that operation

### Requirement: Status Notifications Are Coalesced And Never Truncated

When several reported values change in quick succession, the system SHALL notify the latest report rather than one notification per change, following the coalescing rule `zigbee-attribute-reporting — Attribute updates are coalesced to the latest value` states for the other transport.

A notification SHALL NOT carry a partial report. Where the report does not fit a single notification at the connection's negotiated MTU, the system SHALL notify a change indication that carries no truncated data, and the client SHALL obtain the full report with a read, which is not bounded by the MTU.

The system SHALL NOT drop a change silently: every change SHALL result in either an updated report or a change indication reaching a subscribed connection.

#### Scenario: Burst of changes coalesced
- **WHEN** several reported values change within a short interval
- **THEN** a subscribed client receives the latest report
- **AND** it does not receive one notification per intermediate value

#### Scenario: Oversized report is indicated, not truncated
- **WHEN** the report does not fit a single notification at the negotiated MTU
- **THEN** system notifies a change indication carrying no partial report
- **AND** a subsequent read returns the full report

#### Scenario: No change is silently dropped
- **WHEN** a reported value changes while a client is subscribed
- **THEN** the client receives either the updated report or a change indication

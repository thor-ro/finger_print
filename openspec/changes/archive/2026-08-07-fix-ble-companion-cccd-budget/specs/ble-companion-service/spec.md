## ADDED Requirements

### Requirement: Persisted Notification Subscription Capacity
The system SHALL size its persisted notification-subscription (CCCD) storage capacity to cover the worst case of every bonded peer subscribing to every NOTIFY-capable characteristic exposed by the Companion Service. The configured CCCD capacity SHALL be greater than or equal to the product of the maximum number of bonded peers and the number of NOTIFY-capable characteristics.

#### Scenario: All bonded peers subscribed to all notify characteristics
- **WHEN** the maximum number of bonded peers are each subscribed to every NOTIFY-capable characteristic
- **THEN** every subscription persists successfully across reconnects
- **AND** no persisted subscription is silently dropped due to exhausted CCCD storage capacity

#### Scenario: Adding a NOTIFY-capable characteristic requires capacity review
- **WHEN** a new NOTIFY-capable characteristic is added to the Companion Service's GATT database
- **THEN** the persisted CCCD capacity SHALL be re-verified to still cover the updated bonded-peer × NOTIFY-characteristic product

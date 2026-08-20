# Zigbee Attribute Reporting

## ADDED Requirements

### Requirement: The caller-side alarm mask is composed atomically

The alarm mask is not written wholesale by its callers: each call site sets or clears a subset of bits, so producing the value handed to the Zigbee component is a read-modify-write over state shared by every producer. Producers run on at least three contexts — the application component's task, the BLE host task, and the lock-flow callbacks — with no ordering between them.

The system SHALL make that read-modify-write atomic with respect to concurrent producers, so that a set or clear of one alarm bit SHALL NOT discard a concurrent set or clear of a different bit. The operation SHALL remain non-blocking and SHALL be safe to call from a BLE host callback and from a lock-flow callback, neither of which may wait.

This matters more than a transient inconsistency would suggest: the Zigbee component coalesces recorded values to the latest one rather than replaying superseded intermediates, so a lost update is not corrected by the next write. It persists as a wrong alarm mask — a raised alarm silently dropped, or a cleared alarm left latched — until an unrelated update happens to rewrite that bit.

#### Scenario: Concurrent set and clear of different bits

- **WHEN** two contexts concurrently update the alarm mask, one setting bit A and the other clearing bit B
- **THEN** the resulting mask has bit A set and bit B clear
- **AND** neither update is lost to an interleaved read-modify-write

#### Scenario: A raised alarm is not lost to a concurrent update

- **WHEN** an alarm bit is raised at the same time as another context updates a different bit
- **THEN** the raised alarm is present in the value recorded for the Zigbee attribute
- **AND** it remains present until it is explicitly cleared, rather than being dropped and left to a later unrelated update to restore

#### Scenario: Update from a non-blocking context

- **WHEN** the alarm mask is updated from a BLE host callback or a lock-flow callback
- **THEN** the update completes without the calling context waiting on a lock held by another producer

#### Scenario: Redundant update does not signal the Zigbee component

- **WHEN** an alarm mask update resolves to a value identical to the current mask
- **THEN** no attribute update is recorded and no Zigbee application work is scheduled
- **AND** this suppression is decided on the same atomic operation that composed the value, so a concurrent producer cannot cause a real change to be suppressed

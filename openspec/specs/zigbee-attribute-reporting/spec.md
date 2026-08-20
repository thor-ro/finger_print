# Zigbee Attribute Reporting

## Purpose

Defines how firmware state — door lock state, battery level, alarm mask, and the active-user list — reaches the device's Zigbee ZCL attributes, so that a Zigbee home-automation central sees current state without any caller ever blocking on the Zigbee stack lock.

## Requirements

### Requirement: Attribute updates never block the calling task on the Zigbee stack

The public attribute-update functions of the Zigbee protocol component SHALL NOT invoke any Zigbee SDK API on the calling task's context, and SHALL NOT wait on the Zigbee stack lock. Each call SHALL record the new value in component-owned state and return promptly, bounded only by the component's own short-lived internal state mutex. Application of the recorded value to the ZCL attribute SHALL happen asynchronously on a task owned by the Zigbee protocol component.

This requirement exists because the dominant caller runs on the BLE host task, where a stack-lock wait of up to one second inside a GATT notify callback risks dropping the connection.

#### Scenario: Update from a foreign task returns without acquiring the stack lock

- **WHEN** any task other than the Zigbee component's own tasks calls an attribute-update function while the Zigbee stack is started
- **THEN** the call returns without having acquired the Zigbee stack lock
- **AND** the value is applied to the ZCL attribute subsequently, on a task owned by the Zigbee protocol component

#### Scenario: Update while the Zigbee stack lock is held by another task

- **WHEN** an attribute-update function is called while the Zigbee stack lock is held elsewhere for an extended period
- **THEN** the call still returns promptly rather than waiting for that lock to be released
- **AND** the recorded value is applied once the lock becomes available

#### Scenario: Update before the stack has started

- **WHEN** an attribute-update function is called before the Zigbee stack has started
- **THEN** the value is recorded and the call reports success
- **AND** the recorded value is applied to the ZCL attribute once the stack starts, without the caller repeating the update

### Requirement: No call path holds the component state mutex across a Zigbee SDK call

The Zigbee protocol component SHALL NOT hold its internal state mutex while acquiring the Zigbee stack lock or while calling any Zigbee SDK API. Inbound ZCL command handling runs as a Zigbee callback with the stack lock already held and takes the component state mutex; permitting the reverse order anywhere would create an AB-BA lock-order inversion between inbound commands and outbound attribute updates.

#### Scenario: Outbound attribute application does not nest the locks

- **WHEN** the component applies a cached value to a ZCL attribute
- **THEN** it reads the cached value under the state mutex, releases that mutex, and only then acquires the Zigbee stack lock

#### Scenario: Inbound command and outbound update run concurrently

- **WHEN** an inbound ZCL command is being dispatched — holding the stack lock and taking the state mutex — at the same time as an attribute update is applied
- **THEN** neither operation waits on a lock held by the other in the opposite order
- **AND** both complete without a lock timeout

### Requirement: Attribute updates are coalesced to the latest value

Application of recorded values SHALL push the most recently recorded value for each attribute. When multiple updates to the same attribute are recorded before application occurs, the component SHALL apply only the latest value and SHALL NOT be required to emit an attribute report for each superseded intermediate value.

#### Scenario: Rapid successive updates collapse

- **WHEN** several updates to the same attribute are recorded in quick succession, faster than they can be applied
- **THEN** the attribute converges to the value from the most recent update
- **AND** no superseded intermediate value is left as the final attribute value

#### Scenario: Distinct attributes updated in the same burst are all applied

- **WHEN** lock state, battery level, and alarm mask are each updated before application occurs
- **THEN** all three ZCL attributes reflect their respective latest recorded values

### Requirement: Update calls report argument and lifecycle errors synchronously, write failures asynchronously

An attribute-update call SHALL synchronously reject an out-of-range or otherwise invalid argument, and SHALL synchronously report when the component is not in a state that can accept updates. A successful return SHALL mean the update was accepted for application, NOT that the ZCL attribute write has completed. Failure of the underlying ZCL attribute write SHALL be logged by the component and SHALL NOT be reported through the originating call's return value.

#### Scenario: Invalid argument is rejected synchronously

- **WHEN** an attribute-update function is called with a value outside its permitted range
- **THEN** it returns an invalid-argument error synchronously
- **AND** no value is recorded and no application is scheduled

#### Scenario: Successful return does not assert the write completed

- **WHEN** an attribute-update function returns success
- **THEN** the caller may rely only on the update having been accepted for application
- **AND** the caller is not required to inspect the return value to detect a ZCL write failure

#### Scenario: ZCL write failure is surfaced in logs

- **WHEN** applying a recorded value to a ZCL attribute fails
- **THEN** the component logs the failing cluster and attribute
- **AND** the originating caller has already returned success and is not notified

### Requirement: Updates are disabled cleanly when Zigbee is disabled

When Zigbee functionality is disabled by configuration, attribute-update calls SHALL succeed as no-ops without creating or signalling any application task, so that callers need not branch on whether Zigbee is enabled.

#### Scenario: Update with Zigbee disabled

- **WHEN** an attribute-update function is called and Zigbee is disabled by configuration
- **THEN** it returns success without recording state for application and without signalling any task

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

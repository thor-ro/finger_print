# zigbee-commissioning

## ADDED Requirements

### Requirement: Commissioning State Is Recorded On Every Path That Reaches A Network
The Zigbee protocol layer SHALL record that the device is on a network on every startup path that results in network membership, not only on the path that performs steering. A device that was commissioned before a reboot rejoins its existing network without steering, so no steering signal is raised for it; the reboot path is therefore the only opportunity to record its membership.

Readiness (`sdf_protocol_zigbee_is_ready()`) gates whether lock state is reported upstream. A startup path that reaches a network but does not record it leaves the device permanently unready and silent, which is indistinguishable from a radio fault.

#### Scenario: Device reboots onto an existing network
- **WHEN** the stack reports startup with a non-factory-new device, so no steering is performed and no steering signal is raised
- **THEN** the protocol layer records the device as network-joined, and the device reports lock state upstream for the rest of the boot

#### Scenario: Factory-new device completes steering
- **WHEN** a factory-new device starts, performs network steering, and the steering signal reports success
- **THEN** the protocol layer records the device as network-joined on the steering path

#### Scenario: Factory-new device has not yet joined
- **WHEN** a factory-new device has started steering but steering has not yet succeeded
- **THEN** the device is not reported as ready, and it does not claim network membership it does not have

### Requirement: Failed Network Steering Backs Off Geometrically To A Ceiling
When network steering fails, the protocol layer SHALL retry with a delay that grows geometrically from an initial interval to a bounded ceiling, rather than retrying at a fixed short interval indefinitely. The backoff SHALL reset whenever steering is started fresh and whenever a join succeeds, so a device that recovers is not penalised by its earlier failures.

Each steering attempt is a full all-channel active scan, the most power-expensive radio operation the device performs. A lock out of range of its coordinator would otherwise scan continuously for as long as its battery lasts.

#### Scenario: Steering fails repeatedly
- **WHEN** network steering fails several times in a row
- **THEN** each retry is scheduled after a longer delay than the previous one, up to a fixed ceiling, and the delay never grows beyond that ceiling

#### Scenario: Steering succeeds after earlier failures
- **WHEN** steering succeeds after one or more failures have already lengthened the retry delay
- **THEN** the backoff is reset, so a later disconnection retries promptly rather than at the previously reached delay

#### Scenario: Retry delay is observable
- **WHEN** a steering attempt fails and a retry is scheduled
- **THEN** the log records the delay being used, so the backoff progression can be confirmed from a boot log without a debugger

## ADDED Requirements

### Requirement: Biometric Lockout Survives Reset And Deep Sleep

Biometric lockout state SHALL be durable. When the match task enters a lockout it SHALL persist that fact to NVS, and when the lockout is cleared — by expiry or by a successful match — it SHALL persist the cleared state. Persistence SHALL occur at those two transitions only, not on each failed attempt, so that a lockout episode costs a bounded number of flash writes and cannot be turned into a flash-wear denial of service by an attacker pacing failed scans.

Persistence SHALL NOT be performed while holding the services state lock, consistent with the existing rule that blocking I/O is never issued under that lock.

A failure to persist SHALL be logged and SHALL NOT abort the match cycle: a lockout that cannot be written still applies for the current boot.

The persisted record SHALL be erased by `sdf_storage_erase_all()`, so a factory reset clears an outstanding lockout.

#### Scenario: Lockout entered is persisted

- **WHEN** the failed-attempt count reaches the configured threshold and a lockout is entered
- **THEN** the lockout state is persisted to NVS
- **AND** the write happens after the services state lock is released

#### Scenario: Lockout cleared by expiry is persisted

- **WHEN** an armed lockout's deadline passes and the match task clears it
- **THEN** the cleared state is persisted to NVS

#### Scenario: Lockout cleared by a successful match is persisted

- **WHEN** a fingerprint match succeeds
- **THEN** the failed-attempt counter is reset and the cleared state is persisted to NVS

#### Scenario: Failed persistence does not break matching

- **WHEN** the NVS write fails while entering a lockout
- **THEN** the failure is logged
- **AND** the lockout is still enforced for the remainder of the current boot

### Requirement: Reboot During Lockout Re-Arms Rather Than Clears

On initialisation, when the persisted record reports an armed lockout, the match task SHALL refuse matching for a full `lockout_duration_ms` measured from boot, and SHALL NOT attempt to compute the remaining time from the previous boot. Elapsed wall-clock time is not recoverable across a power loss: there is no battery-backed RTC and `esp_timer_get_time()` restarts at zero, so a persisted deadline would always compare as expired.

The restore SHALL happen before the match task is created, so that no scan can be served in the window between initialisation and the lockout taking effect.

A restored lockout SHALL emit `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` at CRITICAL priority, and its expiry SHALL emit the NORMAL clear, preserving the paired emission that the lockout alarm state and the lockout audit trail both depend on.

A missing record — a device that has never locked out — and a record that fails to load SHALL both resolve to "not locked out", logging the load-failure case. Failing closed on a storage glitch would make biometric entry unavailable with no way for the user to recover it.

#### Scenario: Device reset during a lockout stays locked out

- **WHEN** a device enters a biometric lockout and is reset or loses power before the lockout expires
- **THEN** on the next boot matching is refused
- **AND** the refusal lasts a full configured lockout duration measured from boot

#### Scenario: Deep-sleep cycle during a lockout does not clear it

- **WHEN** a device enters a biometric lockout and then enters deep sleep
- **THEN** after waking, matching is still refused for a full configured lockout duration

#### Scenario: Restored lockout is announced

- **WHEN** a lockout is restored at boot
- **THEN** `SDF_EVENT_ROUTER_SECURITY_LOCKOUT` is emitted at CRITICAL priority
- **AND** when the restored lockout later expires, the NORMAL clear emission follows

#### Scenario: Fresh device is not locked out

- **WHEN** a device with no persisted lockout record initialises
- **THEN** matching is permitted

#### Scenario: Unreadable record does not brick entry

- **WHEN** the persisted lockout record cannot be loaded
- **THEN** the failure is logged
- **AND** matching is permitted

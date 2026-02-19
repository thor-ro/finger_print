# Home Assistant (ZHA) Validation Checklist

This checklist validates Door Lock command handling and lock-state reporting end to end:
`HA/ZHA -> Zigbee Door Lock cluster -> SDF -> Nuki BLE -> Zigbee attributes -> HA`.

## Preconditions

1. Device builds and flashes successfully (`idf.py build`, `idf.py flash`).
2. SDF has valid Nuki credentials and can execute lock actions over BLE.
3. SDF is joined to the Zigbee network as a Door Lock endpoint.
4. Home Assistant sees the SDF lock entity under ZHA.
5. Door can be physically observed during tests.

## Command Path Validation

1. `Lock` command from HA:
Expected:
- Door locks physically.
- SDF logs show Door Lock command `0x00` mapped to lock action.
- HA entity state changes to `locked`.

2. `Unlock` command from HA:
Expected:
- Door unlocks physically.
- SDF logs show Door Lock command `0x01` mapped to unlock action.
- HA entity state changes to `unlocked`.

3. `Open/Latch` command from HA (if exposed), or Door Lock `Unlock With Timeout (0x03)`:
Expected:
- SDF executes unlock then unlatch sequence.
- Door opens completely (latch retracted).
- SDF logs show latch sequence start and unlatch continuation.
- HA final lock state resolves to `unlocked`.

## State Reporting Validation

1. Run `Lock` then `Unlock` from HA repeatedly (3x each).
Expected:
- No stale state in HA after command completion.
- No persistent `unknown` state after successful actions.
- SDF publishes lock-state changes from Nuki keyturner updates.

2. Trigger state refresh (e.g., HA refresh/reload entity or wait for next report).
Expected:
- Reported state in HA matches physical lock state.

## Error Handling Validation

1. Simulate transient BLE failure (lock unavailable briefly), then issue `Unlock`.
Expected:
- SDF logs retry attempts.
- On eventual success: HA state converges to `unlocked`.
- On failure: alarm/failure path is logged and subsequent commands still work.

2. Restore lock availability and issue `Lock`.
Expected:
- Normal operation recovers without reboot.

## Pass Criteria

1. Lock, unlock, and latch all execute end to end.
2. HA lock state tracks the physical lock reliably.
3. Temporary failures do not wedge the command path.

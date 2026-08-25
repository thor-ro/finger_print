## Context

See `proposal.md — Why`. The constraints that shape the approach:

- **The GATT read runs on the NimBLE host task.** `sdf_ble_companion_config_access()` (`sdf_ble_companion.c:790-830`) serializes its whole reply inside the access callback while holding `s_lock`. That is safe because it reads a struct. A health report is not a struct: `fp_is_ready()`/`fp_probe()` are UART transactions that `fingerprint-io` serializes against enrolment and matching, and `sdf_storage_nuki_load()` is an NVS read. Doing either on the host task would stall advertising and every other characteristic.
- **`sdf_power` already has the interface this needs.** Its battery callback treats a result outside 0-100 as "no new reading" and keeps the previous value (`sdf_power.c:288-296`). The driver never uses it: `sdf_drivers_battery_get_percent()` answers 100 for a null ADC handle, 100 for a failed read, and 100 unconditionally on the host build (`battery.c:70-77`, `:108`).
- **The alarm mask is already transport-independent.** `s_zigbee_alarm_mask` is an `_Atomic uint16_t` in `sdf_app` (`sdf_app.c:85`), composed via `sdf_app_set_alarm_mask_bits()` and only then pushed to Zigbee (`:147-171`). Lock state and battery are not: they are computed and pushed, with no local copy.
- **The Zigbee component cannot be the cache.** `sdf_protocol_zigbee_update_battery_percent()` returns `ESP_OK` without recording anything when Zigbee is disabled (`sdf_protocol_zigbee.c:1224-1225`), and `sdf_app_update_zigbee_from_action()` returns early on the same condition (`sdf_app.c:290-292`). A health report sourced from the Zigbee attribute cache would be empty on exactly the builds that most need BLE reporting.
- **Lock state has two very different origins.** `sdf_app_update_zigbee_from_action()` sets LOCKED because a lock command was issued; the keyturner handler sets it from what the lock reported (`sdf_app.c:1756-1770`). Only the second is evidence.
- **The Config characteristic's notify channel is already spoken for.** It carries admin-action replies, correlated client-side by a single-slot `pendingBleAdminAction` (`web-companion/app.js:633-644`, resolved at `:610-612`), which only works because the device permits one pending action at a time. Pushing unsolicited state changes down the same channel would break that correlation.
- **A GATT read is not MTU-bound.** NimBLE serves a long attribute value over ATT read-blob transparently, so the report may exceed the MTU on read. A *notification* is MTU-bound.

## Goals / Non-Goals

**Goals:**
- Give the companion a truthful health report, with unknown represented as unknown.
- Hold last-known state where every transport can read the same copy.
- Keep sensor and bus I/O off the read path.
- Stop a failed measurement from being indistinguishable from a healthy one, both in what is displayed and in what the firmware acts on.

**Non-Goals:**
- Historical data, trends or an event log. The report is a current snapshot.
- Remote lock/unlock control from the companion. Reporting only.
- Reworking the fingerprint, Nuki or Zigbee subsystems. Their existing signals are surfaced, not changed.
- Zigbee attribute reporting behaviour. `zigbee-attribute-reporting` stands; this change only stops the Zigbee path being the sole holder of the values.
- User management. That is `companion-user-mgmt`.

## Decisions

### A new Status characteristic, not an extension of Config

The alternative was adding health fields to the Config characteristic's JSON. Rejected on two grounds. Config is configuration — its read reflects `sdf_config_t` and its write sets it, and mixing a measurement into a document whose other fields are settings is how `battery_default_percent` came to be displayed as a battery level in the first place. And Config's notify channel is the admin-action reply channel with single-slot client-side correlation (`app.js:633-644`, resolved at `:610-612`); a state-change notification arriving on it would be mistaken for an action reply.

Read plus notify, one characteristic, one JSON document.

### Authenticated, not admin

Config, Enrollment and OTA require live admin authority. The health report requires an authenticated connection at any permission level. A standard user learning that the door is unlocked and the battery is low is the product working; requiring admin for it would mean the household's non-admin members see nothing.

The constraint that makes this safe is stated as a requirement rather than left to reviewers: the report carries no secret material. Not the Nuki authorization id (`sdf_cli_commands.c:474-482` prints it to an authenticated console; the report does not), not keys, not salts, not the user list.

### Admission is enforced at delivery, not at subscribe

The first draft of this change said an unauthenticated *subscription* is refused, alongside the refused read. That cannot be implemented where it was written: NimBLE services CCCD writes inside the stack and never routes them to the characteristic's `access_cb`, so there is no point in our code where a subscribe can be answered with an insufficient-authentication error. A spec sentence describing a mechanism that does not exist is worse than no sentence — it reads as a control someone has already built.

```
   client                 NimBLE                    our code
     │  write CCCD          │                          │
     ├─────────────────────▶│  handled internally      │
     │                      │  (never reaches us) ─────╳  no decision here
     │                      │                          │
     │                      │   value changes          │
     │                      │◀─────────────────────────┤  admission checked
     │◀─────────────────────┤   notify (or nothing)    │  per connection,
     │                      │                          │  per notification
```

So admission moves to the only place we control: the notification path re-resolves each connection's auth state and its bound user's live enrolment before sending, and sends nothing to a connection that fails either test. The security outcome is the one the original sentence wanted — an unauthenticated subscriber learns nothing — and it is strictly stronger in one respect: authority is re-checked at every notification, so a connection that loses its user mid-session stops receiving reports rather than keeping the entitlement it held at subscribe time.

The cost is that a client can hold a subscription that yields nothing, with no error telling it why. That is acceptable: an unauthenticated client has no legitimate reason to be subscribed, and a client that lost its authority learns as much from its next read.

### Three-valued fields

```
  measured   →  {"battery": {"percent": 63, "age_ms": 41200}}
  unknown    →  {"battery": {"state": "unknown"}}
  n/a        →  {"zigbee":  {"state": "not_applicable"}}   (built without Zigbee)
```

"Unknown" means the system has no reading — the sensor failed, or nothing has reported yet since boot. "Not applicable" means the subsystem is absent by build or configuration. Neither is a number.

This is the whole point of the change, so it is a requirement rather than a convention: no field is ever populated with a substitute. The two substitutions in the code today — `battery_default_percent` rendered as a battery level, and `100` returned for a failed ADC read — are each specifically prohibited.

### `sdf_drivers_battery_get_percent()` reports unavailability

It returns a negative value when there is no reading, which `sdf_power` already interprets correctly (`sdf_power.c:294`). Two other consumers change:

- `sdf_app_power_battery_percent()` (`sdf_app.c:456-459`) passes it through unchanged; nothing to do.
- `sdf_app.c:1012-1014` gates the low-battery warning on `percent <= 20`. With a negative result it would fire on every unlatch. It becomes: warn when measured and low; do not warn when unknown; report unknown.

Whether an unknown battery should be treated as low for *power policy* is deliberately not decided here — `sdf_power` keeps its previous value, which is the conservative behaviour it already implements.

### One last-known-state cache, owned by `sdf_app`

`sdf_app` already owns the alarm mask and already receives every event that produces a state value. The cache extends that: lock state with provenance and timestamp, last battery reading with timestamp, fingerprint readiness, Nuki paired and transport state, Zigbee join state.

Writers are the existing event handlers. Readers are the Status characteristic, the CLI's `nuki status` (which can stop printing "unknown" at `sdf_cli_commands.c:489`), and the Zigbee push, which keeps working exactly as it does — it reads the cache instead of being the only place the value lands.

Fingerprint readiness is *published* by the fingerprint path when it performs I/O for its own reasons, never pulled by a reader. This is what keeps `fingerprint-io`'s serialization requirement intact: a companion opening a dashboard must not be able to inject a sensor transaction between two enrolment scans.

### Lock state carries provenance and age

```
  {"lock": {"state": "locked", "source": "reported", "age_ms": 8300}}
  {"lock": {"state": "locked", "source": "assumed",  "age_ms": 400}}
  {"lock": {"state": "unknown"}}
```

`assumed` is what `sdf_app_update_zigbee_from_action()` produces: a command was sent and no confirmation has arrived. It is honest to show it — the user pressed unlock and wants feedback — and dishonest to show it as equal to a keyturner report. The Zigbee mapping is unchanged; provenance is additional, and `SDF_PROTOCOL_ZIGBEE_LOCK_STATE_UNDEFINED` already exists for the unknown case (`sdf_app.c:286-287`).

### Notify on change, coalesced

The app subscribes once and receives a new report when a value changes, rather than polling a read. Coalescing follows the rule `zigbee-attribute-reporting — Attribute updates are coalesced to the latest value` already states for the other transport: a burst of changes produces the latest report, not one notification each.

Because a notification is MTU-bound while a read is not, a report that does not fit a notification is sent as a change marker that the client resolves with a read. This keeps the notify path a single unfragmented packet and avoids inventing a second chunking protocol.

## Risks / Trade-offs

- **A cache can be stale.** Every value carries its age, so a client can decide; a value with no age is one that cannot go stale.
- **Provenance may confuse a UI that wants one word.** The app shows the state and marks assumed readings as pending confirmation rather than hiding the distinction.
- **Changing the battery driver's return contract touches a safety path.** `sdf_app.c:1012-1014` is the only behavioural decision made from it, and today that decision is unreachable on any board without the ADC. The change makes it reachable and makes its failure mode visible.
- **The notify-a-marker fallback is a second code path** exercised only by large reports. Mitigated by keeping the report small enough that the fallback is rare, and by testing it directly rather than only when it happens to trigger.
- **`ble-companion-service — BLE GATT Authentication` enumerates the restricted characteristics as "(Config, Enrollment, OTA)".** This change adds a fourth without modifying that requirement, because `companion-user-mgmt` is concurrently modifying the same requirement's text and two conflicting MODIFY deltas on one 100-line requirement is the worse hazard. The Status characteristic's admission is therefore stated self-containedly in its own requirement. When both changes are archived, the enumeration should be corrected to include Status.

## Open Questions

- Whether the setup state belongs in this report at all, given it already has its own unauthenticated characteristic (`ble-companion-service — Setup State Is Observable Before Authentication`). Including it is convenient for a dashboard; duplicating it risks the two disagreeing. The change includes it and requires both to be produced from `sdf_services_get_setup_state()` so they cannot.
- Whether the CLI's `nuki status` should adopt the cache in this change or a later one. The tasks include it, since leaving "Last Keyturner State: unknown" printed next to a companion that shows the real value is the kind of divergence this change exists to remove.

## Verification Outcome (task 8.5, emulator)

Ran `scripts/run_ble_ota_harness.sh --scenario device-health` against the
`ble_ota_gate` fixture under esp-emu. Result: `BLE_OTA_HARNESS_RESULT
status=WEDGE scenario=device-health ... notification_delivery=BLOCKED_EMU_WEDGE`.

**Confirmed under emulation** (harness assertions plus emulator log):

- Unauthenticated Status read refused with `INSUFFICIENT_AUTHENTICATION`.
- Authenticated (standard-flow admin) read returns the full report with the
  documented vocabulary (`battery` measured/unknown, `lock` state vocabulary,
  `ota`, `setup`, `firmware` present) and no secret fields.
- Subscription accepted; driving an OTA BEGIN/END burst flips the reported
  `ota` field through `idle -> downloading -> failed`.
- Coalescing works device-side: the emulator log shows exactly ONE Status
  notification initiated (~150 ms after the burst, att_handle=17), and the
  intermediate `"ota":"downloading"` report is never sent.

**Blocked by the esp-emu ACL wedge** (add-ble-ota-emulator-harness design D6,
~28-31 inbound HCI ACL packets/boot): end-to-end DELIVERY of that coalesced
notification to the central. The scenario's inherent packet budget
(discovery + pairing + CCCD writes + register/login + reads + BEGIN/END)
exceeds the boundary (31 packets observed immediately before the silent
wedge); trimming subscriptions only reduced the count to 31. The notification
leaves NimBLE but never reaches the central's HCI trace. Delivery therefore
stays confirmed only down to the emulator's host/controller boundary;
confirming it over the air requires hardware (as with bulk BLE OTA transfer).

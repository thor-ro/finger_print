## 1. Battery Measurement Reports Unavailability

- [x] 1.1 Change `sdf_drivers_battery_get_percent()` to report unavailability instead of `100` for a null ADC handle and for a failed `adc_oneshot_read()` (`battery.c:70-77`), and make the host build's stub report unavailability rather than a constant `100` (`battery.c:108`)
- [x] 1.2 Confirm `sdf_power`'s battery callback already handles the unavailable result correctly and keeps its previous value (`sdf_power.c:288-296`); add a host test pinning that behaviour rather than assuming it
- [x] 1.3 Gate the low-battery warning on a measured low reading (`sdf_app.c:1012-1014`), not on an unavailable one, and record the unavailability in the state cache
- [x] 1.4 Host unit tests: unavailable measurement raises no low-battery warning; a measured value at and below the threshold still does; an unavailable measurement is never reported as 100

## 2. Transport-Independent Device State Cache

- [x] 2.1 Add a last-known-state cache in `sdf_app` alongside `s_zigbee_alarm_mask` (`sdf_app.c:85`), holding lock state, battery, fingerprint readiness, Nuki link state and Zigbee join state, each with its recording timestamp
- [x] 2.2 Record lock state in the cache from the keyturner report (`sdf_app.c:1756-1770`) marked as confirmed, and from `sdf_app_update_zigbee_from_action()` (`sdf_app.c:290-308`) marked as assumed
- [x] 2.3 Record the state unconditionally, before the Zigbee-enabled check, so a Zigbee-disabled build still holds it (`sdf_protocol_zigbee.c:1224-1225`, `sdf_app.c:290-292`)
- [x] 2.4 Have the Zigbee push read the cache rather than being the only place the value lands, leaving `zigbee-attribute-reporting` behaviour unchanged
- [x] 2.5 Publish fingerprint readiness into the cache from the fingerprint path when it performs I/O for its own reasons; add no probe-on-read path
- [x] 2.6 Record Nuki paired and transport state, and Zigbee join state, from their existing event handlers
- [x] 2.7 Host unit tests: an assumed state is replaced by a confirmation; the cache records with the reporting transport disabled; ages advance; nothing in the cache write path performs I/O

## 3. Health Report Producer

- [x] 3.1 Add a health report serializer producing the three-valued fields (measured / unknown / not applicable) from the cache, with an age on every stale-able value
- [x] 3.2 Source firmware version and OTA state from `sdf_ota_get_version()` and `sdf_ota_get_state()`, and setup state from `sdf_services_get_setup_state()`, so the report and the SetupState characteristic cannot disagree
- [x] 3.3 Report not-applicable for subsystems absent by build or configuration, distinctly from unknown
- [x] 3.4 Exclude secret material: no Nuki authorization id (contrast `sdf_cli_commands.c:474-482`), no keys, no salts, no user records
- [x] 3.5 Host unit tests: every field's three conditions; a fresh boot with nothing reported yields unknown everywhere rather than defaults; the serializer performs no I/O

## 4. Status Characteristic

- [x] 4.1 Add the Status characteristic (read + notify) to the Companion Service GATT table alongside Auth / Config / Enroll / OTA / SetupState (`sdf_ble_companion.c:989`)
- [x] 4.2 Gate it on an authenticated connection without additionally requiring admin authority, unlike `sdf_ble_companion_config_access()` (`sdf_ble_companion.c:785-788`)
- [x] 4.3 Serve the read from the cache inside the access callback with no wait on another task and no bus operation
- [x] 4.4 Notify subscribed connections on change, coalescing a burst to the latest report, re-resolving each connection's authentication and its bound user's live enrolment before every send (admission is enforced at delivery - NimBLE never routes a CCCD write to the access callback, see design "Admission is enforced at delivery, not at subscribe")
- [x] 4.5 Notify a change indication carrying no partial report where the report exceeds the negotiated MTU, and confirm the read path serves the full value via ATT read-blob
- [x] 4.6 Account for the added subscription against `ble-companion-service — Persisted Notification Subscription Capacity`
- [x] 4.7 Host unit tests for 4.2-4.5, including an unauthenticated read refused, a standard-user read permitted, a deleted bound user losing access, an oversized report indicated rather than truncated, and the delivery-time admission rule refusing a connection that is unauthenticated or whose bound user is gone

## 5. Console Alignment

- [x] 5.1 Print the recorded lock state in `nuki status` instead of the hardcoded "unknown", and delete the comment that explains why it could not (`sdf_cli_commands.c:487-489`)
- [x] 5.2 Print the battery level as unknown where no measurement is available, rather than a number
- [x] 5.3 Leave "Signal RSSI: N/A" and "Parent RSSI: N/A" (`sdf_cli_commands.c:490`, `:726-727`) as not-applicable, and represent them the same way in the health report

## 6. Web Companion App

- [x] 6.1 Replace `updateStatusCards()`'s use of `config.battery_default_percent` (`web-companion/app.js:626-631`) with the health report, and remove the "Lock state would come from other notifications" comment by making it true
- [x] 6.2 Add a device health view covering every reported field, available to any authenticated user
- [x] 6.3 Render unknown as unknown and not-applicable distinctly, with no number and no carried-over value
- [x] 6.4 Mark an assumed lock state as awaiting confirmation, and surface the age of a reading old enough to mislead
- [x] 6.5 Subscribe to Status notifications and handle the change indication by issuing a read
- [x] 6.6 Show the reported battery level in the OTA pre-flight warning, or state that it is unknown

## 7. Documentation

- [x] 7.1 Document the Status characteristic and the health report's field vocabulary in `doc/sdf_sas.md`
- [x] 7.2 Document the health view in `doc/user_manual.md`, including what unknown means and why an assumed lock state is shown differently
- [x] 7.3 Record the battery driver's new return contract wherever the old one is described

## 8. Verification

- [x] 8.1 `idf.py build` for esp32c6 clean, no new warnings
- [x] 8.2 Build with Zigbee disabled and confirm the health report is unchanged
- [x] 8.3 Full host suite green, with the new tests registered in `test_runner_main.c`
- [x] 8.4 `openspec validate companion-device-health --strict`
- [x] 8.5 Emulator scenario reading Status, subscribing, and observing a coalesced update, using `scripts/run_ble_ota_harness.sh`; record what the esp-emu ACL wedge (`add-ble-ota-emulator-harness` design D6) prevents confirming rather than ticking past it

## Why

The companion has no idea what the device is doing.

- **The dashboard's battery reading is a configuration value.** `updateStatusCards()` (`web-companion/app.js:626-631`) renders `config.battery_default_percent` — a field the device serializes from `sdf_config_t` on the Config characteristic (`sdf_ble_companion.c:807`) — into a card labelled "Battery" (`web-companion/index.html:25-27`). It is a configured default, not a measurement. Changing it in the config screen changes what the "battery level" appears to be.
- **The lock card never updates.** `index.html:22` shows `--` for the life of the session, above the comment `// Lock state would come from other notifications` (`app.js:630`). There are no other notifications.
- **Nothing holds the lock's last known state.** The keyturner report is parsed at `sdf_app.c:1756-1770`, mapped, pushed to Zigbee, and dropped. The CLI's own code says so: "This would need access to sdf_app internal state, for now we show unknown" (`sdf_cli_commands.c:487-489`). Zigbee is also the wrong owner — `sdf_protocol_zigbee_update_lock_state()` and `..._update_battery_percent()` return early when Zigbee is disabled (`sdf_protocol_zigbee.c:1224-1225`), so on a Zigbee-disabled build there is no cached state anywhere.
- **A failed battery measurement is indistinguishable from a full battery.** `sdf_drivers_battery_get_percent()` returns `100` when the ADC handle is null and `100` when `adc_oneshot_read()` fails (`battery.c:70-77`), and the host build returns a constant `100` (`battery.c:108`). Two consumers act on that: `sdf_app.c:1012-1014` raises the low-battery warning at `<= 20`, which on a board without the ADC can never fire; and `sdf_power`'s battery callback contract already reserves an out-of-range result to mean "no new reading" (`sdf_power.c:288-296`) — a contract the driver defeats by answering 100 instead.
- **The OTA warning asks the user to check something the app could tell them.** `web-companion-app — OTA Battery Warning` specifies the text "Ensure your battery is above 20%" before a transfer that the same spec says "draws significant power". The app has no number to show, because the only one it has is the configured default.
- **Everything else is console-only.** Nuki paired state and transport readiness (`sdf_cli_commands.c:464-492`), Zigbee join state (`:680`), firmware version (`:823`), OTA state (`:841`), alarm bits (`sdf_app.c:85`) — none of it reaches the companion, which is the only supported remote surface for a claimed device.

The result is a status display that is confidently wrong: it reports a battery level it did not measure, a lock state it never learned, and no health signal at all for the fingerprint sensor, the lock link, or the radio.

## What Changes

- **Add a Status characteristic** to the Companion Service: readable by any authenticated connection and notified on change, carrying the device's health report — lock state, battery, alarms, fingerprint sensor readiness, Nuki link, Zigbee join state, firmware version, OTA state and setup state.
- **Introduce a three-valued vocabulary.** Every field is *measured*, *unknown*, or *not applicable*. No field is ever filled with a substitute — not a configured default, not a last-resort constant.
- **BREAKING (internal): `sdf_drivers_battery_get_percent()` reports unavailability** instead of returning 100. `sdf_power` already handles that; `sdf_app`'s low-battery check and the health report gain an explicit unknown case.
- **Hold last-known device state in a transport-independent cache** owned by `sdf_app`, fed by the same events that today only reach Zigbee, so the health report is identical on a Zigbee-disabled build and the two transports cannot report different numbers.
- **Give lock state a provenance and an age.** `sdf_app_update_zigbee_from_action()` (`sdf_app.c:290-308`) writes LOCKED from the fact that a lock command was *sent*. That value is an assumption; a keyturner report is a confirmation. The report distinguishes them and says how old the reading is.
- **Health reads perform no sensor or bus I/O.** The GATT read is served from the cache. The fingerprint sensor is never probed on behalf of a reader, per `fingerprint-io`.
- **Replace the web dashboard's fabricated cards** with the real report, rendering unknown as unknown, and show the actual battery level in the OTA pre-flight warning.

### Capabilities

- **New:** `companion-device-health`
- **Modified:** `ble-companion-service`, `web-companion-app`

### Dependencies

Independent of `companion-user-mgmt`; both extend the same service and can land in either order. Where the two touch the same web view, the health report is the read-only half.

### Accepted risks

- The status report widens what an authenticated non-admin can see (firmware version, Nuki paired yes/no, join state). It carries no secret material — no authorization id, no keys, no salts, no user list.
- A cache means a reader can be shown a stale value. Mitigated by reporting the age alongside every value rather than presenting stale data as current.

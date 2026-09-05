## Why

The first-time wizard asks for the Admin fingerprint once and then goes silent for the whole enrolment. `WizardView.svelte` shows a single "Enrol Admin Finger" button and one static line — "Place the admin finger on the sensor for each of the 3 scans..." — and nothing changes until the enrolment either succeeds or fails. The user is left guessing whether a scan registered, whether to lift the finger, and how many presses remain.

The companion was written expecting better: `handleWizardEnrollNotification()` already has a branch for a notification carrying a `step` field, which renders "Place finger for scan N of 3" and moves the progress bar. That branch is dead code. The firmware never emits such a notification: `sdf_ble_companion` subscribes to `ENROLLMENT_COMPLETE` and `ENROLLMENT_FAILED` only, and the one event that does carry per-scan state, `SDF_EVENT_ROUTER_ENROLLMENT_STEP_COMPLETE`, is consumed solely by `sdf_app` for its alarm mask. Between the first prompt and the terminal reply the companion learns nothing.

`ble-companion-service` already *presumes* these notifications exist — "Enrolment progress notifications SHALL carry the request id of the request that started the enrolment", with a scenario asserting attribution — but no requirement says progress is reported at all, and none is. This change closes that gap and spends it on the wizard.

## Changes

- The enrol task SHALL emit `ENROLLMENT_STEP_COMPLETE` when a scan succeeds and the state machine advances to the next scan, carrying the number of scans captured and the scan now expected.
- The BLE companion SHALL subscribe to that event and notify authenticated (and setup-phase) clients with a progress notification carrying the captured count, the expected scan, the total, and the originating request id — without consuming the id, which belongs to the terminal reply.
- Progress SHALL be notified only for the in-progress states. The event is also emitted at enrolment start (captured 0, scan 1 expected), which is exactly the "place your finger now" prompt; it is emitted on a failed start too, and that case SHALL NOT produce a progress notification, because a terminal reply already reports it.
- The wizard SHALL ask for one fingerprint at a time: it names the scan it is waiting for, and on each successful scan it updates the visualization and asks for the next one, until the enrolment completes.
- The scan visualization SHALL show captured scans discretely (one marker per scan) rather than only as a percentage, so "2 of 3 captured" is legible without reading the label.
- `SDF_EVENT_ROUTER_SUBS_BLE_COMPANION` goes 3 → 4. The router pool is sized with zero headroom on purpose, so the declared count must move with the subscription or `sdf_event_router_start()` fails.

## Impact

**Firmware**

- `sdf_services` (`sdf_services_enroll.c`): one new emitter in the `SDF_ENROLL_ACT_EXECUTE_STEP` branch, after the state lock is released. No new state, no new task.
- `sdf_ble_companion`: one new subscription and one handler, built like the existing complete/failed handlers.
- `sdf_event_router`: declared-subscription count only.

**Web companion**

- `src/lib/protocol/usermgmt.ts`: a progress predicate and decoder (stays DOM-free per the lint gate).
- `src/lib/state/session.svelte.ts`: wizard and dashboard enrolment progress state driven by the notification instead of a static string.
- `ScanProgress.svelte`, `WizardView.svelte`: per-scan markers and per-scan prompting. Colours come from theme tokens only.
- `routes/+page.svelte`, `PostConnectView.svelte`: the wizard, the login pane and the dashboard move behind one dynamic import. Sharing `ScanProgress` with the dashboard put it on the initial load; only the connection view can be on screen before a device is connected, so the rest is deferred and the initial-load budget holds without being raised.

**Tests**

Host Unity tests in `firmware/components/sdf_services/test/`, Vitest for the protocol decoder and the session handler.

**No migration required.** An older companion ignores an unknown notification (its handler already tolerates fields it does not know), and a newer companion against older firmware simply never receives progress — it falls back to the same static prompt it shows today.

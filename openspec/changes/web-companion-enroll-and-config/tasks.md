## 1. Config UI

- [ ] 1.1 In `app.js`, add event listener for `#btn-read-config`: acquire Config characteristic, subscribe to notifications, then read it
- [ ] 1.2 Parse the JSON config response and render as a key-value table with editable fields
- [ ] 1.3 Add an "Apply Changes" button that writes a JSON delta of changed fields to the Config characteristic
- [ ] 1.4 Request MTU negotiation (512 bytes) in the connect flow after GATT connect, before acquiring characteristics
- [ ] 1.5 Add a `configChar` variable; acquire `SDF_CONFIG_UUID` characteristic in the connect handler alongside `authChar`

## 2. Enrollment UI

- [ ] 2.1 Add an enrollment panel to `index.html` inside `#dashboard-view`: user_id number input (1–10), permission select (Standard/Elevated/Admin), "Enroll Fingerprint" button, step progress display, result message
- [ ] 2.2 In `app.js`, acquire the Enroll characteristic (`SDF_ENROLL_UUID`) in the connect flow and subscribe to its notifications
- [ ] 2.3 Implement the enroll button handler: write `{"user_id": N, "permission": P}` to the Enroll characteristic
- [ ] 2.4 Handle enroll notifications: update the step counter display (1/3 → 2/3 → 3/3 → success/fail)

## 3. Status Cards

- [ ] 3.1 Add lock state and battery percent status cards to the dashboard header area in `index.html`
- [ ] 3.2 Populate status cards from the config read response or dedicated status fields when the dashboard loads

## 4. Styling

- [ ] 4.1 Add CSS for the enrollment panel, step progress indicator, config table, and status cards in `style.css`

## 5. Verify

- [ ] 5.1 Test "Read Config" in browser — confirm JSON is received and displayed
- [ ] 5.2 Test enrollment trigger — confirm status messages update step-by-step

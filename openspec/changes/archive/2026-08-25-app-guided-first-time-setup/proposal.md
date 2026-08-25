## Why

First-time setup is today a physical, offline procedure: a Short Press on an unclaimed device enrols the Admin fingerprint, a second Short Press pairs the Nuki, and the Web Companion App only becomes reachable afterwards through an admin-fingerprint-gated pairing window. The app can therefore never guide the steps that matter most, and the two setup phases a user is most likely to get wrong have no on-screen guidance at all.

Making the app the guide requires the device to be reachable before it is claimed, which the current trust model forbids — a fresh device advertises sparse and allow-list-filtered with an empty allow list, so nothing can connect. This change replaces the physical claim procedure with an app-guided one and rebuilds the surrounding trust rules so that opening the device to an unclaimed connection does not weaken it.

## What Changes

- **BREAKING** The Web Companion App becomes mandatory for first-time setup. The physical button path for enrolling the first Admin and for pairing the Nuki is removed; the button retains only setup-phase arming and factory reset.
- **BREAKING** A brand-new or factory-reset device enters a **setup phase**: advertising is unfiltered and connectable so an arbitrary companion client can connect and run the wizard. Access to the wizard is no longer gated on a fingerprint that does not yet exist.
- The setup phase accepts **at most one connection**. The connect path consults setup state and terminates a second inbound connection rather than relying on advertising not being re-armed.
- A **button press during the setup phase** drops the current setup connection and re-arms advertising, giving the person physically at the device the tiebreaker against a squatting client.
- The setup phase ends in exactly one of two ways: an **explicit "finish setup" step** issued by the app, or a **timeout**. On timeout the device wipes all partial setup state (enrolled templates, web accounts, bonds, admission records, partial Nuki credentials), stops advertising, and requires a button press to re-arm. There is **no resume** — a lapsed setup starts over.
- Completion is recorded in a **latched `setup_complete` flag** rather than derived from enrolled-user count plus persisted Nuki credentials, so the transition has a single atomic point and cannot be silently reversed by later user or credential deletion.
- **BREAKING** Allow-list membership is seeded from an **explicit admission record** intersected with the NimBLE bond store, replacing the current "every persisted bond is trusted" inference. Bonding is mandatory to use the wizard at all, so without this an abandoned setup-phase connection becomes permanently trusted at the next reboot.
- Device setup state is **exposed to the app before authentication**, so the wizard can determine which phase the device is in prior to any account existing.
- **BREAKING** Factory reset no longer requires an Admin fingerprint. It is the recovery path for Admin fingerprint loss, which the fingerprint gate made unreachable.
- Registration is ordered **after** Admin enrolment in the wizard, so the existing `WEB_REG_AUTH` admin-fingerprint gate has a fingerprint to gate against.
- Zigbee join stays out of the wizard and remains available from the dashboard after lockdown; it is unchanged by this proposal.

## Capabilities

### New Capabilities
- `device-setup-phase`: The setup-phase lifecycle — arming, the single-connection cap, the explicit finish step, the timeout-and-wipe behaviour, the latched completion flag, and the transition that enables allow-list filtering.
- `ble-companion-admission`: The explicit admission record, its write and delete points, and allow-list seeding as the intersection of admitted identities and the persisted bond store.

### Modified Capabilities
- `ble-companion-service`: Default advertising is unfiltered during the setup phase rather than always sparse and allow-list-filtered; allow-list seeding no longer adopts every persisted bond; connection admission depends on setup state; setup state becomes readable without prior login.
- `web-companion-app`: Gains the first-time setup wizard as a mandatory, guided flow covering Admin enrolment, account registration, and Nuki pairing, and issues the explicit finish step.
- `sdf-services-tasks`: Removes the state-dependent Single-Click setup action and the unauthenticated pre-enrollment bootstrap bypass; redefines button gestures for the setup phase; removes the Admin-fingerprint gate on factory reset.

## Impact

**Firmware**
- `sdf_ble_companion`: advertising mode selection and `restart_advertising()`; connection admission in `BLE_GAP_EVENT_CONNECT`; `seed_allow_list()`; admission record write on setup completion and on pairing-window admit; admission clear on failed-login eviction; a setup-state characteristic or advertisement field.
- `sdf_services`: `sdf_services_get_setup_state()` becomes latch-backed; `sdf_button_resolve_single_click_action()` and `sdf_services_try_bootstrap_admin_action()` are removed; factory-reset dispatch no longer sets a pending admin action; setup-phase timeout and wipe.
- `sdf_storage`: new admission-record and `setup_complete` latch accessors, following the existing `sdf_storage_ble_target_save()` shape and the "absent key reads as empty, not an error" convention.
- `sdf_app`: setup-completion sequencing — admission record is written **before** Nuki credentials, so a power loss mid-completion leaves the device in the setup phase rather than locked out with an empty allow list.

**Web app**
- `web-companion/`: new wizard view preceding the existing connection/auth/dashboard views; reads setup state before login; drives enrolment, registration, Nuki pairing, and finish.

**Docs**
- `doc/First Time Flow Concept.md`, `doc/user_manual.md`: rewritten for the app-guided flow. Both currently state that Double Press is retired and unmapped while `sdf_services_admin.c:234` maps it to the BLE pairing window — that drift is resolved here.

**No migration required** — there are no devices in the field, so allow-list seeding may adopt the intersection rule directly with no compatibility path for existing bonds.

**Accepted risks**
- Removing the fingerprint gate from factory reset means anyone with physical button access can wipe the device and drop it into the setup phase. `doc/wiring.md:53` anticipates the button being routed outside an enclosure, so this may be reachable from the attacker's side of the door. The exposure is denial of service, not entry: a hijacker who claims the device still cannot pair a Nuki without pressing the Nuki's own button on the inside.
- Setup-phase pairing is Just Works with no MITM protection (`ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO`, `sm_mitm = 0`), and the wizard sends a registration password hash over that link. This was acceptable when bonding only occurred inside a short admin-gated window; the setup phase is longer and open. Left unaddressed here and tracked as an open question.

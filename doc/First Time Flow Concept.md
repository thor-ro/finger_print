# First-Time Flow Concept (App-Guided Setup)

This document describes the initial setup flow for a brand-new or factory-reset Smart Door Fingerprint (SDF) lock. Setup is guided end-to-end by the Web Companion App; there is no physical-button path for enrolling the first Admin or for pairing the Nuki lock.

## 1. Core Principles

- **The app guides every setup step.** Admin enrolment, account registration, Nuki pairing and completion all happen through the Web Companion App's setup wizard.
- **Reachable before it is claimed.** A device that has never completed setup advertises *unfiltered and connectable*, so an arbitrary companion client can reach the wizard. This openness is bounded in time, bounded to a single connection, and ends in one of exactly two ways (explicit completion or timeout).
- **Completion is an explicit, latched act.** Setup is complete only when the app issues the explicit finish step — never inferred from enrolled users or persisted credentials. The latch survives later user/credential deletion and is cleared only by a factory reset.
- **Trust follows a recorded act.** Allow-list membership comes from an explicit admission record intersected with the bond store — never from "has a bond, therefore trusted".

## 2. The Setup Phase

A brand-new device (or one just factory-reset) boots into the **setup phase**:

1. The device advertises connectably with no allow-list filter. Any companion client can discover and connect.
2. At most **one** client may hold the connection. Any second inbound connection is terminated immediately at connect time.
3. The phase runs under three compile-time timers:

| Timer | Default | Runs from | Expiry effect |
| --- | --- | --- | --- |
| **Arm window** | 5 min | arming (boot, factory-reset reboot, button press) | Timeout wipe + stop advertising |
| **Setup deadline** | 10 min | first accepted connection | Timeout wipe + stop advertising |
| **Connection idle** | 2 min | last GATT activity on the setup connection | Drops only the connection; advertising re-arms; deadline keeps running |

Neither the arm window nor the deadline is extended by client activity, progress, disconnection or reconnection. The **only** way either timer restarts is a physical button press. Once the first connection starts the deadline, the arm window stops governing altogether — a disconnect or an idle drop leaves the deadline as the sole bound rather than handing out a fresh open-air window. Worst case per arming is therefore 15 min of open advertising, when a client connects in the last instant of the arm window.

## 3. The Wizard

On connecting, the app reads the device's setup state (a read-only characteristic readable before login) and resumes at the matching step:

### Step 1 — Enrol the Admin Fingerprint
The first Admin (User ID 1, Permission 3) is enrolled through the wizard. Three scans of the finger, as with any enrollment.

### Step 2 — Register Your Account
Registration creates the companion account used for login. It is offered only after an Admin exists, because confirming registration requires a scan of that Admin finger (`WEB_REG_AUTH`). Registration before any admin exists is rejected by the device outright.

### Step 3 — Pair Your Nuki Lock
The user puts the Nuki Smart Lock into pairing mode (hold its button ~5 s), then triggers pairing from the wizard. During the setup phase this uses the dedicated `setup_nuki_pair` request — reachable only while setup is not complete; after completion the admin-gated "Request Nuki Re-pair" dashboard trigger takes over.

### Step 4 — Finish Setup
The explicit completion request. The device checks prerequisites (Admin enrolled, account registered, Nuki paired), reporting the first outstanding step if any. All three are required: completing without an account would claim a device nobody can log into. A failure that is *not* an unmet prerequisite is reported as `internal_error` instead of a step name, so the app offers a retry rather than sending the user back through work that already succeeded. On success it, in order:

1. Persists an **admission record** for the connected client (before anything else — see crash-safety below),
2. Persists the **setup-completion latch**,
3. Adds the client to the allow list and pushes it to the controller,
4. Switches advertising to sparse, allow-list-filtered mode,
5. Restores the ordinary multi-connection limit.

> [!IMPORTANT]
> Completing setup locks the device to the companion that completed it: afterwards only allow-listed identities can connect. Losing that companion means using a factory reset to recover.

**Crash safety:** the admission record is written *before* the latch. Power lost between the two writes leaves the device in the setup phase with an inert record — reachable and completable — rather than reporting complete while unreachable.

Steps 3–5 run after the latch is already persisted, so a failure there cannot be handled by ordering alone: the device rolls the latch back. Otherwise it would sit latched but still armed, with the deadline running — and that deadline would wipe the accounts and admission records out from under a device reporting itself claimed, leaving it reachable only by a physical factory reset.

## 4. Timeout: Wipe Without Resume

If the arm window or setup deadline expires, the device:

- erases all partial setup state: enrolled templates, web accounts, bonds, admission records and any partial Nuki credentials,
- disarms the setup phase and stops advertising.

There is **no resume across a lapse** — the next setup attempt starts from scratch. Within one setup phase (before a lapse), the wizard resumes from reported state after reconnects.

## 5. Button Semantics

With the new flow the physical Configuration Button has exactly two meanings:

| Gesture | State | Effect |
| --- | --- | --- |
| **Short Press / Double Press** | Setup incomplete (armed or lapsed) | **Reclaim & re-arm**: terminate the current setup connection, resume unfiltered advertising, restart both the arm window and the deadline. No pending admin action is set. |
| **Short Press / Double Press** | Setup complete | Double Press requests the admin-fingerprint-gated **BLE Companion Pairing Window** (used to admit a second companion). Short Press does nothing. |
| **Hold 8 sec** | Any | **Factory Reset**, executed directly — no Admin fingerprint required (see below). |

> [!NOTE]
> The reclaim gesture gives whoever is physically at the device the tiebreaker over any client squatting on the setup slot. One press, one meaning: *give me the setup slot.*

## 6. Factory Reset

Factory reset (Hold 8 sec) requires **no Admin fingerprint** — it is the recovery path for a lost or unreadable Admin fingerprint, which a fingerprint gate would make unreachable. The reset erases users, web accounts, bonds, admission records, Nuki credentials, Zigbee state and the completion latch, then reboots into the setup phase, ready to be claimed again.

## 7. Adding Additional Users After Setup

After setup completes, additional fingerprints are enrolled through the Web Companion App (Enrollment characteristic on the dashboard, and "Request Enroll Admin" for additional admins) or remotely via Zigbee once joined. There is no button path for enrollment anymore.

Permission model, capacity limits and the Zigbee remote-enrollment behavior are unchanged; see `usermanagement.md` and `user_manual.md`.

## 8. Edge Cases and Considerations

- **Device lapsed before you opened the box:** normal. Press the button once to re-arm, then open the app and start the wizard.
- **Squatted setup slot:** another client holding the connection? Press the button — your press always wins and their session is dropped.
- **Mid-wizard disconnect:** reconnect within the deadline to resume at the current step. If the device stopped advertising, the window lapsed: press the button and start over.
- **Lost companion device:** perform a factory reset (Hold 8 sec), then set up again with the replacement.

# Smart Door Fingerprint Lock User Manual

## Overview
This Smart Door lock integrates a biometric fingerprint sensor directly into your Zigbee Smart Home ecosystem (e.g., Home Assistant). You can manage your fingerprint database using standard Zigbee commands, or via the Web Companion App.

First-time setup is **app-guided**: after a factory reset or on first power-on, the device opens a time-bounded setup window during which the [web-companion app](../web-companion/README.md) wizard walks you through enrolment, registration and Nuki pairing. There is no physical-button path for first-time setup.

## First-Time Setup (Out of the Box)

### The Setup Wizard

When the lock is powered on for the first time (or after a factory reset), it enters the **setup phase**: it advertises over Bluetooth openly so any companion app can connect and run the setup wizard.

> [!IMPORTANT]
> **Time limit:** Setup must be completed within about 15 minutes of the device arming (5 minutes of open advertising, then 10 minutes once you connect; an idle connection is dropped after 2 minutes but your deadline keeps running). If the window lapses, **all progress is erased** and the device stops advertising until you press its button again.

1. **Arm:** On first power-on the device arms automatically. If it has lapsed, press the Configuration Button once to re-arm.
2. **Connect:** Open the [web-companion app](../web-companion/README.md) in a Web-Bluetooth-capable browser and connect to the device. Only one connection is accepted at a time — if someone else holds the slot, press the button on the device to reclaim it.
3. **Enrol the Admin Fingerprint:** Follow the wizard's step 1. Place the admin's finger on the sensor **three consecutive times**; the fingerprint is saved as **User ID 1** with **Admin privileges (Permission 3)**.
4. **Register Your Account:** Create your companion-app username/password. Confirming requires a scan of the Admin finger you just enrolled.
5. **Pair Your Nuki Lock:** Put your Nuki Smart Lock into pairing mode (hold its button ~5 s until the LED glows), then start pairing from the wizard.
6. **Finish Setup:** Confirm the final wizard step. The device records that setup is complete, locks itself to this browser's companion, and switches to filtered advertising.

If you reconnect mid-setup (within the same window), the wizard resumes at the step the device reports. If the window lapses, everything starts from scratch after re-arming.

### Phase: Joining the Smart Home (Optional Zigbee)

Zigbee stays out of the wizard. After completing setup and logging in:

1. **Prepare Coordinator:** Enable "Permit Join" on your Zigbee coordinator (e.g., Zigbee2MQTT).
2. **Initiate Zigbee Join:** Log in to the [web-companion app](../web-companion/README.md) and tap **Request Zigbee Join** on the dashboard. The LED begins to pulse **purple** (Awaiting Admin Auth).
3. **Authorize:** The Admin (User ID 1) touches the fingerprint sensor to authorize the network join.
4. **Active Joining:** The LED flashes **rapid purple** as the device attempts to steer to the network.
5. **Completion:** The LED glows solid **green** and the device becomes available in the Smart Home dashboard.

## Configuration Button Mapping

The button's meaning depends on whether the device has completed first-time setup:

| Action / Duration | State Condition | Authentication Required | Result |
| --- | --- | --- | --- |
| **Press (Short or Double)** | Setup not complete (armed or lapsed) | None | **Reclaim & re-arm:** drops the current setup connection, resumes advertising, restarts the setup timers |
| **Double Press** | Setup complete | Admin Fingerprint | Requests the BLE Companion Pairing Window (admits a second companion app) |
| **Hold 8 sec** | Any | None | Factory Reset (executes directly — see below) |

> [!NOTE]
> Short Press on a *completed* device does nothing: first-time setup is app-guided, and post-setup enrollment runs through the companion app or Zigbee.

> [!NOTE]
> Triple Press and Hold 3 sec are not mapped to any action. They previously triggered Admin User Enrollment and Zigbee Join respectively; both are now only reachable through an authenticated request from the [web-companion app](../web-companion/README.md).

### Re-pairing Nuki After Setup Is Complete

If the Nuki lock ever needs to be re-paired — for example after replacing it — use the **Request Nuki Re-pair** button on the dashboard of the [web-companion app](../web-companion/README.md). This sends a re-pairing request over Bluetooth, which the device only accepts if you're already logged in and setup is complete. As with initial pairing via the wizard, physical presence at the Nuki (its own pairing-mode button) is required too. A full factory reset also clears Nuki credentials by returning the device to the setup phase.

## Managing Fingerprints via Zigbee

The lock maps standard Zigbee "Door Lock" cluster commands to the fingerprint sensor.

### Enrolling a New Fingerprint
To enroll a new user remotely:
1. Issue the **Set PIN Code** or **Set RFID Code** command from your Zigbee coordinator.
2. Specify a unique `User ID` (between 1 and 500) and an optional permission level (1=Normal, 2=Elevated, 3=Admin).
3. The lock will enter enrollment mode. The status LED will blink blue.
4. Place the new user's finger on the sensor. The sensor requires three successful scans to build a complete profile. The LED will flash green after each scan.
5. On the final successful scan, the LED will light solid green, and the user is enrolled.

### Removing a Fingerprint
To delete a user:
1. Issue the **Clear PIN Code** or **Clear RFID Code** command with the specific `User ID`.
2. The user is instantly removed from the lock's database.

To clear all fingerprints:
1. Issue the **Clear All PIN Codes** or **Clear All RFID Codes** command.
2. The entire fingerprint database will be erased.

### Viewing Active Users
The lock automatically synchronizes the list of enrolled users to the Zigbee network.
- Look for the custom Zigbee Attribute `0x4000` (Active Users List).
- It provides a JSON-like array (e.g., `[1:3, 5:1]`) which represents `[UserID:Permission]`.
- This list updates automatically whenever a user is added or removed.

## Local Hardware Enrollment

Enrollment of additional fingerprints is available through the Web Companion App and via Zigbee — the physical button no longer triggers enrollment.

### Standard User Enrollment (Companion App)
1. **Initiate:** Log in to the [web-companion app](../web-companion/README.md) and use the **Enroll Fingerprint** panel on the dashboard (choose User ID and Permission).
2. **Pending Authorization:** The LED begins to pulse **blue**, waiting for Admin authorization.
3. **Authorize:** The Admin (any user with Permission 3) touches the fingerprint sensor within 10 seconds.
   - If a non-Admin fingerprint is scanned, the LED flashes **red** and the action is rejected.
   - If no fingerprint is provided within 10 seconds, the LED flashes **red** and the device returns to idle.
4. **Biometric Capture:** Once authorized, the new user places their finger on the sensor **three consecutive times**. After each successful scan, the LED flashes **green**.
5. **Completion:** After the third scan, the template is saved with the chosen User ID and permission. The LED breathes solid **green** for a few seconds.

### Admin User Enrollment (Companion App)
To enroll an additional Admin (e.g., a partner or co-owner), use the [web-companion app](../web-companion/README.md) — there is no button gesture for this:
1. **Initiate:** A logged-in companion-app user uses the **Enroll Fingerprint** panel on the dashboard and chooses Permission **Admin**.
2. **Pending Authorization:** The LED begins to pulse **blue**.
3. **Authorize:** An existing Admin touches the fingerprint sensor within 10 seconds.
4. **Biometric Capture:** The new user places their finger three times.
5. **Completion:** The template is saved with Admin permission (3). The companion app is notified that enrollment has started.

There is no separate "Enroll Admin" request: enrolling with Permission = Admin through the Enroll Fingerprint panel is the supported path (the remote enrolment request accepts permission 3 directly).

> [!IMPORTANT]
> Adding an Admin gives that person full control over the device, including the ability to enroll/remove other users, pair to Nuki, join Zigbee networks, and factory reset the device. Only grant Admin to trusted individuals.

### Remote Enrollment via Zigbee (Optional)
If the device has completed Phase 3 (Zigbee Join), additional users can be enrolled remotely:
1. **Trigger:** The coordinator sends an enrollment command specifying the desired User ID and permission level.
2. **Biometric Capture:** The device enters enrollment mode. The LED flashes **blue** and the new user places their finger three times.
3. **Completion:** The result (success or error) is reported back to the coordinator.

> [!NOTE]
> Remote enrollment via Zigbee still requires physical presence at the sensor for the biometric capture — it only skips the button press and Admin fingerprint authorization step.

### Enrollment Flow Summary

```
Admin requests via app     ──► LED pulses Blue ──► Admin scans finger ──► New user scans 3x ──► LED Green ✓
   or Zigbee coordinator        (Pending Auth)       (Authorization)       (Capture)           (Saved)
```

| Enrollment Method | Trigger | Permission Assigned | Admin Auth Required |
| --- | --- | --- | --- |
| Standard User | Companion-app request | 1 (Standard) | Yes — Admin fingerprint |
| Admin User | Companion-app request (Enroll Fingerprint, Permission = Admin) | 3 (Admin) | Yes — Admin fingerprint |
| First-time setup (initial Admin) | Setup wizard, step 1 | 3 (Admin) | No — setup phase is the gate |
| Remote (Zigbee) | n/a | Specified by coordinator | No (trusted network) |

## Companion App User Management

After setup completes, the companion app is the supported surface for
managing enrolled users on a claimed device. Open the dashboard's **User
Management** section and tap **Refresh Users** to list every enrolled user
with their ID, name and permission.

Every change is authorized by a **live Admin fingerprint scan on the device**
— a BLE request alone is never enough:

| Action | Scans required |
| --- | --- |
| List users | None (Admin session only) |
| Enroll a new user | 1 authorizing Admin scan, then the new user scans 3 times |
| Rename a user | 1 authorizing Admin scan |
| Change permission | 1 authorizing Admin scan (may take up to ~15 seconds) |
| Delete a user | 1 authorizing Admin scan |

The app tells you before submitting what scans are needed and shows the
request as waiting until the device reports its outcome. Refusals are
rendered specifically: *last admin* (the change would leave no admin),
*name taken* (another user holds that name), *id occupied*, *busy* (retry in
a moment), *denied* (the scanned finger was not an Admin), or *timed out*
(no one scanned within 10 seconds).

You may delete or demote your own user — only the last-admin rule refuses.
The app warns you first: after such a change your session loses its
authority at its next restricted action.

## Companion App Device Health

The dashboard's **Device Health** section shows what the device knows about
itself right now: lock state, battery, active alarms, fingerprint sensor
readiness, the Nuki link, the Zigbee network, firmware version, OTA state and
setup state. It is available to **any signed-in user**, not only an admin,
and it updates on its own as values change — you never need to reload.

Every field shows one of three things:

- **A real reading.** The device measured or was told this value. A lock
  state or battery level also says how old the reading is when it is old
  enough to mislead.
- **Unknown.** The device holds *no* reading — for example the battery
  measurement failed, or nothing has reported since boot. Unknown means
  unknown: no number stands in for it.
- **N/A (not applicable).** The subsystem does not exist in this build or
  configuration — a Zigbee-disabled build shows its network state this way.
  This is different from unknown: nothing failed; there is simply nothing
  there to read.

### Why an assumed lock state looks different

When you send a lock or unlock command, the bridge immediately records the
state it *expects* — but until the lock itself confirms, that value is an
assumption, not evidence. The app marks such a state with
*(awaiting confirmation)*. When the lock reports back, the marking
disappears: from then on the state shown is one the lock confirmed.

### Battery

The battery percentage is always the device's own latest measurement — never
a configured default. If no measurement is available it shows **Unknown**,
and the OTA pre-flight warning will say the battery level is unknown rather
than implying it was checked.

## Permission Model

Each enrolled user is assigned a permission level:

| Permission | Role | Capabilities |
| --- | --- | --- |
| 1 | Standard User | Can unlock the door with their fingerprint. |
| 2 | Elevated User | Reserved for future use (same as Standard for now). |
| 3 | Admin | Can unlock the door **and** authorize configuration actions (enrollment, Nuki pairing, Zigbee join, factory reset). |

The first enrolled user (User ID 1) is always created with Admin privileges (Permission 3). All subsequent users default to Standard (Permission 1) unless explicitly enrolled as Admin.

## User Capacity

The fingerprint sensor supports User IDs from `1` to `500`. When a new user is enrolled locally, the firmware automatically assigns the **lowest available User ID**.

If all User IDs are occupied, the LED flashes **red** and enrollment is rejected.

## Security & Lockouts

To prevent brute-force attacks, the lock tracks consecutive failed attempts.
- **Threshold:** 5 consecutive failed attempts within 60 seconds triggers a lockout.
- **Lockout Duration:** 2 minutes (120 seconds) where the fingerprint sensor ignores all inputs.
- **Zigbee Alarm:** An alarm is sent to the coordinator when the lockout is triggered.

## Factory Reset

Performing a factory reset erases all persistent data (NVS keys and security state), removes all enrolled fingerprint users, clears the setup-completion latch, leaves the Zigbee network, resets in-memory services, and reboots the device into the **setup phase** — ready to be claimed again by any companion app.

A factory reset can be triggered via two methods:

1. **Hardware Button:** Hold the Configuration Button for **8 seconds**. The reset executes directly — **no Admin fingerprint is required**, because it must remain possible to recover a device whose Admin fingerprint can no longer be read.
2. **USB-C CLI Interface:** Authenticate via `login <password>`, then execute `factory_reset YES`.

> [!CAUTION]
> Factory reset is destructive and non-reversible. All enrolled fingerprints and paired credentials will be permanently erased.

> [!WARNING]
> Because factory reset needs no fingerprint, **anyone who can reach and hold the button can wipe the device** (denial of service — they still cannot pair your Nuki or unlock your door). Mount the device so the button is on the interior side of the door, not reachable from outside an enclosure.


## LED Color Reference

| LED State | Meaning |
| --- | --- |
| Breathing Blue | Pending user enrollment (awaiting Admin auth) |
| Breathing Yellow | Pending Nuki pairing (awaiting Admin auth) |
| Breathing Purple | Pending Zigbee join (awaiting Admin auth) |
| Breathing Red | Pending factory reset (awaiting Admin auth) |
| Flash Cyan | BLE Companion pairing window open / setup phase active |
| Flash Blue | Enrollment mode active (biometric capture) |
| Flash Green | Successful enrollment step |
| Solid Green | Enrollment complete / pairing successful |
| Flash Red | Error or timeout |
| Flash Orange | Low battery warning |

## Timeouts

- **Setup arm window:** 5 minutes of unfiltered advertising with no client connected.
- **Setup deadline:** 10 minutes from the first accepted setup connection; not extended by activity, progress, or reconnection. A button press restarts both timers.
- **Setup connection idle:** 2 minutes without GATT activity drops the connection (the deadline keeps running).
- **Admin Action Authorization:** 10 seconds to provide Admin fingerprint after a gated request.
- **Fingerprint Match Cooldown:** 3 seconds between match attempts.
- **Fingerprint Sensor UART Timeout:** 12 seconds for sensor response.

## USB-C CLI Commands

The device provides a USB-C serial console (USB-Serial-JTAG) for local device management, diagnostics, and provisioning without a Zigbee coordinator.

### Accessing the CLI
1. Connect the device to a computer via USB-C.
2. Open a serial terminal (e.g., `screen`, `minicom`, `putty`) at **115200 baud**, 8N1.
3. You will see the `SDF>` prompt.

### Authentication
Most commands require an authenticated session:
```
login <password>
```
The default password is configured at build time (see `CONFIG_SDF_CLI_PASSWORD`). After successful login, the session remains active for a configurable idle timeout (default 300 seconds). Use `logout` to end the session early.

### User Management
Requires: `login` (CLI authentication)

| Command | Description | Auth |
|---------|-------------|------|
| `user list` | List all enrolled users (ID, Permission name) | CLI login |
| `user get <id>` | Show details for a specific user | CLI login |
| `user add <id> <perm>` | Enroll new user locally (3 scans; no Admin FP needed — the enrolment starts directly and the three scans are the capture) | CLI login |
| `user del <id>` | Delete user directly (no Admin FP gate; refused if it would remove the last remaining admin) | CLI login |
| `user permission <id> <perm>` | Change user permission level (requires Admin FP scan to authorize) | CLI login + Admin FP |

Note the difference from the companion app: on the console, `user add` arms
the enrolment directly and `user del` performs the deletion directly —
neither goes through an Admin-fingerprint authorization step (the console
itself is behind your CLI login). Over BLE, by contrast, every state-changing
verb requires a live Admin fingerprint scan.

**Permission levels:** 1 = Standard, 2 = Elevated, 3 = Admin

**Example:**
```
SDF> login mypassword
Authenticated
SDF> user add 42 1
Enrolling user 42 with permission 1: 3 fingerprint scans required.
Place finger on sensor (scan 1 of 3)...
Scan 1 OK.
Remove finger and place again for next scan.
...
User 42 enrolled successfully with permission 1 (Standard).
```

### Nuki Management
Requires: `login` (CLI authentication)

| Command | Description | Auth |
|---------|-------------|------|
| `nuki status` | Show pairing status, auth ID, BLE transport state | CLI login |
| `nuki connect` | Initiate BLE connection to paired Nuki lock | CLI login |
| `nuki pair` | Pair with Nuki lock (requires Nuki in pairing mode, Admin FP) | CLI login + Admin FP |
| `nuki unpair` | Unpair from Nuki lock, clear credentials (requires Admin FP) | CLI login + Admin FP |

**Pairing flow:**
1. Put Nuki lock into pairing mode (hold button 5s until LED solid).
2. Run `nuki pair` and authorize with Admin fingerprint.
3. On success, authorization ID is displayed and stored.

### Zigbee Management
Requires: `login` (CLI authentication)

| Command | Description | Auth |
|---------|-------------|------|
| `zigbee status` | Show network status (enabled, joined, PAN ID, channel, address) | CLI login |
| `zigbee connect` | Start network steering / permit join (requires Admin FP) | CLI login + Admin FP |
| `zigbee unpair` | Leave network and clear NVRAM (requires Admin FP) | CLI login + Admin FP |

> [!NOTE]
> If Zigbee is disabled in the build config (`CONFIG_ZB_ENABLED=n`), status will show "Disabled in build config" and connect/unpair will return an error.

### Factory Reset
Requires: `login` (CLI authentication)

| Command | Description | Auth |
|---------|-------------|------|
| `factory_reset YES` | Complete factory reset (erases NVS, users, Zigbee, reboots) | CLI login |

> [!CAUTION]
> Factory reset is destructive and non-reversible. All enrolled fingerprints and paired credentials will be permanently erased.

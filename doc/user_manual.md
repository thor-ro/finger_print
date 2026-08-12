# Smart Door Fingerprint Lock User Manual

## Overview
This Smart Door lock integrates a biometric fingerprint sensor directly into your Zigbee Smart Home ecosystem (e.g., Home Assistant). You can manage your fingerprint database using standard Zigbee commands, or via a local hardware button.

The lock supports three progressive setup phases:
1. **Phase 1 — Claiming Device Ownership:** Enroll the first Admin user (required before any other action).
2. **Phase 2 — Pairing to Nuki Lock (BLE):** Connect the lock to your physical Nuki Smart Lock.
3. **Phase 3 — Joining Smart Home (Zigbee, optional):** Integrate with Home Assistant or other Zigbee coordinators.

## First-Time Setup (Out of the Box)

### Phase 1: Claiming Device Ownership

When the lock is powered on for the first time (or after a factory reset), it enters the **Unclaimed** state with **0 enrolled users**. The LED breathes **white** to indicate this state.

1. **Initiate Admin Enrollment:** Press the Configuration Button **once (Short Press)**. The LED begins to pulse **blue**.
2. **Biometric Capture:** Place your finger on the sensor **three consecutive times**. After each successful scan, the LED flashes **green**.
3. **Completion:** Upon the third scan, your fingerprint is saved as **User ID 1** with **Admin privileges (Permission 3)**. The LED breathes solid **green** for a few seconds to confirm. The device is now **Claimed**.

> [!IMPORTANT]
> This first enrollment cannot be skipped. The device will not accept Nuki pairing, Zigbee join, or additional user enrollment until an Admin (User ID 1) is enrolled.

### Phase 2: Pairing to the Nuki Lock (BLE)

Once claimed, you can pair the SDF to your Nuki Smart Lock.

1. **Prepare Nuki:** Press and hold the button on your Nuki Smart Lock for 5 seconds until its LED circle glows constantly (Pairing Mode).
2. **Initiate Nuki Pairing on SDF:** Press the SDF Configuration Button **once (Short Press)**. Since the device is claimed but Nuki isn't paired yet, Short Press means "pair Nuki" at this stage (see [Configuration Button Mapping](#configuration-button-mapping) below). The LED begins to pulse **yellow** (Awaiting Admin Auth).
3. **Authorize:** The Admin (User ID 1) touches the fingerprint sensor within 10 seconds to authorize the action.
4. **Active Pairing:** Once authorized, the LED flashes **rapid yellow**. The SDF connects to the Nuki over BLE, negotiates the shared key, and saves the credentials to NVS.
5. **Completion:** The LED glows solid **green** to confirm successful pairing. The user can now unlock the door with their fingerprint.

### Phase 3: Joining the Smart Home (Optional Zigbee)

If you want remote user management, battery monitoring, and logging through Home Assistant:

1. **Prepare Coordinator:** Enable "Permit Join" on your Zigbee coordinator (e.g., Zigbee2MQTT).
2. **Initiate Zigbee Join on SDF:** Press and hold the SDF Configuration Button for **3 seconds**. The LED begins to pulse **purple** (Awaiting Admin Auth).
3. **Authorize:** The Admin (User ID 1) touches the fingerprint sensor to authorize the network join.
4. **Active Joining:** The LED flashes **rapid purple** as the device attempts to steer to the network.
5. **Completion:** The LED glows solid **green** and the device becomes available in the Smart Home dashboard.

## Configuration Button Mapping

The device distinguishes between actions based on **how long the Configuration Button is pressed** and the **current device state** (Unclaimed vs. Claimed). Short Press is **state-dependent**: its meaning is resolved at the moment of the press rather than fixed.

| Action / Duration | State Condition | Authentication Required | LED Color | Result |
| --- | --- | --- | --- | --- |
| **Short Press** | Unclaimed (0 users) | None | n/a | Starts Admin Enrollment (User 1, Permission 3) |
| **Short Press** | Claimed, Nuki not yet paired | Admin Fingerprint | Yellow pulse | Enters BLE Nuki Pairing Mode |
| **Short Press** | Claimed, Nuki already paired | Admin Fingerprint | Blue pulse | Starts Standard User Enrollment (Permission 1) |
| **Triple Press** | Claimed | Admin Fingerprint | Blue pulse | Starts Admin User Enrollment (Permission 3) |
| **Hold 3 sec** | Claimed | Admin Fingerprint | Purple pulse | Enters Zigbee Network Steering |
| **Hold 8 sec** | Any | Admin Fingerprint | Red pulse | Factory Reset |

> [!NOTE]
> Double Press is no longer mapped to any action — it previously triggered Nuki Pairing, but that gesture has been retired in favor of the state-dependent Short Press behavior above. The gesture is free for future use.

> [!NOTE]
> All actions except the initial Admin Enrollment require Admin fingerprint verification within 10 seconds. If no fingerprint is provided, the LED flashes **red** and the device returns to idle.

### Re-pairing Nuki After Setup Is Complete

Once Nuki has been paired, Short Press no longer offers a path to Nuki pairing (it reverts to standard user enrollment, as above). If the lock ever needs to be re-paired — for example after replacing the Nuki Smart Lock — there are two ways to trigger it:

1. **Factory Reset:** A full factory reset clears the Nuki credentials and returns the device to the Unclaimed state, re-opening the Short-Press-pairs-Nuki step of the first-time setup sequence on the next admin enrollment.
2. **BLE Companion App:** Log in to the [web-companion app](../web-companion/README.md), and use the **Request Nuki Re-pair** button on the dashboard. This sends a re-pairing request over Bluetooth, which the device only accepts if you're already logged in and setup is complete. As with the original pairing, a BLE request alone is never enough — an Admin must scan their fingerprint on the physical device within 10 seconds to authorize it. The app shows a pending status while waiting, and reports back whether the request was authorized, denied, or timed out.

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

If your coordinator goes offline or you prefer physical access, you can enroll users locally.

### Standard User Enrollment (Button)
1. **Initiate:** Press the Configuration Button **once (Short Press)** while the device is claimed (≥1 enrolled user).
2. **Pending Authorization:** The LED begins to pulse **blue**, waiting for Admin authorization.
3. **Authorize:** The Admin (any user with Permission 3) touches the fingerprint sensor within 10 seconds.
   - If a non-Admin fingerprint is scanned, the LED flashes **red** and the action is rejected.
   - If no fingerprint is provided within 10 seconds, the LED flashes **red** and the device returns to idle.
4. **Biometric Capture:** Once authorized, the new user places their finger on the sensor **three consecutive times**. After each successful scan, the LED flashes **green**.
5. **Completion:** After the third scan, the template is saved with the next available User ID and Standard permission (1). The LED breathes solid **green** for a few seconds.
6. **Ready:** The new user can now unlock the door with their fingerprint.

### Admin User Enrollment (Button)
To enroll an additional Admin (e.g., a partner or co-owner):
1. **Initiate:** Press the Configuration Button **three times rapidly (Triple Press)**.
2. **Pending Authorization:** The LED begins to pulse **blue**.
3. **Authorize:** The Admin touches the fingerprint sensor within 10 seconds.
4. **Biometric Capture:** The new user places their finger three times.
5. **Completion:** The template is saved with Admin permission (3).

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
Admin presses button ──► LED pulses Blue ──► Admin scans finger ──► New user scans 3x ──► LED Green ✓
     (Short/Triple)        (Pending Auth)       (Authorization)       (Capture)           (Saved)
```

| Enrollment Method | Button Gesture | Permission Assigned | Admin Auth Required |
| --- | --- | --- | --- |
| Standard User (local) | Short Press | 1 (Standard) | Yes — Admin fingerprint |
| Admin User (local) | Triple Press | 3 (Admin) | Yes — Admin fingerprint |
| Remote (Zigbee) | n/a | Specified by coordinator | No (trusted network) |

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

Performing a factory reset erases all persistent data (NVS keys and security state), removes all enrolled fingerprint users, leaves the Zigbee network, resets in-memory services, and reboots the device into the **Unclaimed** state.

A factory reset can be triggered via two methods:

1. **Hardware Button:** Hold the Configuration Button for **8 seconds** until the LED pulses red, then scan an Admin fingerprint within 10 seconds to authorize.
2. **USB-C CLI Interface:** Authenticate via `login <password>`, then execute `factory_reset YES`.

> [!CAUTION]
> Factory reset is destructive and non-reversible. All enrolled fingerprints and paired credentials will be permanently erased.


## LED Color Reference

| LED State | Meaning |
| --- | --- |
| Breathing White | Device is Unclaimed (0 enrolled users) |
| Breathing Blue | Pending user enrollment (awaiting Admin auth) |
| Breathing Yellow | Pending Nuki pairing (awaiting Admin auth) |
| Breathing Purple | Pending Zigbee join (awaiting Admin auth) |
| Breathing Red | Pending factory reset (awaiting Admin auth) |
| Flash Blue | Enrollment mode active (biometric capture) |
| Flash Green | Successful enrollment step |
| Solid Green | Enrollment complete / pairing successful |
| Flash Red | Error or timeout |
| Flash Orange | Low battery warning |

## Timeouts

- **Admin Action Authorization:** 10 seconds to provide Admin fingerprint after button press.
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
| `user add <id> <perm>` | Enroll new user locally (3 scans, requires Admin FP) | CLI login + Admin FP |
| `user del <id>` | Delete user (requires Admin FP) | CLI login + Admin FP |
| `user permission <id> <perm>` | Change user permission level (requires Admin FP) | CLI login + Admin FP |

**Permission levels:** 1 = Standard, 2 = Elevated, 3 = Admin

**Example:**
```
SDF> login mypassword
Authenticated
SDF> user add 42 1
Scan an admin fingerprint to authorize enrollment of user 42 with permission 1...
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

# First-Time Flow Concept (Out of the Box Experience)

This document outlines the optimal initial setup flow for a brand-new or factory-reset Smart Door Fingerprint (SDF) lock. The primary goal is to provide a seamless, secure, and logical progression from unboxing to a fully functional access control system.

## 1. Core Principles
- **Security First:** The device must never be left in a state where a malicious actor can claim ownership or pair it to their own Nuki/Zigbee network.
- **Offline Capable:** The user must be able to set up the core functionality (fingerprint unlocking of the Nuki lock) completely offline, without a Zigbee coordinator or smartphone app.
- **Admin Authorization:** Any state-changing command beyond the initial unboxing requires biometric authorization by the device Administrator (User ID 1).

## 2. Optimal Setup Order

The setup is structured into three progressive phases.

### Phase 1: Claiming Device Ownership (Local Admin Enrollment)
**Goal:** Establish the first authorized user (Admin) who will have the right to configure the rest of the system.
**Why First?** Enrolling the Admin immediately secures the device. Without this step, an attacker could power on the device and pair it with their own systems. Entering Nuki or Zigbee pairing mode is protected by this Admin fingerprint.

1. **Power On:** The device is powered via battery or mains. The LED breathes **White**, indicating it is in a "Factory Reset / Unclaimed" state.
2. **Initiate Setup:** The user presses the physical Configuration Button once. The LED begins to flash **Blue**.
3. **Biometric Capture:** The user places their finger on the sensor three consecutive times. After each successful scan, the LED flashes green.
4. **Completion:** Upon the third scan, the template is generated and saved as `User ID 1` with `Admin` privileges (Permission: 3). The LED breathes solid **Green** for a few seconds to confirm. The device is now "Claimed".

### Phase 2: Pairing to the Nuki Lock (BLE)
**Goal:** Connect the SDF to the physical door mechanism so it can actually unlock the door.
**Why Second?** This provides the core utility of the device (opening the door). It also tests the most critical link in the system. Since we mapped an all-zero target address (as per `nuki-pairing-usage.md`), the SDF can automatically discover a Nuki in pairing mode.

1. **Prepare Nuki:** The user presses and holds the button on their Nuki Smart Lock for 5 seconds until its LED circle glows constantly (Pairing Mode).
2. **Initiate Nuki Pairing on SDF:** 
   - The user presses the SDF Configuration Button **twice rapidly (Double Press)**.
   - The LED begins to pulse **Yellow** (Awaiting Admin Auth).
3. **Authorize:** The Admin (User ID 1) touches the fingerprint sensor to authorize the action.
4. **Active Pairing:** Once authorized, the LED flashes **Rapid Yellow**. The SDF connects to the Nuki over BLE, negotiates the shared key, and saves the credentials to NVS.
5. **Completion:** The LED glows solid **Green** to confirm successful pairing. The user can now unlock the door with their fingerprint.

### Phase 3: Joining the Smart Home (Optional Zigbee)
**Goal:** Integrate the SDF into a Zigbee network (e.g., Home Assistant) for remote user management, battery monitoring, and logging.
**Why Last?** Zigbee is an advanced feature and often not strictly required for standard usage. Separating this step ensures users who only want an offline fingerprint lock are not forced to connect to a coordinator.

1. **Prepare Coordinator:** The user enables "Permit Join" on their Zigbee coordinator (e.g., Zigbee2MQTT).
2. **Initiate Zigbee Join on SDF:**
   - The user presses and holds the SDF Configuration Button for **3 seconds**.
   - The LED begins to pulse **Purple** (Awaiting Admin Auth).
3. **Authorize:** The Admin (User ID 1) touches the fingerprint sensor to authorize the network join.
4. **Active Joining:** The LED flashes **Rapid Purple** as the device attempts to steer to the network.
5. **Completion:** The LED glows solid **Green** and the device becomes available in the Smart Home dashboard.

## 3. Configuration Button Mapping Summary

The device distinguishes between adding a new user, pairing to Nuki, and joining Zigbee based on **how long the physical Configuration Button is pressed**. 

The initial button press tells the device *what* action you want to perform and places the device into a "Pending Authorization" state for that specific action, indicated by a unique LED color. The subsequent Admin fingerprint scan simply answers "Are you allowed to do this?".

| Action / Duration | State Condition | Authentication Required | Resulting Pending State (LED) | Action after Admin Auth |
| --- | --- | --- | --- | --- |
| **Short Press** | Unclaimed (0 users) | None | n/a | Starts Admin Enrollment (Phase 1) |
| **Short Press** | Claimed (>0 users) | Admin Fingerprint | `PENDING_USER_ENROLL` (Pulse Blue) | Starts Standard User Enrollment |
| **Triple Press** | Claimed | Admin Fingerprint | `PENDING_USER_ENROLL` (Pulse Blue) | Starts Admin User Enrollment (Permission 3) |
| **Double Press** | Claimed | Admin Fingerprint | `PENDING_NUKI_PAIR` (Pulse Yellow) | Enters BLE Nuki Pairing Mode (Phase 2) |
| **Hold 3 sec** | Claimed | Admin Fingerprint | `PENDING_ZB_JOIN` (Pulse Purple) | Enters Zigbee Network Steering (Phase 3) |
| **Hold 8 sec** | Any | Admin Fingerprint | `PENDING_FACTORY_RESET` (Pulse Red) | Factory Reset (Wipes users, Nuki keys, Zigbee) |

> [!NOTE] 
> Because the Configuration Button requires Admin verification for nearly all actions, the device is highly secure against physical tampering after the initial setup.

## 4. Edge Cases and Considerations
- **Lost Admin Fingerprint:** If the Admin fingerprint is unreadable, and no Zigbee coordinator is connected to remotely add another Admin, the user must perform a hard factory reset. For security, a true "Hard Reset" without the Admin fingerprint might require a special hardware procedure (e.g., holding the button while powering on the device).
- **Timeouts:** If the user performs an action (e.g., Double Press) but does not provide an Admin fingerprint within 10 seconds, the LED flashes **Red** and the device returns to sleep.

## 5. Adding Additional Users

After Phase 1 is complete and the device is claimed, the Admin can enroll additional users. The SDF supports two enrollment paths depending on the desired privilege level and whether Zigbee is connected.

### 5.1 Permission Model

Each enrolled user is assigned a permission level between 1 and 3:

| Permission | Role | Capabilities |
| --- | --- | --- |
| 1 | Standard User | Can unlock the door with their fingerprint. |
| 2 | Elevated User | Reserved for future use (same as Standard for now). |
| 3 | Admin | Can unlock the door **and** authorize configuration actions (enrollment, Nuki pairing, Zigbee join, factory reset). |

The first enrolled user (User ID 1) is always created with Admin privileges (Permission 3). All subsequent users default to Standard (Permission 1) unless explicitly enrolled as Admin.

### 5.2 User Capacity

The fingerprint sensor supports User IDs from `1` to `0x0FFF` (4095). When a new user is enrolled locally, the firmware automatically assigns the **lowest available User ID** by scanning for gaps in the currently occupied IDs.

If all User IDs are occupied, the LED flashes **Red** and enrollment is rejected.

### 5.3 Local Enrollment — Standard User (Button)

This is the primary method for adding household members, guests, or other people who should be able to unlock the door.

1. **Initiate:** The Admin presses the Configuration Button **once (Short Press)** while the device is claimed (≥1 enrolled user).
2. **Pending Authorization:** The LED begins to pulse **Blue**, indicating the device is in `PENDING_USER_ENROLL` state and waiting for Admin authorization.
3. **Authorize:** The Admin (any user with Permission 3) touches the fingerprint sensor within 10 seconds. The sensor verifies the fingerprint matches an Admin template.
   - If a non-Admin fingerprint is scanned, the LED flashes **Red** and the action is rejected. The device remains in the pending state until timeout.
   - If no fingerprint is provided within 10 seconds, the LED flashes **Red** and the device returns to idle.
4. **Biometric Capture:** Once authorized, the LED flashes **Blue** and the new user places their finger on the sensor **three consecutive times**. After each successful scan, the LED flashes **Green**.
   - The new user should **lift their finger** between scans. If the same image is detected, the step is retried automatically.
5. **Completion:** After the third scan, the template is generated and saved with the next available User ID and Standard permission (1). The LED breathes solid **Green** for a few seconds to confirm.
6. **Ready:** The new user can now unlock the door with their fingerprint.

### 5.4 Local Enrollment — Admin User (Button)

To enroll an additional Admin (e.g., a partner or co-owner who should also be able to configure the device):

1. **Initiate:** The Admin presses the Configuration Button **three times rapidly (Triple Press)**.
2. **Pending Authorization:** The LED begins to pulse **Blue**, indicating the device is in `PENDING_USER_ENROLL` state (same visual as standard enrollment).
3. **Authorize:** The Admin touches the fingerprint sensor within 10 seconds.
4. **Biometric Capture:** Identical to Standard User enrollment — the new user places their finger three times.
5. **Completion:** The template is saved with Admin permission (3). The new Admin can now both unlock the door **and** authorize configuration actions.

> [!IMPORTANT]
> Adding an Admin gives that person full control over the device, including the ability to enroll/remove other users, pair to Nuki, join Zigbee networks, and factory reset the device. Only grant Admin to trusted individuals.

### 5.5 Remote Enrollment via Zigbee (Optional)

If the device has completed Phase 3 (Zigbee Join), additional users can be enrolled remotely through the Smart Home coordinator (e.g., Home Assistant via Zigbee2MQTT). This is useful when the Admin is not physically present at the device.

1. **Trigger:** The coordinator sends an enrollment command specifying the desired User ID and permission level.
2. **Biometric Capture:** The device enters enrollment mode. The LED flashes **Blue** and the new user places their finger three times, as in local enrollment.
3. **Completion:** The result (success or error) is reported back to the coordinator.

> [!NOTE]
> Remote enrollment via Zigbee still requires physical presence at the sensor for the biometric capture — it only skips the button press and Admin fingerprint authorization step.

### 5.6 Enrollment Flow Summary

```
Admin presses button ──► LED pulses Blue ──► Admin scans finger ──► New user scans 3× ──► LED Green ✓
     (Short/Triple)        (Pending Auth)       (Authorization)       (Capture)           (Saved)
```

| Enrollment Method | Button Gesture | Permission Assigned | Admin Auth Required |
| --- | --- | --- | --- |
| Standard User (local) | Short Press | 1 (Standard) | Yes — Admin fingerprint |
| Admin User (local) | Triple Press | 3 (Admin) | Yes — Admin fingerprint |
| Remote (Zigbee) | n/a | Specified by coordinator | No (trusted network) |

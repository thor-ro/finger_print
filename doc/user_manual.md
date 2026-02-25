# Smart Door Fingerprint Lock User Manual

## Overview
This Smart Door lock integrates a biometric fingerprint sensor directly into your Zigbee Smart Home ecosystem (e.g., Home Assistant). You can manage your fingerprint database using standard Zigbee commands, or via a local hardware button.

## Managing Fingerprints via Zigbee
The lock maps standard Zigbee "Door Lock" cluster commands to the fingerprint sensor.

### Enrolling a New Fingerprint
To enroll a new user remotely:
1. Issue the **Set PIN Code** or **Set RFID Code** command from your Zigbee coordinator.
2. Specify a unique `User ID` (between 1 and 500) and an optional permission level (1=Normal, 2=Admin, 3=Super Admin).
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
- It provides a JSON-like array (e.g., `[1:1, 5:3]`) which represents `[UserID:Permission]`.
- This list updates automatically whenever a user is added or removed.

## Local Hardware Enrollment
If your coordinator goes offline or you prefer physical access, you can enroll users locally.

1. **Initiate:** Press and hold the Local Enrollment Button for 3 seconds. The LED will flash blue slowly, entering the `WAIT_ADMIN` state.
2. **Authorize:** An existing Administrator (Permission level 3) must place their finger on the sensor to authorize the new enrollment.
3. **Enroll:** Once authorized, the lock automatically finds the next available `User ID` and enters the standard enrollment mode.
4. Place the new finger on the sensor three times to complete the profile.

## Security & Lockouts
To prevent brute-force attacks, the lock tracks consecutive failed attempts.
- If too many consecutive mismatches occur, the lock will enter a temporary lockout period (default: 2 minutes) where the fingerprint sensor will ignore all inputs.
- A Zigbee alarm is sent to the coordinator when the failed threshold is reached.

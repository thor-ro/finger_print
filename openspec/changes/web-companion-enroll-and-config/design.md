## Context

The web companion currently has a connection view → auth view → dashboard flow. The dashboard has a dead "Read Config" button and no enrollment section. The firmware-side implementation (in `fix-ble-config-enroll-callbacks`) provides the protocol; this change implements the UI layer.

## Goals / Non-Goals

**Goals:**
- Working "Read Config" button that fetches and displays device config as editable fields
- Enrollment panel: trigger enrollment, show which scan (1 of 3) is in progress, show success/failure
- Battery % and lock state status cards shown on connect (populated from config read response or periodic notify)

**Non-Goals:**
- Config persistence (the config is always fetched live from the device)
- Advanced enrollment options (schedule-based access, etc.)

## Decisions

**Single-page app architecture.** The existing view-switching pattern (`switchView()`) is extended with an enrollment sub-section inside the dashboard.

**Config display:** Render as a simple key-value table with inline edit inputs for mutable fields. "Apply" button writes back changed fields as a JSON delta.

**Enrollment flow:**
```
[User ID input] [Permission dropdown: Standard/Elevated/Admin] [Enroll]
  → "Please place your finger on the sensor (1/3)..."
  → "Please place your finger on the sensor (2/3)..."
  → "Please place your finger on the sensor (3/3)..."
  → "✓ User 5 enrolled successfully" / "✗ Enrollment failed"
```
The enrollment notify from the device drives the step counter.

## Risks / Trade-offs

- [MTU] Without MTU negotiation, config JSON may be truncated. The connect flow must request MTU 512 before reading config.
- [Web Bluetooth HTTPS requirement] The companion must be served over HTTPS for Web Bluetooth to work. Already documented in README.

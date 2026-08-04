## Context

`tskIDLE_PRIORITY + 2 = 2` is below ESP-IDF WiFi/lwIP tasks (priority 5+). The OTA task will be starved when calling WiFi and HTTP APIs.

## Goals / Non-Goals

**Goals:**
- Raise OTA task priority so WiFi connection and HTTP download complete reliably

**Non-Goals:**
- Changing OTA download logic or buffer sizes

## Decisions

**Priority: `tskIDLE_PRIORITY + 8` (value = 8).**

Rationale:
- ESP-IDF WiFi tasks run at 5–6; OTA task needs to be above them to process network callbacks
- NimBLE host task runs at priority 9; OTA at 8 yields to BLE gracefully
- ESP-IDF official OTA examples use priority 5–10 for download tasks

Define a named constant: `#define SDF_BLE_OTA_TASK_PRIORITY (tskIDLE_PRIORITY + 8)`

## Risks / Trade-offs

- [No risks] This is a straightforward priority increase within safe bounds.

## 1. Component Setup

- [x] 1.1 Create sdf_event_router component directory structure with include/ and src/
- [x] 1.2 Add component to CMakeLists.txt with proper include and source paths
- [x] 1.3 Add Kconfig options for queue depth and audit logging enable

## 2. Event Router API Implementation

- [x] 2.1 Define sdf_event_type_t enum with 8 event types in sdf_event_router.h
- [x] 2.2 Define sdf_event_priority_t enum with 4 priority levels in sdf_event_router.h
- [x] 2.3 Define sdf_event_t struct with header and payload union in sdf_event_router.h
- [x] 2.4 Implement sdf_event_router_init() with queue and task creation
- [x] 2.5 Implement sdf_event_router_subscribe() with subscriber registration
- [x] 2.6 Implement sdf_event_router_unsubscribe() with handle-based removal
- [x] 2.7 Implement sdf_event_router_emit() for synchronous dispatch
- [x] 2.8 Implement sdf_event_router_emit_async() for queued dispatch
- [x] 2.9 Implement event processing task with priority ordering

## 3. Unit Tests

- [x] 3.1 Create test_sdf_event_router.c with Unity test framework
- [x] 3.2 Test subscribe/emit for biometric match event
- [x] 3.3 Test priority ordering (CRITICAL before HIGH/LOW)
- [x] 3.4 Test async dispatch with queue processing
- [x] 3.5 Test unsubscribe removes handler correctly
- [x] 3.6 Test event payload data integrity

## 4. sdf_services Migration

- [x] 4.1 Add sdf_event_router.h include to sdf_services.c
- [x] 4.2 Emit SDF_EVT_BIOMETRIC_MATCH on successful fingerprint match
- [x] 4.3 Emit SDF_EVT_BIOMETRIC_MATCH_FAILED on failed match
- [x] 4.4 Emit SDF_EVT_SECURITY_LOCKOUT on lockout entered
- [x] 4.5 Emit SDF_EVT_ENROLLMENT_STEP_COMPLETE on enrollment progress
- [x] 4.6 Call existing callbacks alongside events (adapter pattern)

## 5. sdf_app Migration

- [x] 5.1 Add sdf_event_router.h include to sdf_app.c
- [x] 5.2 Subscribe to SDF_EVT_BIOMETRIC_MATCH in app init
- [x] 5.3 Subscribe to SDF_EVT_SECURITY_LOCKOUT in app init
- [x] 5.4 Implement sdf_app_on_event() for event dispatch
- [x] 5.5 Keep adapter pattern callbacks alongside event subscriptions

## 6. Protocol Components Integration

- [x] 6.1 Add event emission to sdf_protocol_zigbee on command receipt
- [x] 6.2 Add event emission to sdf_protocol_ble on connection/state changes
- [x] 6.3 Add event emission to sdf_power on sleep/wake/battery events

## 7. Documentation

- [x] 7.1 Update doc/sdf_sas.md section 5 (Component API) with event router API
- [x] 7.2 Update doc/sdf_sas.md section 6 (Data Flow) with event flow diagram
- [x] 7.3 Update doc/sdf_sas.md section 8 (State Machines) if affected
- [x] 7.4 Update doc/sdf_sas.md section 9 (Integration Points)
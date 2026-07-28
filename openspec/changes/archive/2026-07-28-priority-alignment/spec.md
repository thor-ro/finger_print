## Priority Alignment Specification

### Task Priority Mapping (Resolved)

| Task | Priority Level | Actual Value | Code Constant |
|------|---------------|-------------|---------------|
| sdf_power | NORMAL | 4 | `SDF_POWER_TASK_PRIORITY` |
| sdf_zigbee | HIGH | 5 | `CONFIG_SDF_ZIGBEE_TASK_PRIORITY` |
| sdf_match | HIGH | **5** (was 6) | `SDF_MATCH_TASK_PRIORITY` |
| sdf_enroll | NORMAL | **4** (was 5) | `SDF_ENROLL_TASK_PRIORITY` |
| sdf_admin | HIGH | **5** (was 6) | `SDF_ADMIN_TASK_PRIORITY` |
| sdf_button | NORMAL | **4** (was 5) | `SDF_BUTTON_TASK_PRIORITY` |
| sdf_ota (future) | LOW | 3 | `CONFIG_SDF_OTA_TASK_PRIORITY` |

### Priority Definitions (FreeRTOS 0-6)

```
CRITICAL = 6  (reserved for security lockout events)
HIGH    = 5  (lock actions, Zigbee, admin auth)
NORMAL  = 4  (power mgmt, enrollment, button)
LOW     = 3  (OTA, telemetry)
```

### Verification Points

1. sdf_match runs at HIGH (5), not CRITICAL (6)
2. sdf_admin runs at HIGH (5), not CRITICAL (6)
3. sdf_enroll runs at NORMAL (4), not HIGH (5)
4. sdf_button runs at NORMAL (4), not HIGH (5)
5. No priority inversion risk between match and admin (both HIGH)
6. No priority inversion risk between Zigbee and match (both HIGH, no shared resources)
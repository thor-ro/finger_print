# Design: Split Power Policy from Platform

## Architecture Boundary

The current `sdf_power` component will be split into two separate components with clean separation:

```
┌─────────────────────────────────────────────────────────────────┐
│                        sdf_app (Task)                            │
│  sdf_power_management_task() - orchestrates policy and platform   │
└─────────────────────────────────────────────────────────────────┘
                                │
                ┌───────────────┴───────────────┐
                ▼                           ▼
┌───────────────────────────┐   ┌─────────────────────────────────┐
│   sdf_power_policy        │   │    sdf_platform_power             │
│   (Portable, testable)    │   │    (ESP-IDF / Linux specific)     │
│                           │   │                                 │
│  - Policy evaluation      │   │  - Sleep entry APIs             │
│  - Wake guard logic       │   │  - Wake configuration           │
│  - Battery timing         │   │  - BLE radio gating             │
│  - Callbacks only         │   │  - Retention memory             │
└───────────────────────────┘   └─────────────────────────────────┘
```

## Component: sdf_power_policy

### Purpose
Pure policy logic for sleep/wake decisions. No ESP-IDF dependencies.

### Files
- `include/sdf_power_policy.h` - Public API (no ESP-IDF types)
- `src/sdf_power_policy.c` - Policy implementation
- `test/test_sdf_power_policy.c` - Unity tests

### API Design

```c
// Wake reason for callbacks (portable enum)
typedef enum {
    SDF_POWER_POLICY_WAKE_REASON_TIMER,
    SDF_POWER_POLICY_WAKE_REASON_FINGERPRINT,
    SDF_POWER_POLICY_WAKE_REASON_OTHER,
} sdf_power_policy_wake_reason_t;

// Policy configuration (no ESP-IDF types)
typedef struct {
    uint32_t checkin_interval_ms;
    uint32_t idle_before_sleep_ms;
    uint32_t post_wake_guard_ms;
    uint32_t loop_interval_ms;
    uint32_t battery_report_interval_ms;
    bool enable_light_sleep;
    bool enable_ble_radio_gating;
    bool enable_deep_sleep_fallback;
    int fp_wake_gpio;
    
    // Policy callbacks
    bool (*busy_cb)(void *ctx);
    void (*wake_cb)(void *ctx, sdf_power_policy_wake_reason_t);
    int (*battery_cb)(void *ctx);
    bool (*zigbee_ready_cb)(void *ctx);  // For deep sleep fallback check
    
    void *ctx;
} sdf_power_policy_config_t;

// Decision enum
typedef enum {
    SDF_POWER_POLICY_DECISION_SLEEP_LIGHT,
    SDF_POWER_POLICY_DECISION_SLEEP_DEEP,
    SDF_POWER_POLICY_DECISION_STAY_AWAKE,
} sdf_power_policy_decision_t;

// Public API
void sdf_power_policy_init(const sdf_power_policy_config_t *config);
sdf_power_policy_decision_t sdf_power_policy_evaluate(int64_t now_us, int64_t last_activity_us,
                                              int64_t wake_guard_until_us, int64_t next_battery_report_us);
void sdf_power_policy_mark_activity(void);
uint8_t sdf_power_policy_get_battery_percent(void);
void sdf_power_policy_handle_wake(sdf_power_policy_wake_reason_t reason);
bool sdf_power_policy_is_ready(void);
int64_t sdf_power_policy_get_last_activity_us(void);
int64_t sdf_power_policy_get_wake_guard_until_us(void);
int64_t sdf_power_policy_get_next_battery_report_us(void);
```

## Component: sdf_platform_power

### Purpose
ESP-IDF and Linux-specific sleep/wake implementations.

### Files
- `include/sdf_platform_power.h` - Public API
- `src/sdf_platform_power.c` - ESP-IDF implementation
- `src/sdf_platform_power_linux.c` - Linux mock implementation
- `test/test_sdf_platform_power.c` - Tests

### API Design

```c
typedef enum {
    SDF_PLATFORM_WAKE_TIMER,
    SDF_PLATFORM_WAKE_GPIO,
    SDF_PLATFORM_WAKE_USB,
    SDF_PLATFORM_WAKE_OTHER,
} sdf_platform_wake_reason_t;

typedef enum {
    SDF_PLATFORM_SLEEP_LIGHT,
    SDF_PLATFORM_SLEEP_DEEP,
} sdf_platform_sleep_type_t;

// Platform API
esp_err_t sdf_platform_power_init(void);
esp_err_t sdf_platform_power_enable_timer_wake(uint32_t interval_ms);
esp_err_t sdf_platform_power_enable_gpio_wake(int gpio_num, int level);
esp_err_t sdf_platform_power_disable_all_wake(void);
esp_err_t sdf_platform_power_enter_light(void);
esp_err_t sdf_platform_power_enter_deep(void);
sdf_platform_wake_reason_t sdf_platform_power_get_wake_reason(void);
esp_err_t sdf_platform_power_gate_ble_radio(bool enable);

// Retention (delegates to sdf_platform_sleep)
esp_err_t sdf_platform_power_save_retention(const sdf_power_retention_t *state);
esp_err_t sdf_platform_power_load_retention(sdf_power_retention_t *state);
bool sdf_platform_power_retention_valid(void);
```

## Migration Strategy

### Phase 1: Create new components
1. Create `sdf_platform_power` component with ESP and Linux implementations
2. Create `sdf_power_policy` component with extracted decision logic

### Phase 2: Extract and redirect
3. Extract policy logic from `sdf_power_task` into `sdf_power_policy_evaluate()`
4. Move platform calls to `sdf_platform_power_*()` functions
5. Keep `sdf_power` as thin adapter (deprecated) during transition

### Phase 3: Update integration
6. Update `sdf_app` to initialize both components
7. Move `sdf_power_management_task` to `sdf_app` or new location
8. Update all references to use new APIs

## State Management

The policy component owns all state:
- `last_activity_us` - last activity timestamp
- `wake_guard_until_us` - wake guard expiration
- `next_battery_report_us` - next battery report time
- `battery_percent` - cached battery percentage

The platform component is stateless - all state passed via parameters or callbacks.

## Wake Reason Mapping

```
ESP-IDF wakeup cause           →  Platform wake reason  →  Policy wake reason
ESP_SLEEP_WAKEUP_TIMER           →  SDF_PLATFORM_WAKE_TIMER   → SDF_POWER_POLICY_WAKE_REASON_TIMER
ESP_SLEEP_WAKEUP_GPIO            →  SDF_PLATFORM_WAKE_GPIO    → SDF_POWER_POLICY_WAKE_REASON_FINGERPRINT
USB Serial JTAG connect          →  SDF_PLATFORM_WAKE_USB     → SDF_POWER_POLICY_WAKE_REASON_OTHER
```
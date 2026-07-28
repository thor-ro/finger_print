# Spec: Platform Power API

## Component: sdf_platform_power

### Public Header: `include/sdf_platform_power.h`

```c
#ifndef SDF_PLATFORM_POWER_H
#define SDF_PLATFORM_POWER_H

#include "esp_err.h"
#include <stdint.h>
#include "sdf_platform_sleep.h"

typedef enum {
    SDF_PLATFORM_WAKE_TIMER = 0,
    SDF_PLATFORM_WAKE_GPIO = 1,
    SDF_PLATFORM_WAKE_USB = 2,
    SDF_PLATFORM_WAKE_OTHER = 3,
} sdf_platform_wake_reason_t;

esp_err_t sdf_platform_power_init(void);
esp_err_t sdf_platform_power_enable_timer_wake(uint32_t interval_ms);
esp_err_t sdf_platform_power_enable_gpio_wake(int gpio_num, int level);
esp_err_t sdf_platform_power_disable_all_wake(void);
esp_err_t sdf_platform_power_enter_light(void);
esp_err_t sdf_platform_power_enter_deep(void);
sdf_platform_wake_reason_t sdf_platform_power_get_wake_reason(void);
esp_err_t sdf_platform_power_gate_ble_radio(bool enable);
esp_err_t sdf_platform_power_save_retention(const sdf_power_retention_t *state);
esp_err_t sdf_platform_power_load_retention(sdf_power_retention_t *state);
bool sdf_platform_power_retention_valid(void);

#endif /* SDF_PLATFORM_POWER_H */
```

### ESP-IDF Implementation: `src/sdf_platform_power.c`

Uses existing `sdf_platform_sleep` APIs for actual sleep operations.

**BLE Radio Gating:**
- Calls `sdf_nuki_ble_set_enabled(transport, false/true)` to gate BLE
- No-op on Linux (CONFIG_IDF_TARGET_LINUX)

**GPIO Wake:**
- Uses `sdf_platform_sleep_enable_gpio_wakeup_light()` for light sleep
- Uses `sdf_platform_sleep_enable_gpio_wakeup_deep()` for deep sleep

### Linux Implementation: `src/sdf_platform_power_linux.c`

Mock implementation for host testing (CONFIG_IDF_TARGET_LINUX):
- `sdf_platform_power_init()` - returns ESP_OK
- `sdf_platform_power_enter_light()` - increments mock counter, calls usleep(1000)
- `sdf_platform_power_enter_deep()` - same as light (no real deep sleep)
- Wake reason tracked in mock variable
- Retention memory stored in static array (max 256 bytes)

### Kconfig
- `CONFIG_IDF_TARGET_LINUX` - selects Linux mock implementation
- No new Kconfig options needed (uses existing sdkconfig values)
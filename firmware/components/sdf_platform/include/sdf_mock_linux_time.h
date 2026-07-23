#ifndef SDF_MOCK_LINUX_TIME_H
#define SDF_MOCK_LINUX_TIME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time_mock(void);
void esp_task_wdt_reset_mock(void);
esp_err_t esp_task_wdt_reconfigure_mock(const void *config);

#define esp_timer_get_time esp_timer_get_time_mock
#define esp_task_wdt_reset esp_task_wdt_reset_mock
#define esp_task_wdt_reconfigure esp_task_wdt_reconfigure_mock

typedef struct {
    uint32_t timeout_ms;
    uint32_t idle_core_mask;
    bool trigger_panic;
} esp_task_wdt_config_t;

#define pdMS_TO_TICKS(x) ((x))
#define portNUM_PROCESSORS 1

void vTaskDelay(uint32_t ticks);

#ifdef __cplusplus
}
#endif

#endif /* SDF_MOCK_LINUX_TIME_H */
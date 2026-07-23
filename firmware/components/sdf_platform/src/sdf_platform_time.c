#include "sdf_platform_time.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include "sdf_mock_linux_time.h"
#endif

int64_t sdf_platform_time_get_us(void) {
    return esp_timer_get_time();
}

uint32_t sdf_platform_time_get_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

int64_t sdf_platform_time_elapsed_us(int64_t start_us) {
    int64_t now = sdf_platform_time_get_us();
    if (now >= start_us) {
        return now - start_us;
    }
    return 0;
}

bool sdf_platform_time_is_timeout(int64_t start_us, uint32_t timeout_us) {
    return sdf_platform_time_elapsed_us(start_us) >= (int64_t)timeout_us;
}

void sdf_platform_time_delay_us(uint32_t us) {
    if (us < 1000) {
        int64_t start = sdf_platform_time_get_us();
        while (sdf_platform_time_elapsed_us(start) < (int64_t)us) {
            // busy wait
        }
    } else {
        vTaskDelay(pdMS_TO_TICKS((us + 999) / 1000));
    }
}

void sdf_platform_time_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void sdf_platform_time_wdt_reset(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reset();
#endif
}

void sdf_platform_time_wdt_feed(uint32_t timeout_ms) {
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reconfigure(&(esp_task_wdt_config_t){
        .timeout_ms = timeout_ms,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true,
    });
#endif
}
#include "sdf_platform_time.h"

#define SDF_PLATFORM_WDT_MAX_TASKS 8
static const char *TAG = "sdf_platform_time";

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void *s_wdt_warned_tasks[SDF_PLATFORM_WDT_MAX_TASKS] = {0};
static portMUX_TYPE s_wdt_warn_mux = portMUX_INITIALIZER_UNLOCKED;
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdf_mock_linux_time.h"
#include "esp_log.h"
#include <pthread.h>
#include <time.h>

int64_t esp_timer_get_time_mock(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}

static void *s_wdt_registered_tasks[SDF_PLATFORM_WDT_MAX_TASKS] = {0};
static void *s_wdt_warned_tasks[SDF_PLATFORM_WDT_MAX_TASKS] = {0};
static uint32_t s_wdt_warning_count = 0;
static pthread_mutex_t s_wdt_lock = PTHREAD_MUTEX_INITIALIZER;
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

void sdf_platform_time_wdt_add(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_add(NULL);
    void *current = (void *)xTaskGetCurrentTaskHandle();
    if (current != NULL) {
        portENTER_CRITICAL(&s_wdt_warn_mux);
        for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
            if (s_wdt_warned_tasks[i] == current) {
                s_wdt_warned_tasks[i] = NULL;
                break;
            }
        }
        portEXIT_CRITICAL(&s_wdt_warn_mux);
    }
#else
    void *current = (void *)xTaskGetCurrentTaskHandle();
    if (current == NULL) {
        return;
    }
    pthread_mutex_lock(&s_wdt_lock);
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        if (s_wdt_warned_tasks[i] == current) {
            s_wdt_warned_tasks[i] = NULL;
            break;
        }
    }
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        if (s_wdt_registered_tasks[i] == current) {
            pthread_mutex_unlock(&s_wdt_lock);
            return;
        }
    }
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        if (s_wdt_registered_tasks[i] == NULL) {
            s_wdt_registered_tasks[i] = current;
            break;
        }
    }
    pthread_mutex_unlock(&s_wdt_lock);
#endif
}

void sdf_platform_time_wdt_delete(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_delete(NULL);
    void *current = (void *)xTaskGetCurrentTaskHandle();
    if (current != NULL) {
        portENTER_CRITICAL(&s_wdt_warn_mux);
        for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
            if (s_wdt_warned_tasks[i] == current) {
                s_wdt_warned_tasks[i] = NULL;
                break;
            }
        }
        portEXIT_CRITICAL(&s_wdt_warn_mux);
    }
#else
    void *current = (void *)xTaskGetCurrentTaskHandle();
    if (current == NULL) {
        return;
    }
    pthread_mutex_lock(&s_wdt_lock);
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        if (s_wdt_registered_tasks[i] == current) {
            s_wdt_registered_tasks[i] = NULL;
            break;
        }
    }
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        if (s_wdt_warned_tasks[i] == current) {
            s_wdt_warned_tasks[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&s_wdt_lock);
#endif
}

void sdf_platform_time_wdt_reset(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_err_t err = esp_task_wdt_reset();
    if (err == ESP_ERR_NOT_FOUND) {
        void *current = (void *)xTaskGetCurrentTaskHandle();
        bool already_warned = false;
        portENTER_CRITICAL(&s_wdt_warn_mux);
        for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
            if (s_wdt_warned_tasks[i] == current) {
                already_warned = true;
                break;
            }
        }
        if (!already_warned) {
            for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
                if (s_wdt_warned_tasks[i] == NULL) {
                    s_wdt_warned_tasks[i] = current;
                    break;
                }
            }
        }
        portEXIT_CRITICAL(&s_wdt_warn_mux);

        if (!already_warned) {
            const char *name = pcTaskGetName(NULL);
            ESP_LOGW(TAG, "Task '%s' reset task watchdog without being registered", name ? name : "unknown");
        }
    }
#else
    void *current = (void *)xTaskGetCurrentTaskHandle();
    pthread_mutex_lock(&s_wdt_lock);
    bool registered = false;
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        if (s_wdt_registered_tasks[i] == current) {
            registered = true;
            break;
        }
    }
    if (!registered) {
        bool already_warned = false;
        for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
            if (s_wdt_warned_tasks[i] == current) {
                already_warned = true;
                break;
            }
        }
        if (!already_warned) {
            for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
                if (s_wdt_warned_tasks[i] == NULL) {
                    s_wdt_warned_tasks[i] = current;
                    break;
                }
            }
            s_wdt_warning_count++;
            pthread_mutex_unlock(&s_wdt_lock);
            const char *name = pcTaskGetName(NULL);
            ESP_LOGW(TAG, "Task '%s' reset task watchdog without being registered", name ? name : "unknown");
            return;
        }
    }
    pthread_mutex_unlock(&s_wdt_lock);
#endif
}

void sdf_platform_time_wdt_feed(uint32_t timeout_ms) {
#ifndef CONFIG_IDF_TARGET_LINUX
    esp_task_wdt_reconfigure(&(esp_task_wdt_config_t){
        .timeout_ms = timeout_ms,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true,
    });
#else
    (void)timeout_ms;
#endif
}

bool sdf_platform_time_wdt_is_registered(void *task_handle) {
#ifndef CONFIG_IDF_TARGET_LINUX
    (void)task_handle;
    return true;
#else
    void *target = task_handle ? task_handle : (void *)xTaskGetCurrentTaskHandle();
    if (target == NULL) {
        return false;
    }
    pthread_mutex_lock(&s_wdt_lock);
    bool found = false;
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        if (s_wdt_registered_tasks[i] == target) {
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_wdt_lock);
    return found;
#endif
}

bool sdf_platform_time_wdt_has_warned(void *task_handle) {
#ifndef CONFIG_IDF_TARGET_LINUX
    (void)task_handle;
    return false;
#else
    void *target = task_handle ? task_handle : (void *)xTaskGetCurrentTaskHandle();
    if (target == NULL) {
        return false;
    }
    pthread_mutex_lock(&s_wdt_lock);
    bool found = false;
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        if (s_wdt_warned_tasks[i] == target) {
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_wdt_lock);
    return found;
#endif
}

uint32_t sdf_platform_time_wdt_get_warning_count(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
    return 0;
#else
    pthread_mutex_lock(&s_wdt_lock);
    uint32_t count = s_wdt_warning_count;
    pthread_mutex_unlock(&s_wdt_lock);
    return count;
#endif
}

void sdf_platform_time_wdt_clear_warned_tasks(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
    portENTER_CRITICAL(&s_wdt_warn_mux);
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        s_wdt_warned_tasks[i] = NULL;
    }
    portEXIT_CRITICAL(&s_wdt_warn_mux);
#else
    pthread_mutex_lock(&s_wdt_lock);
    for (size_t i = 0; i < SDF_PLATFORM_WDT_MAX_TASKS; i++) {
        s_wdt_warned_tasks[i] = NULL;
    }
    s_wdt_warning_count = 0;
    pthread_mutex_unlock(&s_wdt_lock);
#endif
}
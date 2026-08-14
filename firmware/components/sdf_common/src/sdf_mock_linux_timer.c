/**
 * @file sdf_mock_linux_timer.c
 * @brief esp_timer_get_time() implementation for Linux host builds.
 *
 * ESP-IDF's esp_timer component registers as header-only (no SRCS) for
 * IDF_TARGET=linux (see components/esp_timer/CMakeLists.txt), so
 * esp_timer_get_time() is declared but never defined on that target.
 * Several SDF components call it directly (rather than going through
 * sdf_platform's time wrappers), so provide a real monotonic-clock backed
 * definition here for the host build.
 */
#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_LINUX

#include "esp_timer.h"

#include <time.h>

int64_t esp_timer_get_time(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}

esp_err_t esp_timer_create(const esp_timer_create_args_t* create_args,
                           esp_timer_handle_t* out_handle) {
  (void)create_args;
  if (out_handle == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  *out_handle = (esp_timer_handle_t)0x1;
  return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
  (void)timer;
  return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
  (void)timer;
  (void)timeout_us;
  return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
  (void)timer;
  return ESP_OK;
}

#endif /* CONFIG_IDF_TARGET_LINUX */

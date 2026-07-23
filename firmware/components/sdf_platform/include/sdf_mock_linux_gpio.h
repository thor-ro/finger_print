#ifndef SDF_MOCK_LINUX_GPIO_H
#define SDF_MOCK_LINUX_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mock functions for Linux target - only mock what ESP-IDF doesn't provide
// Types are provided by ESP-IDF's hal/gpio_types.h

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_NO_MEM -3
#define ESP_ERR_NOT_SUPPORTED -4
#define ESP_ERR_TIMEOUT 0x107
#define ESP_FAIL -1

esp_err_t gpio_config_mock(const void *config);
esp_err_t gpio_install_isr_service_mock(int flags);
esp_err_t gpio_uninstall_isr_service_mock(void);
esp_err_t gpio_isr_handler_add_mock(int gpio_num, void *isr_handler, void *args);
esp_err_t gpio_isr_handler_remove_mock(int gpio_num);
int gpio_get_level_mock(int gpio_num);
esp_err_t gpio_set_level_mock(int gpio_num, uint32_t level);
esp_err_t gpio_wakeup_enable_mock(int gpio_num, int intr_type);
esp_err_t gpio_wakeup_disable_mock(int gpio_num);

#define gpio_config gpio_config_mock
#define gpio_install_isr_service gpio_install_isr_service_mock
#define gpio_uninstall_isr_service gpio_uninstall_isr_service_mock
#define gpio_isr_handler_add gpio_isr_handler_add_mock
#define gpio_isr_handler_remove gpio_isr_handler_remove_mock
#define gpio_get_level gpio_get_level_mock
#define gpio_set_level gpio_set_level_mock
#define gpio_wakeup_enable gpio_wakeup_enable_mock
#define gpio_wakeup_disable gpio_wakeup_disable_mock

#ifdef __cplusplus
}
#endif

#endif /* SDF_MOCK_LINUX_GPIO_H */
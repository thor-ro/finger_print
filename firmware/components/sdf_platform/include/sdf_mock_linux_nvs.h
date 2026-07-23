#ifndef SDF_MOCK_LINUX_NVS_H
#define SDF_MOCK_LINUX_NVS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_PARTITION_TYPE_DATA = 0,
    ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS = 1,
} esp_partition_type_t;

typedef struct esp_partition_t esp_partition_t;

typedef enum {
    NVS_READWRITE = 0,
    NVS_READONLY = 1,
} nvs_open_mode_t;

typedef uint32_t nvs_handle_t;

typedef struct {
    uint8_t eky[32];
} nvs_sec_cfg_t;

esp_err_t nvs_flash_init_mock(void);
esp_err_t nvs_flash_erase_mock(void);
esp_err_t nvs_flash_deinit_mock(void);
esp_err_t nvs_flash_read_security_cfg_mock(const esp_partition_t *partition, nvs_sec_cfg_t *cfg);
esp_err_t nvs_flash_generate_keys_mock(const esp_partition_t *partition, nvs_sec_cfg_t *cfg);

#define nvs_flash_init nvs_flash_init_mock
#define nvs_flash_erase nvs_flash_erase_mock
#define nvs_flash_deinit nvs_flash_deinit_mock
#define nvs_flash_read_security_cfg nvs_flash_read_security_cfg_mock
#define nvs_flash_generate_keys nvs_flash_generate_keys_mock

typedef enum {
    ESP_OK = 0,
    ESP_ERR_NVS_NO_FREE_PAGES = -1,
    ESP_ERR_NVS_NEW_VERSION_FOUND = -2,
    ESP_ERR_NVS_KEYS_NOT_INITIALIZED = -3,
    ESP_ERR_NVS_CORRUPT_KEY_PART = -4,
    ESP_ERR_NVS_WRONG_ENCRYPTION = -5,
} esp_err_t;

const esp_partition_t *esp_partition_find_first_mock(esp_partition_type_t type,
                                                      uint32_t subtype,
                                                      const char *label);

#define esp_partition_find_first esp_partition_find_first_mock

#ifdef __cplusplus
}
#endif

#endif /* SDF_MOCK_LINUX_NVS_H */
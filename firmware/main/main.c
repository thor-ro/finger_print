#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdf_app.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t err = sdf_app_init();
    if (err != ESP_OK) {
        /* A subsystem failed to come up (e.g. fingerprint sensor, BLE,
         * Zigbee, or power manager). Keep running in a degraded state
         * rather than halting or rebooting - a partially-working door
         * lock is still better than a bricked one - but make the
         * degraded boot loudly, unmistakably visible in the logs. */
        ESP_LOGE(TAG,
                 "*** DEGRADED BOOT: sdf_app_init() failed (%s); device is "
                 "running with one or more subsystems disabled ***",
                 esp_err_to_name(err));
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

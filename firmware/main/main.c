#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdf_app.h"

void app_main(void)
{
    sdf_app_init();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

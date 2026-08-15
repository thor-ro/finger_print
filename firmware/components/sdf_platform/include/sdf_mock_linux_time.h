#ifndef SDF_MOCK_LINUX_TIME_H
#define SDF_MOCK_LINUX_TIME_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time_mock(void);

#define esp_timer_get_time esp_timer_get_time_mock

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(x) ((x))
#endif
#ifndef portNUM_PROCESSORS
#define portNUM_PROCESSORS 1
#endif

#ifdef __cplusplus
}
#endif

#endif /* SDF_MOCK_LINUX_TIME_H */
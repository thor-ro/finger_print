#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the USB-C Command Line Interface (CLI)
 *
 * This initializes the esp_console over USB-Serial-JTAG and registers standard commands.
 * It also registers the SDF-specific commands (user, nuki, zigbee) and the login/logout mechanism.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sdf_cli_init(void);

/**
 * @brief Register all CLI commands.
 */
void sdf_cli_register_commands(void);

/**
 * @brief Check if the current CLI session is authenticated.
 *
 * @return true if authenticated, false otherwise.
 */
bool sdf_cli_is_authenticated(void);

/**
 * @brief Set the CLI session as authenticated and reset the idle timer.
 */
void sdf_cli_authenticate(void);

/**
 * @brief Clear the CLI session authentication.
 */
void sdf_cli_logout(void);

#ifdef __cplusplus
}
#endif

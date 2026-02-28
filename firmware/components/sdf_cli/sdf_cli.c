#include "sdf_cli.h"
#include "argtable3/argtable3.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/usb_serial_jtag.h"
#endif
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/timers.h"
#include "linenoise/linenoise.h"
#include <stdio.h>
#include <string.h>

// Define the password config internally if for some reason it's not set
#ifndef CONFIG_SDF_CLI_PASSWORD
#define CONFIG_SDF_CLI_PASSWORD "admin123"
#endif

// Timeout in milliseconds (2 minutes)
#define CLI_IDLE_TIMEOUT_MS (2 * 60 * 1000)

static const char *TAG = "sdf_cli";

static bool s_is_authenticated = false;
static TimerHandle_t s_idle_timer = NULL;

static void cli_idle_timer_cb(TimerHandle_t xTimer) {
  if (s_is_authenticated) {
    s_is_authenticated = false;
    printf(
        "\r\n[SDF CLI] Session timed out due to inactivity. Logged out.\r\n");
    // Print the prompt again
    printf("sdf> ");
    fflush(stdout);
  }
}

bool sdf_cli_is_authenticated(void) { return s_is_authenticated; }

void sdf_cli_authenticate(void) {
  s_is_authenticated = true;
  if (s_idle_timer) {
    xTimerReset(s_idle_timer, 0); // Reset timer
  }
}

void sdf_cli_logout(void) {
  s_is_authenticated = false;
  if (s_idle_timer) {
    xTimerStop(s_idle_timer, 0);
  }
}

/**
 * Hook into the esp_console read loop to reset the idle timer
 * whenever a user presses a key.
 * Currently, there's no direct hook for every keystroke in standard esp_console
 * without modifying linenoise, but we can reset the timer when a command
 * executes. A cleaner way for keystrokes is not strictly necessary if typing a
 * command takes < 2 mins. We will reset timer on any command execution.
 */
static void cli_reset_timer(void) {
  if (s_is_authenticated && s_idle_timer) {
    xTimerReset(s_idle_timer, pdMS_TO_TICKS(10));
  }
}

// Built-in commands
static struct {
  struct arg_str *password;
  struct arg_end *end;
} login_args;

static int cmd_login(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&login_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, login_args.end, argv[0]);
    return 0;
  }

  if (login_args.password->count > 0) {
    const char *pwd = login_args.password->sval[0];
    if (strcmp(pwd, CONFIG_SDF_CLI_PASSWORD) == 0) {
      sdf_cli_authenticate();
      printf("Login successful.\n");
    } else {
      printf("Invalid password.\n");
      // Delay to prevent brute force? Basic 1s delay
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  } else {
    printf("Password required.\n");
  }
  return 0;
}

static int cmd_logout(int argc, char **argv) {
  sdf_cli_logout();
  printf("Logged out.\n");
  return 0;
}

esp_err_t sdf_cli_init(void) {
#ifndef CONFIG_IDF_TARGET_LINUX
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

  // We want to route ESP_LOG via this console. Standard setup handles this
  // automatically if standard output is USJ. The prompt:
  repl_config.prompt = "sdf> ";
  repl_config.max_cmdline_length = 256;

  esp_console_dev_uart_config_t hw_config =
      ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  esp_err_t err = esp_console_new_repl_uart(&hw_config, &repl_config, &repl);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize esp_console_repl over UART");
    return err;
  }
#endif

  // Initialize argtable for login
  login_args.password = arg_str1(NULL, NULL, "<password>", "Admin password");
  login_args.end = arg_end(1);

  const esp_console_cmd_t login_cmd = {.command = "login",
                                       .help = "Log in to the CLI",
                                       .hint = NULL,
                                       .func = &cmd_login,
                                       .argtable = &login_args};
  esp_console_cmd_register(&login_cmd);

  const esp_console_cmd_t logout_cmd = {.command = "logout",
                                        .help = "Log out from the CLI",
                                        .hint = NULL,
                                        .func = &cmd_logout,
                                        .argtable = NULL};
  esp_console_cmd_register(&logout_cmd);

  // Register our specific subsystem commands
  sdf_cli_register_commands();

  // Create idle timer
  s_idle_timer = xTimerCreate("cli_idle", pdMS_TO_TICKS(CLI_IDLE_TIMEOUT_MS),
                              pdFALSE, (void *)0, cli_idle_timer_cb);
  if (s_idle_timer == NULL) {
    ESP_LOGE(TAG, "Failed to create CLI idle timer");
    return ESP_FAIL;
  }

#ifndef CONFIG_IDF_TARGET_LINUX
  // Start REPL task
  esp_err_t err = esp_console_start_repl(repl);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start standard REPL");
    return err;
  }
#endif

  ESP_LOGI(TAG, "CLI initialized via USB-Serial-JTAG");
  return ESP_OK;
}

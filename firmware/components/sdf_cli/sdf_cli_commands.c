#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"
#include "sdf_cli.h"
#include "sdf_services.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Forward declarations or includes to the actual subsystems would go here
// For now, we will just print mocks or call generic hooks.
// Assuming sdf_services.h or similar has what we need later or we decouple it.

static bool check_auth(void) {
  if (!sdf_cli_is_authenticated()) {
    printf("Authentication Required. Please use 'login <password>'\n");
    return false;
  }
  // Note: To properly reset the idle timer on valid command execution,
  // we should ideally call sdf_cli_authenticate() here again as a shortcut to
  // reset the timer.
  sdf_cli_authenticate();
  return true;
}

static bool parse_uint16_arg(const char *text, uint16_t min_value,
                             uint16_t max_value, uint16_t *out_value) {
  if (text == NULL || out_value == NULL) {
    return false;
  }

  errno = 0;
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed < min_value ||
      parsed > max_value) {
    return false;
  }

  *out_value = (uint16_t)parsed;
  return true;
}

static bool parse_uint8_arg(const char *text, uint8_t min_value,
                            uint8_t max_value, uint8_t *out_value) {
  uint16_t parsed = 0;
  if (!parse_uint16_arg(text, min_value, max_value, &parsed)) {
    return false;
  }

  *out_value = (uint8_t)parsed;
  return true;
}

// ==== USER COMMANDS ====
static int cmd_user(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 2) {
    printf("Usage: user <permission|add|get|update|del|list>\n");
    return 0;
  }
  const char *action = argv[1];

  if (strcmp(action, "permission") == 0 ||
      strcmp(action, "set-permission") == 0) {
    if (argc != 4) {
      printf("Usage: user permission <user_id> <permission>\n");
      return 0;
    }

    uint16_t user_id = 0;
    uint8_t permission = 0;
    if (!parse_uint16_arg(argv[2], SDF_FINGERPRINT_USER_ID_MIN,
                          SDF_FINGERPRINT_USER_ID_MAX, &user_id)) {
      printf("Invalid user_id. Expected %u-%u.\n",
             (unsigned)SDF_FINGERPRINT_USER_ID_MIN,
             (unsigned)SDF_FINGERPRINT_USER_ID_MAX);
      return 0;
    }
    if (!parse_uint8_arg(argv[3], 1u, 3u, &permission)) {
      printf("Invalid permission level. Expected 1, 2, or 3.\n");
      return 0;
    }

    printf("Scan an admin fingerprint to authorize user %" PRIu16
           " permission -> %u...\n",
           user_id, (unsigned)permission);
    esp_err_t err = sdf_services_change_user_permission(user_id, permission);
    if (err == ESP_OK) {
      printf("Permission updated for user %" PRIu16 " to level %u.\n", user_id,
             (unsigned)permission);
    } else if (err == ESP_ERR_NOT_FOUND) {
      printf("User %" PRIu16 " is not enrolled.\n", user_id);
    } else if (err == ESP_ERR_INVALID_STATE) {
      printf("Permission change rejected. The service may be busy, or this "
             "would remove the last admin fingerprint.\n");
    } else if (err == ESP_ERR_TIMEOUT) {
      printf("Timed out waiting for admin authorization or sensor response.\n");
    } else {
      printf("Failed to change permission for user %" PRIu16 ": %s\n", user_id,
             esp_err_to_name(err));
    }
  } else if (strcmp(action, "add") == 0) {
    printf("User add not fully implemented yet.\n");
  } else if (strcmp(action, "get") == 0) {
    printf("User get not fully implemented yet.\n");
  } else if (strcmp(action, "update") == 0) {
    printf("User update not fully implemented yet.\n");
  } else if (strcmp(action, "del") == 0) {
    printf("User del not fully implemented yet.\n");
  } else if (strcmp(action, "list") == 0) {
    printf("User list not fully implemented yet.\n");
  } else {
    printf("Unknown action: %s\n", action);
  }
  return 0;
}

// ==== NUKI COMMANDS ====
static int cmd_nuki(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 2) {
    printf("Usage: nuki <status|connect|pair|unpair>\n");
    return 0;
  }
  const char *action = argv[1];

  if (strcmp(action, "status") == 0) {
    printf("Nuki status not fully implemented yet.\n");
  } else if (strcmp(action, "connect") == 0) {
    printf("Nuki connect not fully implemented yet.\n");
  } else if (strcmp(action, "pair") == 0) {
    printf("Nuki pair not fully implemented yet.\n");
  } else if (strcmp(action, "unpair") == 0) {
    printf("Nuki unpair not fully implemented yet.\n");
  } else {
    printf("Unknown action: %s\n", action);
  }
  return 0;
}

// ==== ZIGBEE COMMANDS ====
static int cmd_zigbee(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 2) {
    printf("Usage: zigbee <status|connect|pair|unpair>\n");
    return 0;
  }
  const char *action = argv[1];

  if (strcmp(action, "status") == 0) {
    printf("Zigbee status not fully implemented yet.\n");
  } else if (strcmp(action, "connect") == 0) {
    printf("Zigbee connect not fully implemented yet.\n");
  } else if (strcmp(action, "pair") == 0) {
    printf("Zigbee pair not fully implemented yet.\n");
  } else if (strcmp(action, "unpair") == 0) {
    printf("Zigbee unpair not fully implemented yet.\n");
  } else {
    printf("Unknown action: %s\n", action);
  }
  return 0;
}

void sdf_cli_register_commands(void) {
  const esp_console_cmd_t user_cmd = {
      .command = "user",
      .help = "Manage users (permission, add, get, update, del, list)",
      .hint = "<action> [args...]",
      .func = &cmd_user,
  };
  esp_console_cmd_register(&user_cmd);

  const esp_console_cmd_t nuki_cmd = {
      .command = "nuki",
      .help = "Manage Nuki connection (status, connect, pair, unpair)",
      .hint = "<action>",
      .func = &cmd_nuki,
  };
  esp_console_cmd_register(&nuki_cmd);

  const esp_console_cmd_t zigbee_cmd = {
      .command = "zigbee",
      .help = "Manage Zigbee connection (status, connect, pair, unpair)",
      .hint = "<action>",
      .func = &cmd_zigbee,
  };
  esp_console_cmd_register(&zigbee_cmd);
}

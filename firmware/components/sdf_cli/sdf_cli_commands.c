#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"
#include "sdf_cli.h"
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

// ==== USER COMMANDS ====
static int cmd_user(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 2) {
    printf("Usage: user <add|get|update|del|list>\n");
    return 0;
  }
  const char *action = argv[1];

  if (strcmp(action, "add") == 0) {
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
      .help = "Manage users (add, get, update, del, list)",
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

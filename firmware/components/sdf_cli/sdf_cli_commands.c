#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "fingerprint.h"
#include "sdf_cli.h"
#include "sdf_protocol_zigbee.h"
#include "sdf_services.h"
#include "sdf_storage.h"
#ifndef CONFIG_IDF_TARGET_LINUX
/* OTA (esp_app_desc.h/esp_ota_ops.h/sdf_ota.h) needs app_update, which isn't
 * built for IDF_TARGET=linux; the "ota" CLI command itself is guarded out
 * below. */
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "sdf_ota.h"
#include "esp_zigbee_core.h"
#include "sdf_app.h"
#include "sdf_nuki_ble_transport.h"
#include "sdf_nuki_pairing.h"
#endif
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Forward declarations or includes to the actual subsystems would go here
// For now, we will just print mocks or call generic hooks.
// Assuming sdf_services.h or similar has what we need later or we decouple it.

/* A real compile-time constant (unlike a local "const size_t"), so sizing
 * user_ids[]/permissions[] with it below doesn't get flagged as a VLA. */
#define SDF_CLI_MAX_USERS ((size_t)SDF_FINGERPRINT_USER_ID_MAX + 1u)

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

static const char *permission_name(uint8_t perm) {
  switch (perm) {
  case 1:
    return "Standard";
  case 2:
    return "Elevated";
  case 3:
    return "Admin";
  default:
    return "Unknown";
  }
}

static int cmd_user_list(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  const size_t max_users = SDF_CLI_MAX_USERS;
  uint16_t user_ids[SDF_CLI_MAX_USERS];
  uint8_t permissions[SDF_CLI_MAX_USERS];
  size_t count = 0;
  esp_err_t err = sdf_services_query_users(user_ids, permissions, &count, max_users);
  if (err != ESP_OK) {
    printf("Failed to query users: %s\n", esp_err_to_name(err));
    return 0;
  }

  if (count == 0) {
    printf("No users enrolled.\n");
    return 0;
  }

  printf("User ID  Permission  Name\n");
  printf("-------  ----------  ----\n");
  for (size_t i = 0; i < count; i++) {
    char name[SDF_STORAGE_FP_USER_NAME_MAX];
    err = sdf_storage_load_user_name(user_ids[i], name, sizeof(name));
    if (err != ESP_OK) {
      name[0] = '\0';
    }
    printf("%-7u  %u (%s)  %s\n", (unsigned)user_ids[i], (unsigned)permissions[i],
           permission_name(permissions[i]), name);
  }
  return 0;
}

static int cmd_user_set_name(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc != 4) {
    printf("Usage: user set-name <user_id> <name>\n");
    return 0;
  }

  uint16_t user_id = 0;
  if (!parse_uint16_arg(argv[2], SDF_FINGERPRINT_USER_ID_MIN,
                        SDF_FINGERPRINT_USER_ID_MAX, &user_id)) {
    printf("Invalid user_id. Expected %u-%u.\n",
           (unsigned)SDF_FINGERPRINT_USER_ID_MIN,
           (unsigned)SDF_FINGERPRINT_USER_ID_MAX);
    return 0;
  }

  const char *name = argv[3];
  if (strlen(name) >= SDF_STORAGE_FP_USER_NAME_MAX) {
    printf("Name too long. Max %d characters.\n", SDF_STORAGE_FP_USER_NAME_MAX - 1);
    return 0;
  }

  // Check if user exists
  uint8_t permission = 0;
  sdf_fingerprint_op_result_t res = fp_query_user_permission(user_id, &permission);
  if (res != SDF_FINGERPRINT_OP_OK) {
    printf("User %u not enrolled.\n", (unsigned)user_id);
    return 0;
  }

  esp_err_t err = sdf_storage_save_user_name(user_id, name);
  if (err == ESP_OK) {
    printf("Name '%s' set for user %u.\n", name, (unsigned)user_id);
  } else {
    printf("Failed to save name: %s\n", esp_err_to_name(err));
  }
  return 0;
}

static int cmd_user_clear_name(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc != 3) {
    printf("Usage: user clear-name <user_id>\n");
    return 0;
  }

  uint16_t user_id = 0;
  if (!parse_uint16_arg(argv[2], SDF_FINGERPRINT_USER_ID_MIN,
                        SDF_FINGERPRINT_USER_ID_MAX, &user_id)) {
    printf("Invalid user_id. Expected %u-%u.\n",
           (unsigned)SDF_FINGERPRINT_USER_ID_MIN,
           (unsigned)SDF_FINGERPRINT_USER_ID_MAX);
    return 0;
  }

  esp_err_t err = sdf_storage_delete_user_name(user_id);
  if (err == ESP_OK) {
    printf("Name cleared for user %u.\n", (unsigned)user_id);
  } else {
    printf("Failed to clear name: %s\n", esp_err_to_name(err));
  }
  return 0;
}

static int cmd_user_get(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc != 3) {
    printf("Usage: user get <user_id>\n");
    return 0;
  }

  uint16_t user_id = 0;
  if (!parse_uint16_arg(argv[2], SDF_FINGERPRINT_USER_ID_MIN,
                        SDF_FINGERPRINT_USER_ID_MAX, &user_id)) {
    printf("Invalid user_id. Expected %u-%u.\n",
           (unsigned)SDF_FINGERPRINT_USER_ID_MIN,
           (unsigned)SDF_FINGERPRINT_USER_ID_MAX);
    return 0;
  }

  uint8_t permission = 0;
  sdf_fingerprint_op_result_t res = fp_query_user_permission(user_id, &permission);
  if (res == SDF_FINGERPRINT_OP_OK) {
    char name[SDF_STORAGE_FP_USER_NAME_MAX];
    esp_err_t err = sdf_storage_load_user_name(user_id, name, sizeof(name));
    if (err != ESP_OK) {
      name[0] = '\0';
    }
    printf("User ID: %u, Permission: %u (%s), Name: %s\n", (unsigned)user_id,
           (unsigned)permission, permission_name(permission), name);
  } else if (res == SDF_FINGERPRINT_OP_NO_MATCH) {
    printf("User %u not found.\n", (unsigned)user_id);
  } else {
    printf("Failed to query user %u: %d\n", (unsigned)user_id, (int)res);
  }
  return 0;
}

static int cmd_user_del(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc != 3) {
    printf("Usage: user del <user_id>\n");
    return 0;
  }

  uint16_t user_id = 0;
  if (!parse_uint16_arg(argv[2], SDF_FINGERPRINT_USER_ID_MIN,
                        SDF_FINGERPRINT_USER_ID_MAX, &user_id)) {
    printf("Invalid user_id. Expected %u-%u.\n",
           (unsigned)SDF_FINGERPRINT_USER_ID_MIN,
           (unsigned)SDF_FINGERPRINT_USER_ID_MAX);
    return 0;
  }

  esp_err_t err = sdf_services_delete_user(user_id);
  if (err == ESP_OK) {
    printf("User %u deleted.\n", (unsigned)user_id);
  } else if (err == ESP_ERR_NOT_FOUND) {
    printf("User %u not found.\n", (unsigned)user_id);
  } else if (err == ESP_ERR_INVALID_STATE) {
    printf("Cannot delete user %u: invalid state (may be last admin).\n",
           (unsigned)user_id);
  } else {
    printf("Failed to delete user %u: %s\n", (unsigned)user_id,
           esp_err_to_name(err));
  }
  return 0;
}

static int cmd_user_add(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc != 4) {
    printf("Usage: user add <user_id> <permission>\n");
    return 0;
  }

  uint16_t user_id = 0;
  if (!parse_uint16_arg(argv[2], SDF_FINGERPRINT_USER_ID_MIN,
                        SDF_FINGERPRINT_USER_ID_MAX, &user_id)) {
    printf("Invalid user_id. Expected %u-%u.\n",
           (unsigned)SDF_FINGERPRINT_USER_ID_MIN,
           (unsigned)SDF_FINGERPRINT_USER_ID_MAX);
    return 0;
  }

  uint8_t permission = 0;
  if (!parse_uint8_arg(argv[3], 1u, 3u, &permission)) {
    printf("Invalid permission level. Expected 1, 2, or 3.\n");
    return 0;
  }

  // Check if user_id is already occupied
  const size_t max_users = SDF_CLI_MAX_USERS;
  uint16_t user_ids[SDF_CLI_MAX_USERS];
  uint8_t permissions[SDF_CLI_MAX_USERS];
  size_t count = 0;
  esp_err_t err = sdf_services_query_users(user_ids, permissions, &count, max_users);
  if (err != ESP_OK) {
    printf("Failed to check existing users: %s\n", esp_err_to_name(err));
    return 0;
  }
  for (size_t i = 0; i < count; i++) {
    if (user_ids[i] == user_id) {
      printf("User ID %u already enrolled.\n", (unsigned)user_id);
      return 0;
    }
  }

  // Request enrollment
  printf("Scan an admin fingerprint to authorize enrollment of user %" PRIu16
         " with permission %u...\n",
         user_id, (unsigned)permission);
  err = sdf_services_request_enrollment(user_id, permission);
  if (err != ESP_OK) {
    if (err == ESP_ERR_INVALID_STATE) {
      printf("Enrollment request rejected: service busy or invalid state.\n");
    } else if (err == ESP_ERR_INVALID_ARG) {
      printf("Invalid user ID or permission.\n");
    } else {
      printf("Failed to request enrollment: %s\n", esp_err_to_name(err));
    }
    return 0;
  }

  // Run 3-step enrollment loop
  for (int step = 1; step <= 3; step++) {
    printf("Place finger on sensor (scan %d of 3)...\n", step);
    sdf_fingerprint_op_result_t step_result =
        fp_enroll_step(step, user_id, permission);

    if (step_result == SDF_FINGERPRINT_OP_OK) {
      printf("Scan %d OK.\n", step);
      if (step < 3) {
        printf("Remove finger and place again for next scan.\n");
      }
    } else if (step_result == SDF_FINGERPRINT_OP_TIMEOUT) {
      printf("Scan %d timed out.\n", step);
      printf("Enrollment failed. Remove finger and try again.\n");
      return 0;
    } else if (step_result == SDF_FINGERPRINT_OP_FAILED) {
      if (step < 3) {
        printf("Scan %d failed. Please try this scan again.\n", step);
        step--; // retry same step
      } else {
        printf("Scan 3 failed. Enrollment failed.\n");
        return 0;
      }
    } else if (step_result == SDF_FINGERPRINT_OP_USER_OCCUPIED) {
      printf("User ID %u is already enrolled.\n", (unsigned)user_id);
      return 0;
    } else if (step_result == SDF_FINGERPRINT_OP_FULL) {
      printf("Fingerprint database full (max 10 users).\n");
      return 0;
    } else {
      printf("Scan %d error: %d\n", step, (int)step_result);
      printf("Enrollment failed.\n");
      return 0;
    }
  }

  printf("User %u enrolled successfully with permission %u (%s).\n",
         (unsigned)user_id, (unsigned)permission, permission_name(permission));
  return 0;
}

static int cmd_user(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 2) {
    printf("Usage: user <permission|add|get|set-name|clear-name|del|list>\n");
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
    return cmd_user_add(argc, argv);
  } else if (strcmp(action, "get") == 0) {
    return cmd_user_get(argc, argv);
  } else if (strcmp(action, "set-name") == 0) {
    return cmd_user_set_name(argc, argv);
  } else if (strcmp(action, "clear-name") == 0) {
    return cmd_user_clear_name(argc, argv);
  } else if (strcmp(action, "update") == 0) {
    printf("User update not implemented. Use 'user permission <id> <perm>'.\n");
  } else if (strcmp(action, "del") == 0) {
    return cmd_user_del(argc, argv);
  } else if (strcmp(action, "list") == 0) {
    return cmd_user_list(argc, argv);
  } else {
    printf("Unknown action: %s\n", action);
  }
  return 0;
}

// ==== NUKI COMMANDS ====

#ifndef CONFIG_IDF_TARGET_LINUX

static int cmd_nuki_status(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  uint32_t auth_id = 0;
  uint8_t shared_key[32] = {0};
  esp_err_t err = sdf_storage_nuki_load(&auth_id, shared_key);
  bool paired = (err == ESP_OK);

  bool ble_ready = sdf_nuki_ble_is_ready(sdf_app_get_ble_transport());

  printf("Nuki Status:\n");
  printf("  Paired: %s\n", paired ? "yes" : "no");
  if (paired) {
    printf("  Authorization ID: 0x%08" PRIX32 "\n", auth_id);
  } else {
    printf("  Authorization ID: N/A\n");
  }
  printf("  BLE Transport: %s\n", ble_ready ? "ready" : "disconnected");

  // Try to get last known keyturner state from sdf_app if available
  // (This would need access to sdf_app internal state, for now we show unknown)
  printf("  Last Keyturner State: unknown\n");
  printf("  Signal RSSI: N/A\n");

  return 0;
}

static int cmd_nuki_connect(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  uint32_t auth_id = 0;
  uint8_t shared_key[32] = {0};
  esp_err_t err = sdf_storage_nuki_load(&auth_id, shared_key);
  if (err != ESP_OK) {
    printf("Not paired - run 'nuki pair' first\n");
    return 0;
  }

  if (sdf_nuki_ble_is_ready(sdf_app_get_ble_transport())) {
    printf("Already connected\n");
    return 0;
  }

  printf("Connecting to Nuki lock...\n");
  err = sdf_nuki_ble_set_enabled(sdf_app_get_ble_transport(), true);
  if (err != 0) {
    printf("Failed to enable BLE: %d\n", err);
    return 0;
  }

  err = sdf_nuki_ble_start(sdf_app_get_ble_transport());
  if (err != 0) {
    printf("Failed to start BLE: %d\n", err);
    return 0;
  }

  // Poll for connection with 10s timeout
  printf("Waiting for connection...\n");
  for (int i = 0; i < 100; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
if (sdf_nuki_ble_is_ready(sdf_app_get_ble_transport())) {
      printf("Connected successfully!\n");
      return 0;
    }
  }

  printf("Connection timeout (10s)\n");
  return 0;
}

static int cmd_nuki_pair(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  // Check if already paired
  uint32_t auth_id = 0;
  uint8_t shared_key[32] = {0};
  esp_err_t err = sdf_storage_nuki_load(&auth_id, shared_key);
  if (err == ESP_OK) {
    printf("WARNING: Already paired (Authorization ID: 0x%08" PRIX32 ").\n", auth_id);
    printf("Re-pairing will overwrite existing credentials.\n");
  }

  printf("Starting Nuki pairing...\n");
  printf("Ensure Nuki lock is in pairing mode (hold button 5s until LED solid).\n");

  // Enable BLE transport
  err = sdf_nuki_ble_set_enabled(sdf_app_get_ble_transport(), true);
  if (err != 0) {
    printf("Failed to enable BLE: %d\n", err);
    return 0;
  }

  err = sdf_nuki_ble_start(sdf_app_get_ble_transport());
  if (err != 0) {
    printf("Failed to start BLE: %d\n", err);
    return 0;
  }

  // Initialize pairing
  // Using fixed values from sdf_app.c
  #define SDF_APP_ID 123456
  #define SDF_APP_NAME "SmartDoorFinger"

  err = sdf_nuki_pairing_init(sdf_app_get_nuki_pairing(), sdf_app_get_nuki_client(), 1, SDF_APP_ID, SDF_APP_NAME);
  if (err != 0) {
    printf("Failed to initialize pairing: %d\n", err);
    return 0;
  }

  printf("Pairing initialized. Starting pairing process...\n");
  err = sdf_nuki_pairing_start(sdf_app_get_nuki_pairing());
  if (err != 0) {
    printf("Failed to start pairing: %d\n", err);
    return 0;
  }

  // Wait for pairing completion (poll state with 60s timeout)
  printf("Waiting for pairing to complete (60s timeout)...\n");
  for (int i = 0; i < 600; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (sdf_app_get_nuki_pairing()->state == SDF_NUKI_PAIRING_COMPLETE) {
      sdf_nuki_credentials_t creds;
      err = sdf_nuki_pairing_get_credentials(sdf_app_get_nuki_pairing(), &creds);
      if (err == 0) {
        err = sdf_storage_nuki_save(creds.authorization_id, creds.shared_key);
        if (err == ESP_OK) {
          printf("Pairing complete! Authorization ID: 0x%08" PRIX32 "\n",
                 creds.authorization_id);
        } else {
          printf("Pairing completed but failed to save credentials: %s\n",
                 esp_err_to_name(err));
        }
      } else {
        printf("Pairing completed but failed to get credentials: %d\n", err);
      }
      return 0;
    } else if (sdf_app_get_nuki_pairing()->state == SDF_NUKI_PAIRING_ERROR) {
      printf("Pairing failed (state: ERROR)\n");
      return 0;
    }
  }

  printf("Pairing timeout (60s)\n");
  return 0;
}

static int cmd_nuki_unpair(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  printf("Unpairing from Nuki lock...\n");

  // Stop BLE transport
  sdf_nuki_ble_stop(sdf_app_get_ble_transport());

  // Clear credentials
  esp_err_t err = sdf_storage_nuki_clear();
  if (err != ESP_OK) {
    printf("Warning: Failed to clear Nuki credentials: %s\n", esp_err_to_name(err));
  }

  err = sdf_storage_ble_target_clear();
  if (err != ESP_OK) {
    printf("Warning: Failed to clear BLE target: %s\n", esp_err_to_name(err));
  }

  // Reset pairing state
  memset(sdf_app_get_nuki_pairing(), 0, sizeof(sdf_nuki_pairing_t));

  printf("Nuki unpair complete. Device ready for new pairing.\n");
  return 0;
}

static int cmd_nuki(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 2) {
    printf("Usage: nuki <status|connect|pair|unpair>\n");
    return 0;
  }
  const char *action = argv[1];

  if (strcmp(action, "status") == 0) {
    return cmd_nuki_status(argc, argv);
  } else if (strcmp(action, "connect") == 0) {
    return cmd_nuki_connect(argc, argv);
  } else if (strcmp(action, "pair") == 0) {
    return cmd_nuki_pair(argc, argv);
  } else if (strcmp(action, "unpair") == 0) {
    return cmd_nuki_unpair(argc, argv);
  } else {
    printf("Unknown action: %s\n", action);
  }
  return 0;
}

#endif // CONFIG_IDF_TARGET_LINUX

// ==== ZIGBEE COMMANDS ====

static int cmd_zigbee_status(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  printf("Zigbee Status:\n");

  bool enabled = sdf_protocol_zigbee_is_enabled();
  printf("  Enabled: %s\n", enabled ? "yes" : "no");

  if (!enabled) {
    printf("  (Zigbee disabled in build config)\n");
    return 0;
  }

  bool ready = sdf_protocol_zigbee_is_ready();
  printf("  Stack Started: %s\n", ready ? "yes" : "no");

  if (!ready) {
    printf("  Network Joined: no\n");
    return 0;
  }

  // Try to get network info from ESP Zigbee stack
  // Note: These require ESP Zigbee stack APIs which may not be available on Linux builds
#ifndef CONFIG_IDF_TARGET_LINUX
  esp_zb_ieee_addr_t ieee_addr;
  esp_zb_get_long_address(ieee_addr);
  printf("  IEEE Address: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
         ieee_addr[7], ieee_addr[6], ieee_addr[5], ieee_addr[4],
         ieee_addr[3], ieee_addr[2], ieee_addr[1], ieee_addr[0]);

  uint16_t short_addr = esp_zb_get_short_address();
  printf("  Short Address: 0x%04X\n", short_addr);

  uint16_t pan_id = esp_zb_get_pan_id();
  printf("  PAN ID: 0x%04X\n", pan_id);

  uint8_t channel = esp_zb_get_current_channel();
  printf("  Channel: %u\n", (unsigned)channel);

  uint32_t checkin_interval = sdf_protocol_zigbee_get_checkin_interval_ms();
  printf("  Check-in Interval: %" PRIu32 " ms\n", checkin_interval);

  // Parent RSSI would require additional Zigbee APIs
  printf("  Parent RSSI: N/A\n");
#else
  printf("  (Network details not available on Linux build)\n");
#endif

  printf("  Network Joined: yes\n");
  return 0;
}

static int cmd_zigbee_connect(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  if (!sdf_protocol_zigbee_is_enabled()) {
    printf("Zigbee disabled in build config\n");
    return 0;
  }

  if (sdf_protocol_zigbee_is_ready()) {
    // Already joined, show current network
#ifndef CONFIG_IDF_TARGET_LINUX
    uint16_t pan_id = esp_zb_get_pan_id();
    printf("Already joined to network PAN 0x%04X\n", pan_id);
#else
    printf("Already joined to network\n");
#endif
    return 0;
  }

  printf("Starting network steering (permit join)...\n");
  esp_err_t err = sdf_protocol_zigbee_permit_join();
  if (err == ESP_OK) {
    printf("Network steering enabled (join window open)\n");
    printf("Check coordinator for join request.\n");
  } else {
    printf("Failed to start network steering: %s\n", esp_err_to_name(err));
  }
  return 0;
}

static int cmd_zigbee_unpair(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  if (!sdf_protocol_zigbee_is_enabled()) {
    printf("Zigbee disabled in build config\n");
    return 0;
  }

  if (!sdf_protocol_zigbee_is_ready()) {
    printf("Not joined to any network\n");
    return 0;
  }

  printf("Leaving Zigbee network and clearing NVRAM...\n");
  esp_err_t err = sdf_protocol_zigbee_factory_reset();
  if (err == ESP_OK) {
    printf("Zigbee network left and NVRAM cleared\n");
  } else {
    printf("Failed to leave network: %s\n", esp_err_to_name(err));
  }
  return 0;
}

static int cmd_zigbee(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 2) {
    printf("Usage: zigbee <status|connect|unpair>\n");
    return 0;
  }
  const char *action = argv[1];

  if (strcmp(action, "status") == 0) {
    return cmd_zigbee_status(argc, argv);
  } else if (strcmp(action, "connect") == 0) {
    return cmd_zigbee_connect(argc, argv);
  } else if (strcmp(action, "unpair") == 0) {
    return cmd_zigbee_unpair(argc, argv);
  } else {
    printf("Unknown action: %s\n", action);
  }
  return 0;
}

// ==== OTA COMMANDS ====

#ifndef CONFIG_IDF_TARGET_LINUX

static int cmd_ota_version(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  const char *version = sdf_ota_get_version();
  const esp_app_desc_t *app_desc = esp_app_get_description();
  
  printf("SDF Firmware Version: %s\n", version ? version : "unknown");
  printf("Project Name: %s\n", app_desc->project_name);
  printf("Build Time: %s %s\n", app_desc->date, app_desc->time);
  printf("IDF Version: %s\n", app_desc->idf_ver);
  printf("ELF SHA256: %.8s\n", app_desc->app_elf_sha256);
  return 0;
}

static int cmd_ota_status(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  sdf_ota_state_t state = sdf_ota_get_state();
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  
  printf("OTA Status:\n");
  printf("  State: %d\n", state);
  printf("  Running Partition: %s (0x%08" PRIX32 ", %" PRIu32 " bytes)\n",
         running->label, running->address, running->size);
  printf("  Next Update Partition: %s (0x%08" PRIX32 ", %" PRIu32 " bytes)\n",
         next->label, next->address, next->size);
  printf("  Current Version: %s\n", sdf_ota_get_version());

  esp_app_desc_t running_desc;
  esp_err_t err = esp_ota_get_partition_description(running, &running_desc);
  if (err == ESP_OK) {
    printf("  Running Version: %s\n", running_desc.version);
  }

  esp_app_desc_t next_desc;
  err = esp_ota_get_partition_description(next, &next_desc);
  if (err == ESP_OK) {
    printf("  Next Version: %s\n", next_desc.version);
  } else {
    printf("  Next Version: (empty or invalid)\n");
  }

  return 0;
}

static int cmd_ota_trigger(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 3) {
    printf("Usage: ota trigger <source>\n");
    printf("  zigbee://  - Trigger Zigbee OTA query\n");
    printf("  <url>      - Placeholder for HTTP/BLE source (not implemented)\n");
    return 0;
  }

  const char *source = argv[2];
  
  if (strcmp(source, "zigbee://") == 0) {
    printf("Triggering Zigbee OTA query...\n");
    esp_err_t err = sdf_protocol_zigbee_trigger_ota_query();
    if (err == ESP_OK) {
      printf("Query interval temporarily shortened; expect a query within "
             "~1 minute (restores to %d hour(s) automatically).\n",
             CONFIG_SDF_OTA_ZIGBEE_QUERY_INTERVAL_HOURS);
    } else if (err == ESP_ERR_INVALID_STATE) {
      printf("Zigbee not joined to a network. Use 'zigbee status' to check.\n");
    } else if (err == ESP_ERR_TIMEOUT) {
      printf("Timed out acquiring the Zigbee stack lock. Try again.\n");
    } else {
      printf("Failed to trigger OTA query: %s\n", esp_err_to_name(err));
    }
  } else {
    printf("Source '%s' not yet implemented\n", source);
  }
  return 0;
}

static int cmd_ota_rollback(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  printf("WARNING: This will rollback to the previous firmware and reboot!\n");
  printf("Are you sure? (y/N): ");
  
  char confirm[4];
  if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
    printf("Cancelled\n");
    return 0;
  }
  
  if (confirm[0] != 'y' && confirm[0] != 'Y') {
    printf("Cancelled\n");
    return 0;
  }

  printf("Rolling back...\n");
  esp_err_t err = sdf_ota_rollback();
  if (err != ESP_OK) {
    printf("Rollback failed: %s\n", esp_err_to_name(err));
  }
  // sdf_ota_rollback() reboots on success
  return 0;
}

static int cmd_ota_verify(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!check_auth())
    return 0;

  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  if (next == NULL) {
    printf("No OTA partition available\n");
    return 0;
  }

  printf("Verifying OTA partition: %s\n", next->label);
  
  // Check if partition has valid app
  esp_app_desc_t desc;
  esp_err_t err = esp_ota_get_partition_description(next, &desc);
  if (err != ESP_OK) {
    printf("No valid firmware in next partition: %s\n", esp_err_to_name(err));
    return 0;
  }
  
  printf("Found firmware: version=%s, project=%s\n", desc.version, desc.project_name);
  
  // Compare versions
  const char *current = sdf_ota_get_version();
  sdf_ota_version_cmp_t cmp = sdf_ota_version_compare(current, desc.version);
  
  if (cmp == SDF_OTA_VERSION_NEWER) {
    printf("Incoming is NEWER (upgrade)\n");
  } else if (cmp == SDF_OTA_VERSION_OLDER) {
    printf("Incoming is OLDER (downgrade)\n");
  } else {
    printf("Incoming is SAME version (reinstall)\n");
  }

#if CONFIG_SDF_OTA_SIGNATURE_VERIFY
  printf("Verifying signature...\n");
  /* This command verifies an out-of-band partition (e.g. flashed outside
   * a tracked sdf_ota_begin()/write() session), so there's no session-
   * recorded "actual bytes written" to fall back on the way
   * sdf_ota_verify_and_commit() does. Accept an optional explicit size
   * ("ota verify <image_size_bytes>"); without it, fall back to the full
   * partition size, which only works when the image was written to exactly
   * fill the partition - warn loudly since that's rarely true. */
  uint32_t image_size = next->size;
  if (argc >= 3) {
    image_size = (uint32_t)strtoul(argv[2], NULL, 0);
  } else {
    printf("Warning: no image size given, assuming image fills the whole "
           "partition (%" PRIu32 " bytes). Pass the real size as "
           "'ota verify <bytes>' for a reliable result.\n",
           (uint32_t)next->size);
  }
  /* No session accumulated a digest for this partition, so recompute it by
   * reading the committed image back. */
  uint8_t digest[SDF_OTA_DIGEST_SIZE];
  err = sdf_ota_compute_partition_digest(next, image_size, digest);
  if (err != ESP_OK) {
    printf("Signature verification: FAILED to digest image (%s)\n", esp_err_to_name(err));
    return 0;
  }
  err = sdf_ota_verify_signature(next, image_size, digest);
  if (err == ESP_OK) {
    printf("Signature verification: PASSED\n");
  } else {
    printf("Signature verification: FAILED (%s)\n", esp_err_to_name(err));
  }
#else
  printf("Signature verification: DISABLED (CONFIG_SDF_OTA_SIGNATURE_VERIFY=n)\n");
#endif

  return 0;
}

static int cmd_ota(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 2) {
    printf("Usage: ota <version|status|trigger|rollback|verify>\n");
    return 0;
  }
  const char *action = argv[1];

  if (strcmp(action, "version") == 0) {
    return cmd_ota_version(argc, argv);
  } else if (strcmp(action, "status") == 0) {
    return cmd_ota_status(argc, argv);
  } else if (strcmp(action, "trigger") == 0) {
    return cmd_ota_trigger(argc, argv);
  } else if (strcmp(action, "rollback") == 0) {
    return cmd_ota_rollback(argc, argv);
  } else if (strcmp(action, "verify") == 0) {
    return cmd_ota_verify(argc, argv);
  } else {
    printf("Unknown action: %s\n", action);
    printf("Usage: ota <version|status|trigger|rollback|verify>\n");
  }
  return 0;
}

#endif // CONFIG_IDF_TARGET_LINUX
static int cmd_factory_reset(int argc, char **argv) {
  if (!check_auth())
    return 0;

  if (argc < 2 || strcmp(argv[1], "YES") != 0) {
    printf("WARNING: Factory reset will erase all NVS data, fingerprint users, Zigbee pairing, and reboot.\n");
    printf("To confirm factory reset, run: factory_reset YES\n");
    return 0;
  }

  printf("Executing factory reset...\n");
  printf("Step 1/5: Erasing NVS storage...\n");
  esp_err_t err = sdf_storage_erase_all();
  if (err != ESP_OK) {
    printf("Warning: NVS erase failed: %s\n", esp_err_to_name(err));
  }

  printf("Step 2/5: Deleting fingerprint templates...\n");
  sdf_fingerprint_op_result_t fp_res = fp_delete_all_users();
  if (fp_res != SDF_FINGERPRINT_OP_OK) {
    printf("Warning: Fingerprint clear failed: %d\n", (int)fp_res);
  }

  printf("Step 3/5: Resetting Zigbee stack...\n");
  err = sdf_protocol_zigbee_factory_reset();
  if (err != ESP_OK) {
    printf("Warning: Zigbee reset failed: %s\n", esp_err_to_name(err));
  }

  printf("Step 4/5: Resetting services state...\n");
  err = sdf_services_reset_state();
  if (err != ESP_OK) {
    printf("Warning: Services state reset failed: %s\n", esp_err_to_name(err));
  }

  printf("Step 5/5: Rebooting device...\n");
  esp_restart();
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

#ifndef CONFIG_IDF_TARGET_LINUX
  const esp_console_cmd_t nuki_cmd = {
      .command = "nuki",
      .help = "Manage Nuki connection (status, connect, pair, unpair)",
      .hint = "<action>",
      .func = &cmd_nuki,
  };
  esp_console_cmd_register(&nuki_cmd);
#endif

  const esp_console_cmd_t zigbee_cmd = {
      .command = "zigbee",
      .help = "Manage Zigbee connection (status, connect, pair, unpair)",
      .hint = "<action>",
      .func = &cmd_zigbee,
  };
  esp_console_cmd_register(&zigbee_cmd);

#ifndef CONFIG_IDF_TARGET_LINUX
  const esp_console_cmd_t ota_cmd = {
      .command = "ota",
      .help = "Manage OTA updates (version, status, trigger, rollback, verify)",
      .hint = "<action>",
      .func = &cmd_ota,
  };
  esp_console_cmd_register(&ota_cmd);
#endif

  const esp_console_cmd_t factory_reset_cmd = {
      .command = "factory_reset",
      .help = "Perform complete factory reset (erases NVS, users, Zigbee pairing, reboots)",
      .hint = "YES",
      .func = &cmd_factory_reset,
  };
  esp_console_cmd_register(&factory_reset_cmd);
}


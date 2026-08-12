/* Pure decision functions for the BLE web companion's LOGIN/REGISTER flow.
 * No I/O, no locks, no BLE/storage calls - callers (sdf_ble_companion,
 * sdf_app) gather inputs, call into here, then perform I/O based on the
 * result. See openspec/changes/add-linux-target-sdf-app-support/design.md.
 */
#include "sdf_services.h"

#include <string.h>

#include "mbedtls/constant_time.h"

bool sdf_services_web_auth_verify_login(const sdf_storage_web_user_t *user,
                                         const uint8_t *submitted_hash,
                                         size_t hash_len) {
  if (!user || !submitted_hash || hash_len != SDF_STORAGE_WEB_USER_HASH_LEN) {
    return false;
  }

  return mbedtls_ct_memcmp(user->password_hash, submitted_hash,
                            SDF_STORAGE_WEB_USER_HASH_LEN) == 0;
}

sdf_services_web_auth_registration_decision_t sdf_services_web_auth_decide_registration(
    const char *username, const uint8_t *password_hash, size_t hash_len,
    uint8_t permission, bool admin_authorized) {
  sdf_services_web_auth_registration_decision_t decision = {0};
  decision.reply_authorized = admin_authorized;

  if (!admin_authorized || !username || !password_hash ||
      hash_len != SDF_STORAGE_WEB_USER_HASH_LEN) {
    return decision;
  }

  decision.should_persist = true;
  strncpy(decision.user.username, username, SDF_STORAGE_WEB_USER_NAME_MAX - 1);
  decision.user.username[SDF_STORAGE_WEB_USER_NAME_MAX - 1] = '\0';
  decision.user.permission = permission;
  decision.user.valid = true;
  memcpy(decision.user.password_hash, password_hash, SDF_STORAGE_WEB_USER_HASH_LEN);

  return decision;
}

bool sdf_services_web_auth_should_resolve_on_action_complete(
    sdf_services_admin_action_t action, esp_err_t result) {
  return action == SDF_SERVICES_ADMIN_ACTION_WEB_REG_AUTH && result != ESP_OK;
}

/* Same "always resolve the pending BLE client" guarantee as above, shared by
 * every BLE-triggered admin action that routes its result back to an
 * originating GATT connection: Nuki re-pair (nuki-pairing-setup-flow), and
 * Enroll-Admin / Zigbee Join (button-admin-actions-to-companion-app). Kept
 * as its own explicit, tested guard - rather than folded into the
 * WEB_REG_AUTH one above, which resolves via a different reply path
 * (sdf_ble_companion_reply_auth(), keyed by username, not conn_handle) - so
 * a future BLE-triggered action can't silently break either guarantee. */
bool sdf_services_ble_admin_action_should_resolve_on_action_complete(
    sdf_services_admin_action_t action, esp_err_t result) {
  return (action == SDF_SERVICES_ADMIN_ACTION_NUKI_REPAIR ||
          action == SDF_SERVICES_ADMIN_ACTION_ENROLL_ADMIN ||
          action == SDF_SERVICES_ADMIN_ACTION_ZB_JOIN) &&
         result != ESP_OK;
}

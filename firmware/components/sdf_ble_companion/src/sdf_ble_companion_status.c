#include "sdf_ble_companion_status.h"

bool sdf_ble_companion_status_admits(sdf_ble_companion_auth_state_t auth_state,
                                     uint16_t bound_user_id,
                                     bool bound_user_enrolled) {
    return auth_state == SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED &&
           bound_user_id != 0 && bound_user_enrolled;
}

bool sdf_ble_companion_status_fits_notification(size_t report_len,
                                                uint16_t att_mtu) {
    if (att_mtu < 3 + 1) {
        /* No room for even the ATT header plus one payload byte. */
        return false;
    }
    return report_len <= (size_t)att_mtu - 3u;
}

#include "unity.h"

#include "sdf_ble_companion_status.h"

/* --- 4.2: authenticated, any permission level; never admin-gated -------- */

void test_status_unauthenticated_read_refused(void) {
    TEST_ASSERT_FALSE(sdf_ble_companion_status_admits(
        SDF_BLE_COMPANION_AUTH_STATE_UNAUTHENTICATED, 0, false));
    TEST_ASSERT_FALSE(sdf_ble_companion_status_admits(
        SDF_BLE_COMPANION_AUTH_STATE_LOGIN_CHALLENGE_ISSUED, 3, true));
    TEST_ASSERT_FALSE(sdf_ble_companion_status_admits(
        SDF_BLE_COMPANION_AUTH_STATE_PENDING, 3, true));
}

void test_status_standard_user_read_permitted(void) {
    /* Authenticated bound user, still enrolled - no admin flag involved:
     * the rule is deliberately blind to permission level. */
    TEST_ASSERT_TRUE(sdf_ble_companion_status_admits(
        SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED, 5, true));
}

void test_status_admin_read_admitted_by_the_same_rule(void) {
    /* The same inputs admit an admin's connection - the report a standard
     * user receives is the report an admin receives. */
    TEST_ASSERT_TRUE(sdf_ble_companion_status_admits(
        SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED, 5, true));
}

void test_status_deleted_bound_user_loses_access(void) {
    /* Live enrolment resolution reports the bound user as gone. */
    TEST_ASSERT_FALSE(sdf_ble_companion_status_admits(
        SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED, 7, false));
}

void test_status_unbound_authenticated_connection_refused(void) {
    /* bound_user_id == 0 means no account to lose access with - refuse. */
    TEST_ASSERT_FALSE(sdf_ble_companion_status_admits(
        SDF_BLE_COMPANION_AUTH_STATE_AUTHENTICATED, 0, false));
}

/* --- 4.5: oversized report indicated, not truncated --------------------- */

void test_report_fitting_notification_is_sent_whole(void) {
    /* 3 bytes of ATT notify overhead on top of the payload. */
    TEST_ASSERT_TRUE(sdf_ble_companion_status_fits_notification(10u, 100u));
    TEST_ASSERT_TRUE(sdf_ble_companion_status_fits_notification(97u, 100u));
    /* Boundary: exactly MTU-3 fits. */
    TEST_ASSERT_TRUE(sdf_ble_companion_status_fits_notification(247u, 250u));
}

void test_oversized_report_gets_change_marker_not_truncation(void) {
    TEST_ASSERT_FALSE(sdf_ble_companion_status_fits_notification(248u, 250u));
    TEST_ASSERT_FALSE(sdf_ble_companion_status_fits_notification(500u, 250u));
    /* Degenerate MTU: nothing fits, so every report is a marker. */
    TEST_ASSERT_FALSE(sdf_ble_companion_status_fits_notification(1u, 3u));
}

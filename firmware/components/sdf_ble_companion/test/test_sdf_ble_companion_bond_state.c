#include "unity.h"

#include <string.h>

#include "sdf_ble_companion_bond_state.h"

static sdf_ble_companion_addr_t make_addr(uint8_t last_byte) {
    sdf_ble_companion_addr_t addr = {0};
    addr.type = 0; /* BLE_ADDR_PUBLIC */
    addr.val[5] = last_byte;
    return addr;
}

/* Failed-login counter state machine (7.1) */

void test_bond_login_failure_increments_counter(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t addr = make_addr(1);

    TEST_ASSERT_EQUAL(1, sdf_ble_companion_bond_note_login_failure(&state, &addr));
    TEST_ASSERT_EQUAL(2, sdf_ble_companion_bond_note_login_failure(&state, &addr));
}

void test_bond_login_success_resets_counter(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t addr = make_addr(1);

    sdf_ble_companion_bond_note_login_failure(&state, &addr);
    sdf_ble_companion_bond_note_login_failure(&state, &addr);
    sdf_ble_companion_bond_note_login_success(&state, &addr);

    TEST_ASSERT_EQUAL(1, sdf_ble_companion_bond_note_login_failure(&state, &addr));
}

void test_bond_evicts_at_threshold(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t addr = make_addr(1);
    sdf_ble_companion_bond_allow_list_add(&state, &addr);

    uint8_t count = 0;
    for (uint8_t i = 0; i < SDF_BLE_COMPANION_FAILED_LOGIN_THRESHOLD - 1; i++) {
        count = sdf_ble_companion_bond_note_login_failure(&state, &addr);
        TEST_ASSERT_FALSE(sdf_ble_companion_bond_should_evict(count));
    }

    count = sdf_ble_companion_bond_note_login_failure(&state, &addr);
    TEST_ASSERT_TRUE(sdf_ble_companion_bond_should_evict(count));

    /* Eviction is the caller's job (bond store + connection termination);
     * this module just signals it. Applying the removal here mirrors what
     * the caller does next. */
    TEST_ASSERT_TRUE(sdf_ble_companion_bond_allow_list_remove(&state, &addr));
    TEST_ASSERT_FALSE(sdf_ble_companion_bond_is_allow_listed(&state, &addr));
}

void test_bond_counter_retained_across_simulated_reconnect(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t addr = make_addr(1);
    sdf_ble_companion_bond_allow_list_add(&state, &addr);

    sdf_ble_companion_bond_note_login_failure(&state, &addr);
    sdf_ble_companion_bond_note_login_failure(&state, &addr);

    /* A disconnect/reconnect doesn't touch this module's state at all
     * (it's keyed by resolved identity, not conn_handle) - simulate one by
     * simply doing nothing and re-checking the counter via another
     * failure. */
    TEST_ASSERT_EQUAL(3, sdf_ble_companion_bond_note_login_failure(&state, &addr));
}

void test_bond_counter_cleared_on_reinit(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t addr = make_addr(1);

    sdf_ble_companion_bond_note_login_failure(&state, &addr);
    sdf_ble_companion_bond_note_login_failure(&state, &addr);

    /* Simulated reinit/reboot */
    sdf_ble_companion_bond_state_init(&state);

    TEST_ASSERT_EQUAL(1, sdf_ble_companion_bond_note_login_failure(&state, &addr));
}

/* Pairing-window admission (7.2) */

void test_window_first_bond_closes_window_and_allow_lists(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t addr = make_addr(1);

    sdf_ble_companion_bond_open_window(&state);
    TEST_ASSERT_TRUE(sdf_ble_companion_bond_window_is_open(&state));

    TEST_ASSERT_TRUE(sdf_ble_companion_bond_admit_if_window_open(&state, &addr));

    TEST_ASSERT_FALSE(sdf_ble_companion_bond_window_is_open(&state));
    TEST_ASSERT_TRUE(sdf_ble_companion_bond_is_allow_listed(&state, &addr));
}

void test_window_incomplete_connection_leaves_window_open(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t addr = make_addr(1);

    sdf_ble_companion_bond_open_window(&state);

    /* An incomplete connection never calls admit_if_window_open() at all
     * (bonding didn't complete) - nothing to do here except confirm the
     * window state is untouched by anything else that might have run. */
    TEST_ASSERT_TRUE(sdf_ble_companion_bond_window_is_open(&state));
    TEST_ASSERT_FALSE(sdf_ble_companion_bond_is_allow_listed(&state, &addr));
}

void test_window_timeout_closes_with_no_bond(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);

    sdf_ble_companion_bond_open_window(&state);
    sdf_ble_companion_bond_close_window(&state); /* simulated timeout */

    TEST_ASSERT_FALSE(sdf_ble_companion_bond_window_is_open(&state));
}

void test_window_admit_when_not_open_is_a_noop(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t addr = make_addr(1);

    TEST_ASSERT_FALSE(sdf_ble_companion_bond_admit_if_window_open(&state, &addr));
    TEST_ASSERT_FALSE(sdf_ble_companion_bond_is_allow_listed(&state, &addr));
}

/* Allow-list filtering proxy (7.3): the real filtered-advertising behavior
 * lives in NimBLE (untestable on the linux host target), but it's entirely
 * driven by this module's allow-list membership, so this exercises that
 * membership directly. */

void test_allow_listed_device_is_allow_listed(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t allowed = make_addr(1);
    sdf_ble_companion_addr_t stranger = make_addr(2);

    sdf_ble_companion_bond_allow_list_add(&state, &allowed);

    TEST_ASSERT_TRUE(sdf_ble_companion_bond_is_allow_listed(&state, &allowed));
    TEST_ASSERT_FALSE(sdf_ble_companion_bond_is_allow_listed(&state, &stranger));
}

void test_allow_list_snapshot_matches_membership(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t a = make_addr(1);
    sdf_ble_companion_addr_t b = make_addr(2);

    sdf_ble_companion_bond_allow_list_add(&state, &a);
    sdf_ble_companion_bond_allow_list_add(&state, &b);

    sdf_ble_companion_addr_t snapshot[SDF_BLE_COMPANION_BOND_TABLE_MAX];
    size_t count = sdf_ble_companion_bond_snapshot_allow_list(&state, snapshot,
                                                                SDF_BLE_COMPANION_BOND_TABLE_MAX);
    TEST_ASSERT_EQUAL(2, count);
}

void test_addr_eq_compares_type_and_value(void) {
    sdf_ble_companion_addr_t a = make_addr(1);
    sdf_ble_companion_addr_t b = make_addr(1);
    sdf_ble_companion_addr_t c = make_addr(2);

    TEST_ASSERT_TRUE(sdf_ble_companion_addr_eq(&a, &b));
    TEST_ASSERT_FALSE(sdf_ble_companion_addr_eq(&a, &c));
}

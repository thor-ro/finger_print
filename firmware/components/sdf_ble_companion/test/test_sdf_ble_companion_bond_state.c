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

/* ---------------------------------------------------------------------------
 * Setup-phase decisions (app-guided-first-time-setup): advertising-mode
 * selection, the single-connection cap, and intersection seeding.
 * ------------------------------------------------------------------------- */

void test_adv_mode_selection_covers_all_latch_armed_combinations(void) {
    /* Pairing window wins over everything. */
    TEST_ASSERT_EQUAL(SDF_BLE_COMPANION_ADV_MODE_PAIRING_WINDOW,
                      sdf_ble_companion_select_advertising_mode(true, false, true));
    TEST_ASSERT_EQUAL(SDF_BLE_COMPANION_ADV_MODE_PAIRING_WINDOW,
                      sdf_ble_companion_select_advertising_mode(true, true, false));

    /* Latch unset: armed -> unfiltered setup; disarmed -> silence. */
    TEST_ASSERT_EQUAL(SDF_BLE_COMPANION_ADV_MODE_UNFILTERED_SETUP,
                      sdf_ble_companion_select_advertising_mode(false, false, true));
    TEST_ASSERT_EQUAL(SDF_BLE_COMPANION_ADV_MODE_NOT_ADVERTISING,
                      sdf_ble_companion_select_advertising_mode(false, false, false));

    /* Latch set: sparse filtered default regardless of armed state. */
    TEST_ASSERT_EQUAL(SDF_BLE_COMPANION_ADV_MODE_SPARSE_FILTERED,
                      sdf_ble_companion_select_advertising_mode(false, true, true));
    TEST_ASSERT_EQUAL(SDF_BLE_COMPANION_ADV_MODE_SPARSE_FILTERED,
                      sdf_ble_companion_select_advertising_mode(false, true, false));
}

void test_second_connection_terminated_only_while_latch_unset(void) {
    /* Latch unset: any second inbound connection is terminated... */
    TEST_ASSERT_TRUE(sdf_ble_companion_should_terminate_second_connection(false, 1));
    TEST_ASSERT_TRUE(sdf_ble_companion_should_terminate_second_connection(false, 2));
    /* ...but a first connection never is. */
    TEST_ASSERT_FALSE(sdf_ble_companion_should_terminate_second_connection(false, 0));

    /* Latch set: ordinary limit governs - the cap never fires (the slot
     * accounting in BLE_GAP_EVENT_CONNECT still bounds at MAX_CONNECTIONS). */
    TEST_ASSERT_FALSE(sdf_ble_companion_should_terminate_second_connection(true, 1));
    TEST_ASSERT_FALSE(sdf_ble_companion_should_terminate_second_connection(true, 3));
}

void test_seed_intersection_abandoned_setup_bond_is_not_seeded(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t squatter = make_addr(9);

    sdf_ble_companion_addr_t bonded[] = {squatter};
    size_t seeded = sdf_ble_companion_allow_list_seed_intersection(
        &state, bonded, 1, NULL, 0);

    /* A bond made during an abandoned setup phase has no admission record,
     * so it grants nothing across a reboot. */
    TEST_ASSERT_EQUAL(0, seeded);
    TEST_ASSERT_FALSE(sdf_ble_companion_bond_is_allow_listed(&state, &squatter));
}

void test_seed_intersection_admitted_and_bonded_peer_is_seeded(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t owner = make_addr(4);
    sdf_ble_companion_addr_t other_owner = make_addr(5);

    sdf_ble_companion_addr_t bonded[] = {owner};
    sdf_ble_companion_addr_t admitted[] = {owner, other_owner};

    size_t seeded = sdf_ble_companion_allow_list_seed_intersection(
        &state, bonded, 1, admitted, 2);
    TEST_ASSERT_EQUAL(1, seeded);
    TEST_ASSERT_TRUE(sdf_ble_companion_bond_is_allow_listed(&state, &owner));
}

void test_seed_intersection_admission_without_bond_grants_nothing(void) {
    sdf_ble_companion_bond_state_t state;
    sdf_ble_companion_bond_state_init(&state);
    sdf_ble_companion_addr_t ghost = make_addr(6);

    sdf_ble_companion_addr_t bonded[SDF_BLE_COMPANION_BOND_TABLE_MAX] = {0};
    sdf_ble_companion_addr_t admitted[] = {ghost};

    /* Admission record exists but NimBLE forgot the keys - no resurrection. */
    size_t seeded = sdf_ble_companion_allow_list_seed_intersection(
        &state, bonded, 0, admitted, 1);
    TEST_ASSERT_EQUAL(0, seeded);
    TEST_ASSERT_FALSE(sdf_ble_companion_bond_is_allow_listed(&state, &ghost));
}

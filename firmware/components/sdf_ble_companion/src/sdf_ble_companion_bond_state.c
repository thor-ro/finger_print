#include "sdf_ble_companion_bond_state.h"

#include <string.h>

void sdf_ble_companion_bond_state_init(sdf_ble_companion_bond_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

bool sdf_ble_companion_addr_eq(const sdf_ble_companion_addr_t *a,
                                const sdf_ble_companion_addr_t *b) {
    if (!a || !b) return false;
    return a->type == b->type && memcmp(a->val, b->val, sizeof(a->val)) == 0;
}

static sdf_ble_companion_bond_entry_t *bond_find(sdf_ble_companion_bond_state_t *state,
                                                   const sdf_ble_companion_addr_t *addr) {
    for (int i = 0; i < SDF_BLE_COMPANION_BOND_TABLE_MAX; i++) {
        if (state->entries[i].in_use &&
            sdf_ble_companion_addr_eq(&state->entries[i].identity, addr)) {
            return &state->entries[i];
        }
    }
    return NULL;
}

static sdf_ble_companion_bond_entry_t *bond_find_free(sdf_ble_companion_bond_state_t *state) {
    for (int i = 0; i < SDF_BLE_COMPANION_BOND_TABLE_MAX; i++) {
        if (!state->entries[i].in_use) {
            return &state->entries[i];
        }
    }
    return NULL;
}

/* Finds addr's existing entry, or claims a free slot for it with a fresh
 * (zero-count, not-allow-listed) entry. Returns NULL if neither exists. */
static sdf_ble_companion_bond_entry_t *bond_find_or_create(sdf_ble_companion_bond_state_t *state,
                                                             const sdf_ble_companion_addr_t *addr) {
    sdf_ble_companion_bond_entry_t *entry = bond_find(state, addr);
    if (entry) return entry;

    entry = bond_find_free(state);
    if (!entry) return NULL;

    memset(entry, 0, sizeof(*entry));
    entry->in_use = true;
    entry->identity = *addr;
    return entry;
}

bool sdf_ble_companion_bond_is_allow_listed(const sdf_ble_companion_bond_state_t *state,
                                             const sdf_ble_companion_addr_t *addr) {
    sdf_ble_companion_bond_entry_t *entry =
        bond_find((sdf_ble_companion_bond_state_t *)state, addr);
    return entry != NULL && entry->allow_listed;
}

bool sdf_ble_companion_bond_allow_list_add(sdf_ble_companion_bond_state_t *state,
                                            const sdf_ble_companion_addr_t *addr) {
    if (!state || !addr) return false;
    sdf_ble_companion_bond_entry_t *entry = bond_find_or_create(state, addr);
    if (!entry) return false;
    entry->allow_listed = true;
    return true;
}

bool sdf_ble_companion_bond_allow_list_remove(sdf_ble_companion_bond_state_t *state,
                                               const sdf_ble_companion_addr_t *addr) {
    if (!state || !addr) return false;
    sdf_ble_companion_bond_entry_t *entry = bond_find(state, addr);
    if (!entry) return false;
    memset(entry, 0, sizeof(*entry));
    return true;
}

uint8_t sdf_ble_companion_bond_note_login_failure(sdf_ble_companion_bond_state_t *state,
                                                   const sdf_ble_companion_addr_t *addr) {
    if (!state || !addr) return 0;
    sdf_ble_companion_bond_entry_t *entry = bond_find_or_create(state, addr);
    if (!entry) return 0;
    if (entry->failed_login_count < UINT8_MAX) {
        entry->failed_login_count++;
    }
    return entry->failed_login_count;
}

void sdf_ble_companion_bond_note_login_success(sdf_ble_companion_bond_state_t *state,
                                                const sdf_ble_companion_addr_t *addr) {
    if (!state || !addr) return;
    sdf_ble_companion_bond_entry_t *entry = bond_find(state, addr);
    if (!entry) return;
    entry->failed_login_count = 0;
}

void sdf_ble_companion_bond_open_window(sdf_ble_companion_bond_state_t *state) {
    if (!state) return;
    state->pairing_window_open = true;
}

void sdf_ble_companion_bond_close_window(sdf_ble_companion_bond_state_t *state) {
    if (!state) return;
    state->pairing_window_open = false;
}

bool sdf_ble_companion_bond_window_is_open(const sdf_ble_companion_bond_state_t *state) {
    return state != NULL && state->pairing_window_open;
}

bool sdf_ble_companion_bond_admit_if_window_open(sdf_ble_companion_bond_state_t *state,
                                                  const sdf_ble_companion_addr_t *addr) {
    if (!state || !addr || !state->pairing_window_open) return false;

    sdf_ble_companion_bond_entry_t *entry = bond_find_or_create(state, addr);
    if (entry) {
        entry->allow_listed = true;
    }
    state->pairing_window_open = false;
    return true;
}

size_t sdf_ble_companion_bond_snapshot_allow_list(const sdf_ble_companion_bond_state_t *state,
                                                    sdf_ble_companion_addr_t *out_addrs,
                                                    size_t max_count) {
    if (!state || !out_addrs) return 0;
    size_t count = 0;
    for (int i = 0; i < SDF_BLE_COMPANION_BOND_TABLE_MAX && count < max_count; i++) {
        if (state->entries[i].in_use && state->entries[i].allow_listed) {
            out_addrs[count++] = state->entries[i].identity;
        }
    }
    return count;
}

sdf_ble_companion_adv_mode_t sdf_ble_companion_select_advertising_mode(
    bool pairing_window_open, bool setup_complete, bool setup_phase_armed) {
    if (pairing_window_open) {
        return SDF_BLE_COMPANION_ADV_MODE_PAIRING_WINDOW;
    }
    if (!setup_complete) {
        return setup_phase_armed ? SDF_BLE_COMPANION_ADV_MODE_UNFILTERED_SETUP
                                 : SDF_BLE_COMPANION_ADV_MODE_NOT_ADVERTISING;
    }
    return SDF_BLE_COMPANION_ADV_MODE_SPARSE_FILTERED;
}

bool sdf_ble_companion_should_terminate_second_connection(
    bool setup_complete, size_t connected_others) {
    return !setup_complete && connected_others > 0;
}

size_t sdf_ble_companion_allow_list_seed_intersection(
    sdf_ble_companion_bond_state_t *state,
    const sdf_ble_companion_addr_t *bonded, size_t num_bonded,
    const sdf_ble_companion_addr_t *admitted, size_t num_admitted) {
    if (!state || !bonded || !admitted) return 0;
    size_t added = 0;
    for (size_t i = 0; i < num_bonded; i++) {
        bool is_admitted = false;
        for (size_t a = 0; a < num_admitted; a++) {
            if (sdf_ble_companion_addr_eq(&bonded[i], &admitted[a])) {
                is_admitted = true;
                break;
            }
        }
        if (!is_admitted) {
            continue;
        }
        if (sdf_ble_companion_bond_allow_list_add(state, &bonded[i])) {
            added++;
        }
    }
    return added;
}

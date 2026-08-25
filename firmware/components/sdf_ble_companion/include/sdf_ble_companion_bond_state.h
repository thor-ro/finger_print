#ifndef SDF_BLE_COMPANION_BOND_STATE_H
#define SDF_BLE_COMPANION_BOND_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pure, hardware-independent allow-list/bond-tracking and pairing-window
 * admission logic for the BLE Companion Service's trust model. Kept free of
 * NimBLE calls (ble_gap_wl_set, ble_store_util_delete_peer, ...) so it can
 * be exercised by host (linux target) unit tests without the BLE stack -
 * mirrors the sdf_ble_ota_protocol.c pattern. See
 * openspec/changes/ble-companion-trust-and-lockout/design.md for the full
 * rationale. sdf_ble_companion.c owns the real NimBLE side effects and
 * calls into this module for the state transitions. */

/* Pairing-window duration and failed-login threshold are compile-time
 * constants, not runtime-configurable (sdf_config_set_*) values: these are
 * structural safety parameters for a BLE-reachable subsystem, so nothing an
 * attacker can write over the air can loosen its own lockout threshold or
 * widen its own pairing window (see design.md "Decisions"). */
#define SDF_BLE_COMPANION_PAIRING_WINDOW_MS 60000u
#define SDF_BLE_COMPANION_FAILED_LOGIN_THRESHOLD 3u

/* Every allow-listed identity is also a bonded identity (see design.md,
 * "Device identity = resolved BLE identity address"), and NimBLE only
 * persists up to CONFIG_BT_NIMBLE_MAX_BONDS (3) bond records - so this
 * table, and the allow list it drives, can never hold more than 3 entries
 * in practice. CONFIG_BT_NIMBLE_WHITELIST_SIZE=12 (the Filter Accept List's
 * controller-side capacity) therefore has 4x headroom over the actual
 * maximum this table will ever populate it with. */
#define SDF_BLE_COMPANION_BOND_TABLE_MAX 3

/* Mirrors NimBLE's `ble_addr_t` layout (type + 6-byte val) byte-for-byte
 * without pulling in any NimBLE headers. sdf_ble_companion.c converts
 * to/from the real ble_addr_t via a plain memcpy. */
typedef struct {
    uint8_t type;
    uint8_t val[6];
} sdf_ble_companion_addr_t;

typedef struct {
    bool in_use;
    sdf_ble_companion_addr_t identity;
    bool allow_listed;
    uint8_t failed_login_count;
} sdf_ble_companion_bond_entry_t;

typedef struct {
    sdf_ble_companion_bond_entry_t entries[SDF_BLE_COMPANION_BOND_TABLE_MAX];
    bool pairing_window_open;
} sdf_ble_companion_bond_state_t;

/* Clears every entry and closes the window - the state this module is in
 * right after boot/reinit. The failed-login counters and the allow list
 * itself both start empty here; sdf_ble_companion_init() is responsible for
 * re-populating the allow list (but not the counters) from NimBLE's own
 * NVS-persisted bond store immediately afterward, so a reboot resets only
 * the counters, not which devices are trusted. */
void sdf_ble_companion_bond_state_init(sdf_ble_companion_bond_state_t *state);

bool sdf_ble_companion_addr_eq(const sdf_ble_companion_addr_t *a,
                                const sdf_ble_companion_addr_t *b);

bool sdf_ble_companion_bond_is_allow_listed(const sdf_ble_companion_bond_state_t *state,
                                             const sdf_ble_companion_addr_t *addr);

/* Adds `addr` to the allow list, creating a fresh (zero failed-login-count)
 * entry if one doesn't already exist. Returns false only if the table is
 * full and `addr` isn't already tracked. */
bool sdf_ble_companion_bond_allow_list_add(sdf_ble_companion_bond_state_t *state,
                                            const sdf_ble_companion_addr_t *addr);

/* Removes `addr`'s entire bond-tracking entry (allow-list membership and
 * failed-login counter both go away) so a future re-pair starts fresh.
 * Returns false if `addr` wasn't tracked. */
bool sdf_ble_companion_bond_allow_list_remove(sdf_ble_companion_bond_state_t *state,
                                               const sdf_ble_companion_addr_t *addr);

/* Increments `addr`'s failed-login counter (creating a tracked-but-not-
 * allow-listed entry if somehow not already present) and returns the new
 * count. Keyed by resolved identity, not conn_handle, so it survives
 * disconnect/reconnect within uptime. */
uint8_t sdf_ble_companion_bond_note_login_failure(sdf_ble_companion_bond_state_t *state,
                                                   const sdf_ble_companion_addr_t *addr);

/* Resets `addr`'s failed-login counter to zero on a successful LOGIN. */
void sdf_ble_companion_bond_note_login_success(sdf_ble_companion_bond_state_t *state,
                                                const sdf_ble_companion_addr_t *addr);

/* Pure threshold check, split out from note_login_failure() so callers can
 * decide what "eviction" means (bond store deletion, connection
 * termination) without this module needing to know about either. */
static inline bool sdf_ble_companion_bond_should_evict(uint8_t failed_login_count) {
    return failed_login_count >= SDF_BLE_COMPANION_FAILED_LOGIN_THRESHOLD;
}

void sdf_ble_companion_bond_open_window(sdf_ble_companion_bond_state_t *state);
void sdf_ble_companion_bond_close_window(sdf_ble_companion_bond_state_t *state);
bool sdf_ble_companion_bond_window_is_open(const sdf_ble_companion_bond_state_t *state);

/* First-bond-wins pairing-window admission: if the window is open, adds
 * `addr` to the allow list and closes the window as one atomic state
 * transition, returning true (the caller must then cancel the window's own
 * timeout and revert advertising). If the window isn't open, this is just
 * an ordinary reconnect of an already-trusted device - no state change,
 * returns false. */
bool sdf_ble_companion_bond_admit_if_window_open(sdf_ble_companion_bond_state_t *state,
                                                  const sdf_ble_companion_addr_t *addr);

/* Snapshots every currently allow-listed identity into out_addrs (capacity
 * max_count) for pushing into the NimBLE Filter Accept List. Returns the
 * number written. */
size_t sdf_ble_companion_bond_snapshot_allow_list(const sdf_ble_companion_bond_state_t *state,
                                                    sdf_ble_companion_addr_t *out_addrs,
                                                    size_t max_count);

/* ---------------------------------------------------------------------------
 * Setup-phase trust decisions (app-guided-first-time-setup). Pure functions
 * so the host suite can pin the latch/armed combinations and the
 * admission-intersection rule down without the BLE stack.
 * ------------------------------------------------------------------------- */

typedef enum {
    SDF_BLE_COMPANION_ADV_MODE_NOT_ADVERTISING = 0,
    SDF_BLE_COMPANION_ADV_MODE_SPARSE_FILTERED = 1,
    SDF_BLE_COMPANION_ADV_MODE_UNFILTERED_SETUP = 2,
    SDF_BLE_COMPANION_ADV_MODE_PAIRING_WINDOW = 3,
} sdf_ble_companion_adv_mode_t;

/* Advertising-mode selection used by restart_advertising(). Priority order:
 * an open pairing window wins; then, while the setup-completion latch is
 * unset, armed means unfiltered connectable setup advertising and disarmed
 * means silence; once the latch is set the sparse filtered default applies. */
sdf_ble_companion_adv_mode_t sdf_ble_companion_select_advertising_mode(
    bool pairing_window_open, bool setup_complete, bool setup_phase_armed);

/* Setup-phase connection cap: while the latch is unset, at most one client
 * may hold a connection - any second inbound one is terminated at connect.
 * Once set, the ordinary slot limit governs. `connected_others` counts
 * currently-tracked connections other than the inbound one. */
bool sdf_ble_companion_should_terminate_second_connection(
    bool setup_complete, size_t connected_others);

/* Allow-list seeding as the intersection of persisted admission records and
 * the persisted bond store: adds to `state`'s allow list exactly the bonded
 * peers that also appear in `admitted`. Bonds without admission grant
 * nothing; admissions without bonds cannot resurrect anything either.
 * Returns the number of peers newly allow-listed. */
size_t sdf_ble_companion_allow_list_seed_intersection(
    sdf_ble_companion_bond_state_t *state,
    const sdf_ble_companion_addr_t *bonded, size_t num_bonded,
    const sdf_ble_companion_addr_t *admitted, size_t num_admitted);

#ifdef __cplusplus
}
#endif

#endif /* SDF_BLE_COMPANION_BOND_STATE_H */

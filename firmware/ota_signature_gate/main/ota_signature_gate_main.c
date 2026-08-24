/* ota-signature-emulator-gate fixture (add-ble-ota-emulator-harness, Layer 1).
 *
 * Drives sdf_ota_begin()/sdf_ota_write()/sdf_ota_verify_integrity()/
 * sdf_ota_verify_and_commit() - the same call order
 * sdf_ble_companion_ota.c's END handler uses (sdf_ble_companion_ota.c:244-
 * 260) - directly against three images derived from one pre-staged blob,
 * with no transport in between (spec: ota-signature-emulator-gate,
 * "Driven through the OTA session API"). See design.md D1-D5 for why this
 * layer exists standalone and how its fixtures are laid out.
 *
 * Images are pre-staged in flash rather than transferred: scripts/
 * ota_signature_gate_prepare.py splices a 16-byte manifest, the fixture's
 * own signed binary (the D2 "valid" image), a 4096-byte foreign signature
 * sector, and a tampered-and-checksum/hash-repaired image (D3) into the
 * `fixtures` partition at build time. This file only reads that partition;
 * it never writes it.
 *
 * Run order is reject (tampered), reject (foreign key), accept (valid) -
 * D5 - because the accept case's successful commit reboots the device
 * (sdf_ota_verify_and_commit() calls esp_restart() unconditionally on
 * success and never returns to its caller; see sdf_ota.c:463-473). The
 * reject cases' "boot partition unchanged" assertions would be meaningless
 * if they ran after that reboot.
 *
 * Deviation from this file's earlier draft: the accept case's result used
 * to be confirmed by surviving the reboot and checking, on the next boot,
 * that esp_ota_get_running_partition() had advanced (RTC_NOINIT_ATTR state
 * carried the expectation across esp_restart(), a software reset). Under
 * esp-emu that reboot reliably crashes before app_main() runs again:
 * ESP-IDF re-probes the SPI flash chip's JEDEC size on every app boot
 * (esp_flash_init_default_chip(), esp_flash_spi_init.c:654) and, only on
 * the boot immediately after this fixture's OTA write/erase activity,
 * esp-emu's simulated flash answers a smaller size than the app image
 * header declares, aborting with "Detected size(4096k) smaller than the
 * size in the binary image header(8192k). Probe failed." A real NOR
 * flash's JEDEC ID is a fixed hardware property that cannot change between
 * two reads of the same chip, cold boot or warm, so this failure signature
 * has no real-hardware analog - traced to a warm-reboot fidelity gap in how
 * esp-emu models the SPI flash peripheral, not to this fixture, to
 * sdf_ota, or to production firmware (which reboots via the identical
 * esp_restart() call path on every real commit and does not exhibit this).
 *
 * The fix does not depend on surviving that reboot at all. sdf_ota.c's
 * state machine emits SDF_AUDIT_OTA_COMMITTED (sdf_ota.c:111-112) from
 * inside sdf_ota_state_transition(SDF_OTA_STATE_COMPLETE) (sdf_ota.c:463),
 * which sdf_ota_verify_and_commit() only reaches after
 * esp_ota_set_boot_partition() has already succeeded (sdf_ota.c:455-459)
 * and strictly before its call to esp_restart() (sdf_ota.c:472). Observing
 * that specific audit event from this file's sdf_app_emit_audit() callback
 * - which sdf_ota_emit_audit() invokes synchronously, same call stack,
 * same boot (sdf_ota.c:66-71) - is therefore just as strong a proof of
 * "boot partition updated" (spec: Correctly Signed Image Accepted) as
 * booting the new partition would have been, and it is observable entirely
 * within the first boot, before esp-emu's warm-reboot gap can matter.
 *
 * Deviation, design.md D3 correction: the tampered case used to flip a
 * payload byte live, in gate_run_case(), with no repair. That failed at
 * process_checksum() (esp_image_format.c:215-232) before signature code
 * ever ran, whether or not signature verification was enabled - it tested
 * the image format's checksum, not the signature. The tampered case is now
 * a *fourth pre-staged region*, built host-side by scripts/
 * ota_signature_gate_prepare.py's generate_tampered_repaired_image(): one
 * byte flipped inside a loaded segment, then the checksum byte and
 * appended SHA-256 recomputed so the image is internally consistent,
 * keeping the original (now stale) signature sector. gate_run_case() below
 * just reads it, the same way it already read the foreign-key case's
 * substituted sector - no on-device repair logic.
 *
 * This also means task 4.4's self-test (verification compiled out, confirm
 * the tampered and foreign-key images are now *accepted*) can no longer
 * reuse the same reject-reject-accept flow: an accepted case commits and
 * reboots unconditionally (sdf_ota.c:472, out of scope for this change to
 * touch), so two cases that both accept cannot both run to completion in
 * one boot. See main/Kconfig.projbuild's GATE_SELF_TEST_MODE choice - the
 * real gate always runs GATE_SELF_TEST_MODE_FULL (this file's original
 * three-case flow); task 4.4 instead builds and boots this same fixture
 * twice, once per single-case self-test mode.
 */

#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "sdf_common.h"
#include "sdf_ota.h"

static const char *TAG = "ota_signature_gate";

typedef enum {
    GATE_CASE_NOT_RUN = 0,
    GATE_CASE_REJECTED,
    GATE_CASE_ACCEPTED,
    GATE_CASE_UNEXPECTED,
} gate_case_outcome_t;

static const char *gate_outcome_str(gate_case_outcome_t outcome) {
    switch (outcome) {
    case GATE_CASE_REJECTED:
        return "REJECT";
    case GATE_CASE_ACCEPTED:
        return "ACCEPT";
    case GATE_CASE_UNEXPECTED:
        return "UNEXPECTED";
    case GATE_CASE_NOT_RUN:
    default:
        return "NOT_RUN";
    }
}

/* Terminal line every run must reach exactly once for a passing exit to be
 * reachable at all (spec: "Success is unambiguous" - a fixture that skipped
 * or never reached the cases exits non-zero). scripts/run_ota_signature_
 * gate.sh matches this exact prefix for --exit-on and greps it for
 * status=PASS to decide the process exit code (task 4.2: "surface the
 * emulator output identifying the failing case"). Only reached by
 * GATE_SELF_TEST_MODE_FULL - the real gate. */
static void gate_emit_terminal(bool pass, int cases_run, gate_case_outcome_t tampered, gate_case_outcome_t foreign_key,
                                gate_case_outcome_t valid) {
    ESP_LOGI(TAG, "OTA_SIGNATURE_GATE_RESULT status=%s cases_run=%d/3 tampered=%s foreign_key=%s valid=%s",
             pass ? "PASS" : "FAIL", cases_run, gate_outcome_str(tampered), gate_outcome_str(foreign_key),
             gate_outcome_str(valid));
}

/* Task 4.4's single-case self-test terminal line - distinct prefix so the
 * runner script's --exit-on can target it independently of the real gate's
 * line above, and so a self-test build can never be mistaken for a real
 * gate PASS by a script only matching OTA_SIGNATURE_GATE_RESULT. */
static void gate_emit_selftest_terminal(bool pass, const char *case_name) {
    ESP_LOGI(TAG, "OTA_SIGNATURE_GATE_SELFTEST_RESULT status=%s case=%s", pass ? "PASS" : "FAIL", case_name);
}

/* Nothing left to prove once a terminal line above has been printed - spin
 * so the run ends via the emulator's --exit-on match on that line (or its
 * --timeout, for a run that never reaches it). */
static void gate_halt(void) __attribute__((noreturn));
static void gate_halt(void) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* Fatal setup failures (no fixtures partition, bad manifest, signing-key
 * mismatch) are reported through the same terminal line as a case failure
 * so the runner script has exactly one place to look, rather than a
 * separate "setup vs. case" failure grammar. cases_run is 0 for all of
 * these since no case ran. */
static void gate_fail_setup(const char *detail) {
    ESP_LOGE(TAG, "FATAL (setup): %s", detail);
    gate_emit_terminal(false, 0, GATE_CASE_NOT_RUN, GATE_CASE_NOT_RUN, GATE_CASE_NOT_RUN);
    gate_halt();
}

/* sdf_ota.c declares this extern and calls it on every state transition and
 * signature failure (sdf_ota.c:17, :66-71). It normally lives in sdf_app,
 * which this fixture does not link - linking sdf_app would pull in the
 * BLE/Zigbee application stack this gate is explicitly independent of
 * (D1). A local stub satisfies the link and doubles as the observation
 * point for both "an OTA_SIGNATURE_INVALID audit event is emitted" (spec:
 * Tampered Image Rejected) and "boot partition updated", for both the real
 * gate's accept case and task 4.4's self-test accept cases (see the file
 * header's deviation notes for why both are checked here rather than after
 * a reboot). */
static bool s_audit_seen_signature_invalid = false;

/* Set only around the accept case's gate_run_case() call, so a stray
 * SDF_AUDIT_OTA_COMMITTED could never be misattributed - not that one can
 * occur elsewhere, since the reject cases are guaranteed to fail
 * verification by construction, but the gate should not rely on that
 * guarantee holding to avoid emitting a false PASS. */
static volatile bool s_case3_active = false;
static gate_case_outcome_t s_case3_tampered_outcome = GATE_CASE_NOT_RUN;
static gate_case_outcome_t s_case3_foreign_key_outcome = GATE_CASE_NOT_RUN;

/* Task 4.4 self-test equivalent of s_case3_active above: set only around a
 * single-case self-test's gate_run_case() call. */
static volatile bool s_selftest_active = false;
static const char *s_selftest_case_name = "";

void sdf_app_emit_audit(sdf_audit_event_type_t type, uint16_t user_id, int32_t status, uint16_t detail) {
    ESP_LOGI(TAG, "audit: type=%d user_id=%u status=%" PRId32 " detail=%u", type, user_id, status, detail);

    /* A rejected commit emits SDF_AUDIT_OTA_SIGNATURE_INVALID and then
     * SDF_AUDIT_OTA_FAILED as the session transitions to FAILED
     * (sdf_ota.c:443-446 followed by the FAILED case in
     * sdf_ota_state_transition()'s switch, sdf_ota.c:114-115) - tracking
     * only the latest event would silently lose the one this fixture needs
     * to assert on. */
    if (type == SDF_AUDIT_OTA_SIGNATURE_INVALID) {
        s_audit_seen_signature_invalid = true;
    }

    /* See the file header's deviation note: this fires synchronously from
     * inside sdf_ota_verify_and_commit(), strictly after
     * esp_ota_set_boot_partition() has succeeded and strictly before its
     * call to esp_restart() (sdf_ota.c:455-472). Emitting a terminal PASS
     * line from here is authoritative, not a proxy - the commit has
     * already fully succeeded by this point - and requires no reboot to
     * observe. */
    if (type == SDF_AUDIT_OTA_COMMITTED && s_case3_active) {
        ESP_LOGI(TAG, "CASE 3 (valid): committed - boot partition updated, reboot imminent");
        gate_emit_terminal(true, 3, s_case3_tampered_outcome, s_case3_foreign_key_outcome, GATE_CASE_ACCEPTED);
    }
    if (type == SDF_AUDIT_OTA_COMMITTED && s_selftest_active) {
        ESP_LOGI(TAG, "SELF-TEST case=%s: committed - accepted as expected with verification compiled out (D3)",
                 s_selftest_case_name);
        gate_emit_selftest_terminal(true, s_selftest_case_name);
    }
}

static void gate_reset_audit(void) {
    s_audit_seen_signature_invalid = false;
}

/* The Secure Boot V2 signature block occupies exactly the trailing 4096
 * bytes of a signed image (design.md D3), and ESP-IDF's own image build
 * pads app images to a sector boundary before that block is appended - the
 * production build confirms this: sdf-unsigned.bin is 1,179,648 bytes
 * (288 * 4096) and sdf.bin is exactly 4096 bytes longer. This fixture's own
 * (smaller) image inherits the same padding behaviour, which is what makes
 * a fixed 4096-byte chunk size land the foreign-key case's substitution
 * exactly on the final chunk with no partial-sector arithmetic (see
 * gate_run_case() below). Reused as the manifest's fixed header size too,
 * for one less magic number. */
#define GATE_CHUNK_SIZE 4096u

/* The `fixtures` partition (D4) is laid out as:
 *   [0, GATE_MANIFEST_SIZE)                                        16-byte manifest
 *   [GATE_MANIFEST_SIZE, +image_size)                              the valid signed image
 *   [GATE_MANIFEST_SIZE + image_size, +GATE_CHUNK_SIZE)            foreign signature sector
 *   [GATE_MANIFEST_SIZE + image_size + GATE_CHUNK_SIZE, +image_size) tampered-and-repaired image (D3)
 * image_size is read from the manifest rather than hard-coded, since it is
 * only known once this fixture's own build has produced a signed binary
 * (scripts/ota_signature_gate_prepare.py writes the manifest as a
 * post-build step, D3). Field layout mirrors the byte sequence
 * scripts/ota_signature_gate_prepare.py writes; kept as raw offsets rather
 * than a packed struct so there is no compiler-padding ambiguity between
 * the two languages. */
#define GATE_MANIFEST_SIZE     16u
#define GATE_MANIFEST_MAGIC    "OTFX"
#define GATE_MANIFEST_MAGIC_LEN 4u
#define GATE_MANIFEST_SIZE_OFFSET 4u

/* D2/spec "Key mismatch surfaces as a gate failure, not a false pass": the
 * staged valid image (fixtures partition) and the running app must carry
 * an identical signature block, since D2 makes the fixture's own build
 * output the accept-case image. A mismatch means the splice step ran
 * against a stale or differently-keyed binary - a setup bug, not a
 * signature-verification result - and must fail loudly rather than let the
 * accept case coincidentally still pass or silently fail for the wrong
 * reason.
 *
 * Compiled only into the real (signature-verification-enabled) build.
 * Task 4.4's self-test builds stage images from a normal build's output
 * (see main/Kconfig.projbuild and sdkconfig.no_verify.defaults) but boot a
 * no-verify build as the checking app - one with no signature block of its
 * own at all (CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES cascades off along
 * with CONFIG_SECURE_SIGNED_ON_UPDATE) and a different image size, so this
 * precondition does not apply to it. */
#if CONFIG_SECURE_SIGNED_ON_UPDATE
static void gate_assert_signature_blocks_match(const esp_partition_t *fixtures, uint32_t image_size) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        gate_fail_setup("esp_ota_get_running_partition() returned NULL");
    }

    static uint8_t staged_sig[GATE_CHUNK_SIZE];
    static uint8_t running_sig[GATE_CHUNK_SIZE];

    esp_err_t err = esp_partition_read(fixtures, GATE_MANIFEST_SIZE + image_size - GATE_CHUNK_SIZE, staged_sig,
                                        sizeof(staged_sig));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read(fixtures, staged sig) failed: %s", esp_err_to_name(err));
        gate_fail_setup("could not read the staged image's signature block");
    }

    err = esp_partition_read(running, image_size - GATE_CHUNK_SIZE, running_sig, sizeof(running_sig));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read(running, own sig) failed: %s", esp_err_to_name(err));
        gate_fail_setup("could not read the running image's own signature block");
    }

    if (memcmp(staged_sig, running_sig, sizeof(staged_sig)) != 0) {
        gate_fail_setup("staged valid image's signature block does not match the running image's own - "
                         "they were signed with different keys (D2 setup failure, not a case result)");
    }

    ESP_LOGI(TAG, "Staged valid image's signature block matches the running image's own (D2 trust anchor check OK)");
}
#endif /* CONFIG_SECURE_SIGNED_ON_UPDATE */

/* Which pre-staged region of the fixtures partition gate_run_case() reads
 * from (see the partition layout comment on GATE_MANIFEST_SIZE above). */
typedef enum {
    GATE_SOURCE_VALID = 0,
    GATE_SOURCE_TAMPERED,
    GATE_SOURCE_FOREIGN_KEY,
} gate_image_source_t;

/* Streams the staged image through one OTA session, in the same order
 * sdf_ble_companion_ota.c's END handler uses (begin, write*, verify_
 * integrity, verify_and_commit, abort-on-any-failure -
 * sdf_ble_companion_ota.c:145-152, :191-194, :244-260).
 *
 * image_size is always a multiple of GATE_CHUNK_SIZE (see the comment on
 * that macro), so every chunk including the last is exactly GATE_CHUNK_SIZE
 * bytes - the foreign_key substitution below relies on that to land the
 * swap exactly on the final chunk rather than needing partial-sector
 * arithmetic. Guarded by an assertion in the caller rather than repeated
 * here, so a violation fails loudly once instead of silently on every case.
 *
 * On success, sdf_ota_verify_and_commit() reboots the device and does not
 * return (sdf_ota.c:463-473); on any other outcome the function returns the
 * failing esp_err_t. */
static esp_err_t gate_run_case(const esp_partition_t *fixtures, uint32_t image_size, gate_image_source_t source) {
    sdf_ota_handle_t handle = NULL;
    esp_err_t err = sdf_ota_begin(SDF_OTA_SOURCE_CLI, image_size, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdf_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    /* GATE_SOURCE_TAMPERED reads a whole separate pre-repaired region
     * (D3) - the tamper, checksum repair and appended-hash repair all
     * happen host-side in scripts/ota_signature_gate_prepare.py, not here,
     * since doing it on-device would mean re-deriving and re-verifying the
     * same repair algorithm in two languages for no benefit. */
    const uint32_t read_base =
        (source == GATE_SOURCE_TAMPERED) ? (GATE_MANIFEST_SIZE + image_size + GATE_CHUNK_SIZE) : GATE_MANIFEST_SIZE;

    static uint8_t buf[GATE_CHUNK_SIZE];
    for (uint32_t offset = 0; offset < image_size; offset += GATE_CHUNK_SIZE) {
        bool is_final_chunk_sector = (offset == image_size - GATE_CHUNK_SIZE);

        if (source == GATE_SOURCE_FOREIGN_KEY && is_final_chunk_sector) {
            /* Substitute the whole trailing sector rather than patching it,
             * since it *is* exactly one chunk (design.md D3). */
            err = esp_partition_read(fixtures, GATE_MANIFEST_SIZE + image_size, buf, GATE_CHUNK_SIZE);
        } else {
            err = esp_partition_read(fixtures, read_base + offset, buf, GATE_CHUNK_SIZE);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_partition_read(fixtures) failed at offset %" PRIu32 ": %s", offset,
                     esp_err_to_name(err));
            sdf_ota_abort(handle);
            return err;
        }

        err = sdf_ota_write(handle, buf, GATE_CHUNK_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "sdf_ota_write failed at offset %" PRIu32 ": %s", offset, esp_err_to_name(err));
            /* sdf_ota_write() already fails the session internally
             * (sdf_ota.c:270-276); the abort here is the same
             * belt-and-braces call the BLE transport makes for the same
             * reason (sdf_ble_companion_ota.c:194) - harmless against an
             * already-released session. */
            sdf_ota_abort(handle);
            return err;
        }
    }

    err = sdf_ota_verify_integrity(handle);
    if (err == ESP_OK) {
        err = sdf_ota_verify_and_commit(handle);
    }
    if (err != ESP_OK) {
        sdf_ota_abort(handle);
    }
    return err;
}

/* Cases 1 and 2 (reject, reject) - see the file header for why they must
 * run before the accept case. Only used by GATE_SELF_TEST_MODE_FULL (the
 * real gate); task 4.4's self-test builds call gate_run_self_test_single_
 * case() instead, since both cases are expected to *accept* there and an
 * accept reboots before a second case could run. */
static void gate_run_reject_cases(const esp_partition_t *fixtures, uint32_t image_size,
                                   gate_case_outcome_t *tampered_out, gate_case_outcome_t *foreign_out) {
    /* --- Case 1: tampered (spec: Tampered Image Rejected) --- */
    const esp_partition_t *boot_before = esp_ota_get_boot_partition();
    gate_reset_audit();
    esp_err_t err = gate_run_case(fixtures, image_size, GATE_SOURCE_TAMPERED);
    const esp_partition_t *boot_after = esp_ota_get_boot_partition();

    if (err == SDF_ERR_OTA_SIGNATURE_INVALID && s_audit_seen_signature_invalid && boot_after == boot_before) {
        *tampered_out = GATE_CASE_REJECTED;
        ESP_LOGI(TAG, "CASE 1 (tampered): rejected as expected");
    } else {
        *tampered_out = GATE_CASE_UNEXPECTED;
        ESP_LOGE(TAG,
                 "CASE 1 (tampered) DID NOT REJECT AS EXPECTED: err=%s audit_signature_invalid_seen=%d "
                 "boot_partition_changed=%d",
                 esp_err_to_name(err), s_audit_seen_signature_invalid, boot_after != boot_before);
        return;
    }

    /* --- Case 2: foreign key (spec: Image Signed With A Different Key
     * Rejected). Structural validity of the foreign-signed artifact is
     * asserted before this run, by scripts/ota_signature_gate_prepare.py
     * (task 3.4) - not here - so that a malformed artifact fails the build
     * step loudly instead of masquerading as this case's rejection.
     *
     * The assertion below is deliberately identical to case 1's, and for the
     * same reason (spec: "Rejection is attributable to the key, not to
     * malformation"): merely observing `err != ESP_OK` would let a failing
     * sdf_ota_begin() - which is exactly the session-recovery break D5 exists
     * to catch, since this case follows a rejected one - or a partition read
     * error, or an allocation failure, all report "rejected as expected" and
     * turn this case green without any signature check having run. A genuine
     * foreign-key rejection reaches the same code path the tampered case
     * does: esp_ota_end() returns ESP_ERR_OTA_VALIDATE_FAILED, which
     * sdf_ota_verify_and_commit() maps onto SDF_ERR_OTA_SIGNATURE_INVALID
     * after emitting SDF_AUDIT_OTA_SIGNATURE_INVALID (sdf_ota.c:441-445), so
     * the strict form is available here and nothing weaker is warranted. */
    boot_before = esp_ota_get_boot_partition();
    gate_reset_audit();
    err = gate_run_case(fixtures, image_size, GATE_SOURCE_FOREIGN_KEY);
    boot_after = esp_ota_get_boot_partition();

    if (err == SDF_ERR_OTA_SIGNATURE_INVALID && s_audit_seen_signature_invalid && boot_after == boot_before) {
        *foreign_out = GATE_CASE_REJECTED;
        ESP_LOGI(TAG, "CASE 2 (foreign key): rejected as expected");
    } else {
        *foreign_out = GATE_CASE_UNEXPECTED;
        ESP_LOGE(TAG,
                 "CASE 2 (foreign key) DID NOT REJECT AS EXPECTED: err=%s audit_signature_invalid_seen=%d "
                 "boot_partition_changed=%d",
                 esp_err_to_name(err), s_audit_seen_signature_invalid, boot_after != boot_before);
    }
}

/* Case 3: valid (spec: Correctly Signed Image Accepted). A successful
 * commit reboots before this function can return - sdf_app_emit_audit()
 * above emits the gate's terminal PASS line synchronously, before that
 * reboot, once it observes SDF_AUDIT_OTA_COMMITTED (see the file header's
 * deviation note). This function only returns if the commit unexpectedly
 * failed. */
static void gate_run_accept_case(const esp_partition_t *fixtures, uint32_t image_size, gate_case_outcome_t tampered,
                                  gate_case_outcome_t foreign_key) {
    s_case3_tampered_outcome = tampered;
    s_case3_foreign_key_outcome = foreign_key;
    s_case3_active = true;

    ESP_LOGI(TAG, "CASE 3 (valid): committing - a successful commit reboots unconditionally "
                  "(sdf_ota.c's esp_restart()) and this call will not return; sdf_app_emit_audit() "
                  "reports the result synchronously, before that reboot (spec: Session Recovery "
                  "After Rejection - this is also the 'reject then accept' recovery case, D5).");

    esp_err_t err = gate_run_case(fixtures, image_size, GATE_SOURCE_VALID);

    /* Only reached on an unexpected failure - a genuine accept reboots
     * before returning here. */
    s_case3_active = false;
    ESP_LOGE(TAG, "CASE 3 (valid) FAILED TO COMMIT: %s", esp_err_to_name(err));
    gate_emit_terminal(false, 3, tampered, foreign_key, GATE_CASE_UNEXPECTED);
    gate_halt();
}

/* Task 4.4's self-test: run exactly one case and expect it to be
 * *accepted* with signature verification compiled out (design.md D3's
 * discriminating self-test - "if [the tampered image] is still rejected,
 * the gate is measuring the checksum rather than the signature"). A
 * successful commit reboots before this function returns, mirroring
 * gate_run_accept_case() above; sdf_app_emit_audit() reports the PASS line
 * synchronously beforehand. Only reached if the commit unexpectedly failed
 * - i.e. the case is still being rejected even with verification off,
 * which per D3 means the tampered/foreign-key fixture was not repaired
 * correctly and must be fixed before task 4.4 can be checked. */
static void gate_run_self_test_single_case(const esp_partition_t *fixtures, uint32_t image_size,
                                            gate_image_source_t source, const char *case_name) {
    ESP_LOGI(TAG, "SELF-TEST (task 4.4, verification compiled out): running case=%s, expecting ACCEPT", case_name);
    gate_reset_audit();
    s_selftest_case_name = case_name;
    s_selftest_active = true;

    esp_err_t err = gate_run_case(fixtures, image_size, source);

    s_selftest_active = false;
    ESP_LOGE(TAG,
             "SELF-TEST case=%s DID NOT ACCEPT: err=%s - still rejected with verification compiled out; "
             "the fixture's checksum/appended-hash repair (D3) is not internally consistent",
             case_name, esp_err_to_name(err));
    gate_emit_selftest_terminal(false, case_name);
    gate_halt();
}

void app_main(void) {
    ESP_LOGI(TAG, "ota_signature_gate fixture starting");

    ESP_ERROR_CHECK(sdf_ota_init());

    const esp_partition_t *fixtures = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                                                 "fixtures");
    if (fixtures == NULL) {
        gate_fail_setup("no 'fixtures' data partition found - the merged flash image was not spliced correctly "
                         "(scripts/ota_signature_gate_prepare.py did not run, or the partition table is wrong)");
    }

    uint8_t manifest[GATE_MANIFEST_SIZE];
    esp_err_t err = esp_partition_read(fixtures, 0, manifest, sizeof(manifest));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read(fixtures, manifest) failed: %s", esp_err_to_name(err));
        gate_fail_setup("could not read the fixtures partition manifest");
    }
    if (memcmp(manifest, GATE_MANIFEST_MAGIC, GATE_MANIFEST_MAGIC_LEN) != 0) {
        gate_fail_setup("fixtures partition manifest magic mismatch - the splice step did not run, or ran against "
                         "a different layout than this firmware expects");
    }

    uint32_t image_size;
    memcpy(&image_size, manifest + GATE_MANIFEST_SIZE_OFFSET, sizeof(image_size));

    if (image_size % GATE_CHUNK_SIZE != 0) {
        /* gate_run_case()'s foreign-key substitution assumes the final
         * chunk *is* the signature sector; if that ever stops holding, fail
         * loudly here instead of silently writing the valid signature in
         * the foreign-key case (D3's correctness constraint). */
        gate_fail_setup("staged image size is not a multiple of the 4096-byte sector size - the foreign-key "
                         "case's chunk-aligned substitution would silently write the wrong bytes");
    }

    ESP_LOGI(TAG, "Staged valid image is %" PRIu32 " bytes", image_size);

#if CONFIG_SECURE_SIGNED_ON_UPDATE
    gate_assert_signature_blocks_match(fixtures, image_size);
#else
    ESP_LOGI(TAG, "CONFIG_SECURE_SIGNED_ON_UPDATE is off - skipping the D2 trust-anchor precondition, since this "
                  "build's own signature block (if any) does not correspond to the staged images (task 4.4 "
                  "self-test only; see sdkconfig.no_verify.defaults)");
#endif

#if CONFIG_GATE_SELF_TEST_MODE_TAMPERED_ONLY
    gate_run_self_test_single_case(fixtures, image_size, GATE_SOURCE_TAMPERED, "tampered");
#elif CONFIG_GATE_SELF_TEST_MODE_FOREIGN_KEY_ONLY
    gate_run_self_test_single_case(fixtures, image_size, GATE_SOURCE_FOREIGN_KEY, "foreign_key");
#else
    gate_case_outcome_t tampered = GATE_CASE_NOT_RUN;
    gate_case_outcome_t foreign_key = GATE_CASE_NOT_RUN;
    gate_run_reject_cases(fixtures, image_size, &tampered, &foreign_key);

    if (tampered != GATE_CASE_REJECTED) {
        gate_emit_terminal(false, 1, tampered, foreign_key, GATE_CASE_NOT_RUN);
        gate_halt();
    }
    if (foreign_key != GATE_CASE_REJECTED) {
        gate_emit_terminal(false, 2, tampered, foreign_key, GATE_CASE_NOT_RUN);
        gate_halt();
    }

    gate_run_accept_case(fixtures, image_size, tampered, foreign_key); /* reboots on success; only returns on failure */
#endif
}

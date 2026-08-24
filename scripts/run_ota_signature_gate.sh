#!/usr/bin/env bash
# ota-signature-emulator-gate (Layer 1, add-ble-ota-emulator-harness).
#
# Builds the firmware/ota_signature_gate fixture out-of-tree, stages its
# valid/tampered/foreign-key images into a merged flash image (D2/D3/D4),
# and boots the result under esp-emu with no hardware attached. Exits
# non-zero on build failure, on esp-emu reporting a non-PASS gate result, or
# on esp-emu not reaching the gate's terminal log line before --timeout
# (task 4.2 - a hang must fail the gate, not hang the caller).
#
# Usage: scripts/run_ota_signature_gate.sh [--no-verify]
#   --no-verify   Task 4.4's regression check only - never use for the real
#                 gate. Confirms the discriminating self-test design.md D3
#                 exists for: with signature verification compiled out, the
#                 integrity-repaired tampered image and the foreign-key
#                 image must both be *accepted*. Because a successful
#                 sdf_ota_verify_and_commit() reboots unconditionally
#                 (sdf_ota.c:472) and two cases that both accept cannot run
#                 to completion in the same boot, this builds and boots the
#                 fixture THREE times: once normally (verification enabled,
#                 used only as the source of the staged images - see
#                 sdkconfig.no_verify.defaults for why the no-verify
#                 build's own output cannot be used for that), then once
#                 per case with verification compiled out and that single
#                 case selected (main/Kconfig.projbuild's GATE_SELF_TEST_
#                 MODE), asserting each case is accepted.
#
# Env overrides (mainly for CI, see .github/workflows/firmware-ci.yml):
#   OTA_GATE_BUILD_DIR   Out-of-tree build directory (default: a fresh dir
#                        under mktemp -d, removed on exit).
#   OTA_GATE_TIMEOUT     esp-emu --timeout value (default: 240s).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURE_DIR="${REPO_ROOT}/firmware/ota_signature_gate"

NO_VERIFY=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-verify) NO_VERIFY=1; shift ;;
    -h|--help)
      sed -n '2,27p' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

CLEANUP_BUILD_DIR=0
if [[ -z "${OTA_GATE_BUILD_DIR:-}" ]]; then
  OTA_GATE_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ota_gate_build.XXXXXX")"
  CLEANUP_BUILD_DIR=1
fi
BUILD_DIR="${OTA_GATE_BUILD_DIR}"
TIMEOUT="${OTA_GATE_TIMEOUT:-240s}"

cleanup() {
  if [[ "${CLEANUP_BUILD_DIR}" -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
  fi
}
trap cleanup EXIT

if [[ ! -f "${REPO_ROOT}/ota_signing_key.pem" ]]; then
  echo "::error:: ${REPO_ROOT}/ota_signing_key.pem not found. Build firmware/ (or firmware/test_runner) at least" \
       "once first, or provide OTA_SIGNING_KEY, so the fixture's own signing key exists (D2: the trust" \
       "anchor with secure boot disabled is the running app's own signature block)." >&2
  exit 1
fi

# Every build below produces its outputs at these fixed, build-dir-relative
# paths (idf.py's PROJECT_NAME defaults to the directory name), so nothing
# else needs to track them separately.
app_bin_for() { echo "$1/ota_signature_gate.bin"; }
app_elf_for() { echo "$1/ota_signature_gate.elf"; }
merged_bin_for() { echo "$1/merged.bin"; }

# build_fixture <build_dir> <sdkconfig_defaults> <label>
# set -o pipefail (set -eu above already covers pipefail via `set -euo
# pipefail`) so a build failure piped through tee still fails this script
# rather than being swallowed by tee's exit code.
build_fixture() {
  local build_dir="$1" sdkconfig_defaults="$2" label="$3"
  echo "== Building ota_signature_gate fixture (${label}, esp32c6, out-of-tree at ${build_dir}) =="
  # set-target's own "fullclean" dependency refuses to run against a build
  # directory containing anything it doesn't recognize as CMake output
  # ("doesn't seem to be a CMake build directory. Refusing to automatically
  # delete files..."). tee creates/opens its output file immediately, before
  # idf.py's cmake invocation has produced any recognized marker, so teeing
  # this step's log *into* build_dir deposits exactly such a foreign file
  # and trips that refusal - the log has to live next to build_dir, not
  # inside it. (build.log below is safe: by then set-target has already
  # populated build_dir with CMakeCache.txt, so fullclean's check does not
  # re-trigger.)
  idf.py -C "${FIXTURE_DIR}" -B "${build_dir}" -D SDKCONFIG_DEFAULTS="${sdkconfig_defaults}" \
    -D SDKCONFIG="${build_dir}/sdkconfig" set-target esp32c6 | tee "${build_dir}.set-target.log"
  idf.py -C "${FIXTURE_DIR}" -B "${build_dir}" -D SDKCONFIG="${build_dir}/sdkconfig" build \
    | tee "${build_dir}/build.log"

  local app_bin app_elf
  app_bin="$(app_bin_for "${build_dir}")"
  app_elf="$(app_elf_for "${build_dir}")"
  if [[ ! -f "${app_bin}" || ! -f "${app_elf}" ]]; then
    echo "::error:: (${label}) expected build outputs missing: ${app_bin} / ${app_elf}" >&2
    exit 1
  fi
}

# merge_fixture <build_dir> <label>
merge_fixture() {
  local build_dir="$1" label="$2"
  echo "== Merging flash image (${label}, 8MB, matching firmware/partition_table.csv's flash size) =="
  idf.py -C "${FIXTURE_DIR}" -B "${build_dir}" -D SDKCONFIG="${build_dir}/sdkconfig" merge-bin \
    -o "$(merged_bin_for "${build_dir}")" --pad-to-size 8MB \
    | tee "${build_dir}/merge-bin.log"
}

# stage_fixture <app_bin_source> <merged_bin> <key_dir> <label>
stage_fixture() {
  local app_bin_source="$1" merged_bin="$2" key_dir="$3" label="$4"
  echo "== Staging fixture images into the merged flash image (${label}) (D2/D3/D4) =="
  python3 "${REPO_ROOT}/scripts/ota_signature_gate_prepare.py" \
    --app-bin "${app_bin_source}" \
    --merged-bin "${merged_bin}" \
    --key-dir "${key_dir}" \
    --signing-key "${REPO_ROOT}/ota_signing_key.pem"
}

# boot_fixture <build_dir> <exit_on> <pass_pattern> <label>
# Returns non-zero (does not exit the script) so callers can run multiple
# boots and report all their failures rather than stopping at the first.
boot_fixture() {
  local build_dir="$1" exit_on="$2" pass_pattern="$3" label="$4"
  echo "== Booting merged image under esp-emu (${label}, no hardware attached) =="
  set +e
  esp-emu --chip esp32c6 --firmware "$(merged_bin_for "${build_dir}")" --elf "$(app_elf_for "${build_dir}")" \
    --timeout "${TIMEOUT}" --exit-on "${exit_on}" \
    | tee "${build_dir}/emu.log"
  local emu_exit=${PIPESTATUS[0]}
  set -e

  if [[ "${emu_exit}" -ne 0 ]]; then
    echo "::error:: (${label}) esp-emu exited ${emu_exit} (timeout or crash before the gate reported a result)" >&2
    return 1
  fi
  if ! grep -q "${pass_pattern}" "${build_dir}/emu.log"; then
    echo "::error:: (${label}) did not report PASS - see ${build_dir}/emu.log" >&2
    return 1
  fi
  return 0
}

if [[ "${NO_VERIFY}" -eq 0 ]]; then
  build_fixture "${BUILD_DIR}" "sdkconfig.defaults" "real gate"
  merge_fixture "${BUILD_DIR}" "real gate"
  stage_fixture "$(app_bin_for "${BUILD_DIR}")" "$(merged_bin_for "${BUILD_DIR}")" "${BUILD_DIR}/foreign_key" \
    "real gate"
  boot_fixture "${BUILD_DIR}" "OTA_SIGNATURE_GATE_RESULT" "OTA_SIGNATURE_GATE_RESULT status=PASS" "real gate"
  echo "== ota-signature-emulator-gate: PASS =="
  exit 0
fi

echo "== task 4.4: signature verification compiled out (regression check only) =="

NORMAL_DIR="${BUILD_DIR}/normal"
TAMPERED_DIR="${BUILD_DIR}/no_verify_tampered"
FOREIGN_KEY_DIR="${BUILD_DIR}/no_verify_foreign_key"
mkdir -p "${NORMAL_DIR}" "${TAMPERED_DIR}" "${FOREIGN_KEY_DIR}"

# The staged images (valid, tampered-repaired, foreign-key) always come
# from a normal, verification-enabled build - never from a no-verify
# build's own output, which is genuinely unsigned and a different size
# (see sdkconfig.no_verify.defaults). This build's ELF/merged image is not
# booted; only its .bin feeds scripts/ota_signature_gate_prepare.py.
build_fixture "${NORMAL_DIR}" "sdkconfig.defaults" "normal (staging source)"
NORMAL_APP_BIN="$(app_bin_for "${NORMAL_DIR}")"

SELFTEST_RESULT=0

build_fixture "${TAMPERED_DIR}" "sdkconfig.defaults;sdkconfig.no_verify.defaults;sdkconfig.no_verify_tampered.defaults" \
  "no-verify, tampered case only"
merge_fixture "${TAMPERED_DIR}" "no-verify, tampered case only"
stage_fixture "${NORMAL_APP_BIN}" "$(merged_bin_for "${TAMPERED_DIR}")" "${TAMPERED_DIR}/foreign_key" \
  "no-verify, tampered case only"
boot_fixture "${TAMPERED_DIR}" "OTA_SIGNATURE_GATE_SELFTEST_RESULT" \
  "OTA_SIGNATURE_GATE_SELFTEST_RESULT status=PASS case=tampered" "no-verify, tampered case only" \
  || SELFTEST_RESULT=1

build_fixture "${FOREIGN_KEY_DIR}" \
  "sdkconfig.defaults;sdkconfig.no_verify.defaults;sdkconfig.no_verify_foreign_key.defaults" \
  "no-verify, foreign-key case only"
merge_fixture "${FOREIGN_KEY_DIR}" "no-verify, foreign-key case only"
stage_fixture "${NORMAL_APP_BIN}" "$(merged_bin_for "${FOREIGN_KEY_DIR}")" "${FOREIGN_KEY_DIR}/foreign_key" \
  "no-verify, foreign-key case only"
boot_fixture "${FOREIGN_KEY_DIR}" "OTA_SIGNATURE_GATE_SELFTEST_RESULT" \
  "OTA_SIGNATURE_GATE_SELFTEST_RESULT status=PASS case=foreign_key" "no-verify, foreign-key case only" \
  || SELFTEST_RESULT=1

if [[ "${SELFTEST_RESULT}" -ne 0 ]]; then
  echo "::error:: task 4.4 self-test FAILED - see the per-build emu.log paths logged above" >&2
  exit 1
fi

echo "== task 4.4 self-test: PASS (tampered and foreign-key images both accepted with verification compiled out) =="

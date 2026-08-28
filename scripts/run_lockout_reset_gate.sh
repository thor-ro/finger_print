#!/usr/bin/env bash
# lockout-reset-emulator-gate (persist-biometric-lockout, tasks.md 7.3).
#
# Builds the firmware/lockout_reset_gate fixture out-of-tree and boots it
# under esp-emu with no hardware attached. The fixture runs two boots of the
# same image separated by a real esp_restart(): boot 1 arms and persists the
# biometric lockout, boot 2 brings the full sdf_app closure up and checks that
# sdf_services_init()'s boot-time restore re-armed a full
# CONFIG_SDF_SECURITY_BIOMETRIC_LOCKOUT_MS from that boot, that a match cycle
# refuses to reach the sensor, and that the restored lockout announces itself
# once as a CRITICAL SECURITY_LOCKOUT.
#
# Exits non-zero on build failure, on the gate reporting a non-PASS result, or
# on esp-emu not reaching the gate's terminal log line before --timeout (a
# hang must fail the gate, not hang the caller).
#
# Usage: scripts/run_lockout_reset_gate.sh
#
# Env overrides (mainly for CI):
#   LOCKOUT_GATE_BUILD_DIR   Out-of-tree build directory (default: a fresh dir
#                            under mktemp -d, removed on exit).
#   LOCKOUT_GATE_TIMEOUT     esp-emu --timeout value (default: 300s - the
#                            fixture boots twice).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURE_DIR="${REPO_ROOT}/firmware/lockout_reset_gate"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      sed -n '2,23p' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

CLEANUP_BUILD_DIR=0
if [[ -z "${LOCKOUT_GATE_BUILD_DIR:-}" ]]; then
  LOCKOUT_GATE_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/lockout_gate_build.XXXXXX")"
  CLEANUP_BUILD_DIR=1
fi
BUILD_DIR="${LOCKOUT_GATE_BUILD_DIR}"
TIMEOUT="${LOCKOUT_GATE_TIMEOUT:-300s}"

cleanup() {
  if [[ "${CLEANUP_BUILD_DIR}" -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
  fi
}
trap cleanup EXIT

# Same trust anchor as the OTA gates: the fixture carries production's
# CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES, so the build needs the signing key
# even though nothing here exercises OTA.
if [[ ! -f "${REPO_ROOT}/ota_signing_key.pem" ]]; then
  echo "::error:: ${REPO_ROOT}/ota_signing_key.pem not found. Build firmware/ (or firmware/test_runner) at least" \
       "once first so the fixture's signing key exists." >&2
  exit 1
fi

APP_ELF="${BUILD_DIR}/lockout_reset_gate.elf"
MERGED_BIN="${BUILD_DIR}/merged.bin"

# See run_ota_signature_gate.sh for why set-target's log is teed *next to*
# build_dir rather than inside it (fullclean refuses to run against a build
# directory holding a file it does not recognize as CMake output).
echo "== Building lockout_reset_gate fixture (esp32c6, out-of-tree at ${BUILD_DIR}) =="
idf.py -C "${FIXTURE_DIR}" -B "${BUILD_DIR}" -D SDKCONFIG="${BUILD_DIR}/sdkconfig" \
  set-target esp32c6 | tee "${BUILD_DIR}.set-target.log"
idf.py -C "${FIXTURE_DIR}" -B "${BUILD_DIR}" -D SDKCONFIG="${BUILD_DIR}/sdkconfig" build \
  | tee "${BUILD_DIR}/build.log"

if [[ ! -f "${APP_ELF}" ]]; then
  echo "::error:: expected build output missing: ${APP_ELF}" >&2
  exit 1
fi

# 4MB, not the 8MB the OTA gates merge to. This is the only fixture that
# reboots itself, and esp-emu v0.40.1 re-detects the flash as 4MB after a soft
# reset, so an 8MB image header aborts boot 2 in __esp_system_init_fn_init_flash
# before app_main() runs. The partition table ends at 0x400000, so 4MB holds
# production's layout unchanged - see firmware/lockout_reset_gate/
# sdkconfig.defaults, which pins the matching CONFIG_ESPTOOLPY_FLASHSIZE.
echo "== Merging flash image (4MB - esp-emu soft-reset workaround, see below) =="
idf.py -C "${FIXTURE_DIR}" -B "${BUILD_DIR}" -D SDKCONFIG="${BUILD_DIR}/sdkconfig" merge-bin \
  -o "${MERGED_BIN}" --pad-to-size 4MB | tee "${BUILD_DIR}/merge-bin.log"

echo "== Booting merged image under esp-emu (two boots, no hardware attached) =="
set +e
esp-emu --chip esp32c6 --firmware "${MERGED_BIN}" --elf "${APP_ELF}" \
  --timeout "${TIMEOUT}" --exit-on "LOCKOUT_RESET_GATE_RESULT" \
  | tee "${BUILD_DIR}/emu.log"
EMU_EXIT=${PIPESTATUS[0]}
set -e

if [[ "${EMU_EXIT}" -ne 0 ]]; then
  echo "::error:: esp-emu exited ${EMU_EXIT} (timeout or crash before the gate reported a result)" >&2
  exit 1
fi
if ! grep -q "LOCKOUT_RESET_GATE_RESULT status=PASS" "${BUILD_DIR}/emu.log"; then
  echo "::error:: gate did not report PASS - see ${BUILD_DIR}/emu.log" >&2
  exit 1
fi

echo "== lockout-reset-emulator-gate: PASS =="

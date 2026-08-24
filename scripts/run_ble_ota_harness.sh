#!/usr/bin/env bash
# Layer 2 harness runner (add-ble-ota-emulator-harness): builds the
# ble_ota_gate fixture, derives the three test images, boots the fixture under
# esp-emu with an HCI TCP backend, and drives the companion BLE OTA protocol
# with the Bumble central in tools/ble_ota_harness.
#
# Usage: scripts/run_ble_ota_harness.sh
#
# Requires: ESP-IDF environment sourced (idf.py on PATH), esp-emu on PATH,
# and a Python interpreter with bumble (see tools/ble_ota_harness/requirements.txt;
# the script creates/reuses a venv at /tmp/sdf_bleh_venv unless BUMBLE_PYTHON
# points at an interpreter that already has bumble installed).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURE_DIR="$REPO_ROOT/firmware/ble_ota_gate"
HARNESS_DIR="$REPO_ROOT/tools/ble_ota_harness"
SCRATCH="${BLE_OTA_HARNESS_SCRATCH:-/tmp/ble_ota_harness_run}"
BUILD_DIR="$SCRATCH/build"
IMAGE_DIR="$SCRATCH/images"

DEV_PORT=14431
CENTRAL_PORT=14432

EMU_PID=""
cleanup() {
  if [[ -n "$EMU_PID" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
    kill "$EMU_PID" 2>/dev/null || true
    wait "$EMU_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

echo "== ble-ota-emulator-harness: building ble_ota_gate =="
mkdir -p "$BUILD_DIR" "$IMAGE_DIR"
idf.py -C "$FIXTURE_DIR" -B "$BUILD_DIR" \
  -D "SDKCONFIG=$BUILD_DIR/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults' build | tee "$SCRATCH-build.log"
idf.py -C "$FIXTURE_DIR" -B "$BUILD_DIR" \
  -D "SDKCONFIG=$BUILD_DIR/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults' merge-bin | tee "$SCRATCH-merge.log"

APP_BIN="$BUILD_DIR/ble_ota_gate.bin"
MERGED_BIN="$BUILD_DIR/merged-binary.bin"
ELF="$BUILD_DIR/ble_ota_gate.elf"
SIGNING_KEY="$REPO_ROOT/ota_signing_key.pem"
for f in "$APP_BIN" "$MERGED_BIN" "$ELF" "$SIGNING_KEY"; do
  [[ -f "$f" ]] || { echo "::error:: missing build input/output: $f"; exit 1; }
done

echo "== ble-ota-emulator-harness: deriving test images =="
"${BUMBLE_PYTHON:-python3}" "$HARNESS_DIR/prepare_images.py" \
  --app-bin "$APP_BIN" --signing-key "$SIGNING_KEY" --out-dir "$IMAGE_DIR" \
  | tee "$SCRATCH-prepare.log"

if [[ -z "${BUMBLE_PYTHON:-}" ]]; then
  VENV="$SCRATCH/venv"
  if [[ ! -x "$VENV/bin/python" ]]; then
    echo "== ble-ota-emulator-harness: creating bumble venv at $VENV =="
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q -r "$HARNESS_DIR/requirements.txt"
  fi
  BUMBLE_PYTHON="$VENV/bin/python"
fi

echo "== ble-ota-emulator-harness: running harness =="
set -o pipefail
RUN_EXIT=0
# The harness must own the HCI TCP listener before esp-emu dials in (esp-emu
# connects as a TCP client exactly once, early in its boot).
"$BUMBLE_PYTHON" "$HARNESS_DIR/ble_ota_harness.py" \
  --device-port "$DEV_PORT" --central-port "$CENTRAL_PORT" \
  --tampered-image "$IMAGE_DIR/tampered.bin" \
  --foreign-image "$IMAGE_DIR/foreign.bin" \
  --valid-image "$IMAGE_DIR/valid.bin" \
  > "$SCRATCH-harness.log" 2>&1 &
HARNESS_PID=$!
for _ in $(seq 1 30); do
  if nc -z 127.0.0.1 "$DEV_PORT" 2>/dev/null; then break; fi
  if ! kill -0 "$HARNESS_PID" 2>/dev/null; then break; fi
  sleep 1
done

echo "== ble-ota-emulator-harness: booting esp-emu =="
esp-emu --chip esp32c6 --firmware "$MERGED_BIN" --elf "$ELF" \
  --ble-hci "tcp:127.0.0.1:$DEV_PORT" --timeout 900s \
  > "$SCRATCH-emu.log" 2>&1 &
EMU_PID=$!

set +e
wait "$HARNESS_PID"
RUN_EXIT=$?
cat "$SCRATCH-harness.log"

RESULT_LINE="$(grep 'BLE_OTA_HARNESS_RESULT' "$SCRATCH-harness.log" | tail -1 || true)"
echo "result: ${RESULT_LINE:-<none>}"
if [[ "$RUN_EXIT" -ne 0 ]] || ! grep -q 'BLE_OTA_HARNESS_RESULT status=PASS cases_run=3/3' "$SCRATCH-harness.log"; then
  echo "::error:: (ble-ota-emulator-harness) did not report PASS - see $SCRATCH-harness.log and $SCRATCH-emu.log"
  exit 1
fi

echo "== ble-ota-emulator-harness: PASS =="

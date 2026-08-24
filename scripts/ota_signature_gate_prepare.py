#!/usr/bin/env python3
"""Fixture-image preparation for the ota-signature-emulator-gate (Layer 1).

openspec/changes/add-ble-ota-emulator-harness/design.md D2/D3/D4.

Runs after `idf.py build` and `idf.py merge-bin` for the
firmware/ota_signature_gate project. Given the fixture's own signed app
binary (the D2 "valid" image) and the merged flash image `idf.py merge-bin`
produced:

  1. Generates a foreign P-256 signing key (fresh per invocation, never
     committed - it is written under --key-dir, which callers must point at
     a location `.gitignore`'s `*.pem` rule covers, e.g. an out-of-tree
     build directory).
  2. Re-signs the app's *payload* (everything before its own trailing
     4096-byte Secure Boot V2 signature sector - signing only appends that
     sector, so the payload bytes are identical whichever key signs them)
     with the foreign key, and asserts the result carries a structurally
     valid signature block (task 3.4) before extracting its trailing 4096
     bytes as the "foreign signature sector".
  3. Flips one byte inside a loaded segment's data and recomputes the base
     image format's checksum byte and appended SHA-256 so the result is
     internally consistent, keeping the *original* (now stale) trailing
     signature sector untouched (D3). `esp_image_verify()` runs
     process_image_header -> process_segments -> process_checksum ->
     process_appended_hash_and_sig -> verify_secure_boot_signature in that
     fixed order (esp_image_format.c:215-232); a naive byte flip fails at
     process_checksum and never reaches signature code at all, which would
     make the "tampered" case indistinguishable from a corrupt-image case.
     Repairing the checksum and hash after the flip means this image can
     only be rejected by the actual cryptographic check (task 3.2/3.4a) -
     confirmed here, not just asserted, by running `espsecure
     verify-signature` against the real signing key and requiring the
     specific failure to be a signature digest mismatch.
  4. Writes a 16-byte manifest (magic + image size) followed by the valid
     image, the foreign signature sector, and the tampered-and-repaired
     image into the merged flash image at the `fixtures` partition's offset
     (D4), splicing over the 0xFF-filled bytes `idf.py merge-bin
     --pad-to-size` leaves there.

No transfer protocol participates in producing any of this (spec:
ota-signature-emulator-gate - "Images are pre-staged in flash"); everything
here runs before the emulator boots.
"""
import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
from pathlib import Path

# Must match GATE_MANIFEST_* in
# firmware/ota_signature_gate/main/ota_signature_gate_main.c exactly.
MANIFEST_MAGIC = b"OTFX"
MANIFEST_SIZE = 16
SIG_SECTOR_SIZE = 4096

# D4: the fixtures partition's offset and size, from
# firmware/ota_signature_gate/partition_table.csv.
FIXTURES_PARTITION_OFFSET = 0x400000
FIXTURES_PARTITION_SIZE = 0x140000

# esp_app_format.h: esp_image_header_t is 24 bytes, esp_image_segment_
# header_t is 8 bytes. Offsets below are field positions within the header,
# not struct member names, so this stays a plain byte-offset parser rather
# than depending on any generated binding.
ESP_IMAGE_HEADER_MAGIC = 0xE9
ESP_IMAGE_HEADER_SIZE = 24
ESP_IMAGE_SEGMENT_HEADER_SIZE = 8
ESP_IMAGE_SEGMENT_COUNT_OFFSET = 1
ESP_IMAGE_HASH_APPENDED_OFFSET = 23
ESP_IMAGE_CHECKSUM_SEED = 0xEF
ESP_IMAGE_HASH_LEN = 32


def fail(message: str) -> "NoReturn":
    print(f"::error:: ota_signature_gate_prepare.py: {message}", file=sys.stderr)
    sys.exit(1)


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        fail(
            f"command failed (exit {proc.returncode}): {' '.join(cmd)}\n"
            f"--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}"
        )
    return proc


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        fail(f"required tool '{name}' not found on PATH - source the ESP-IDF export.sh first")
    return path


def build_manifest(image_size: int) -> bytes:
    manifest = MANIFEST_MAGIC + struct.pack("<I", image_size) + b"\x00" * 8
    assert len(manifest) == MANIFEST_SIZE
    return manifest


class ImageLayout:
    """Byte offsets `esp_image_format.c`'s process_* functions derive from
    an app image's header and segment table, needed to repair the checksum
    and appended hash after a tamper (D3). Computed once per image since
    every offset below the segment table depends on segment_count and each
    segment's declared data_len."""

    def __init__(self, image_bytes: bytes):
        if image_bytes[0] != ESP_IMAGE_HEADER_MAGIC:
            fail(f"fixture app image does not start with the ESP image magic byte (0x{ESP_IMAGE_HEADER_MAGIC:02x})")

        segment_count = image_bytes[ESP_IMAGE_SEGMENT_COUNT_OFFSET]
        self.hash_appended = image_bytes[ESP_IMAGE_HASH_APPENDED_OFFSET] != 0

        offset = ESP_IMAGE_HEADER_SIZE
        self.segments: list[tuple[int, int]] = []  # (data_start, data_len)
        for _ in range(segment_count):
            if offset + ESP_IMAGE_SEGMENT_HEADER_SIZE > len(image_bytes):
                fail("segment header runs past the end of the image while parsing the ESP image layout")
            _load_addr, data_len = struct.unpack_from("<II", image_bytes, offset)
            offset += ESP_IMAGE_SEGMENT_HEADER_SIZE
            self.segments.append((offset, data_len))
            offset += data_len

        # process_checksum (esp_image_format.c:1045-1058): the checksum byte
        # sits at the last byte of (image_len + 1) padded up to the next
        # 16-byte boundary; the appended hash, if present, immediately
        # follows that padded region.
        unpadded_length = offset
        next16 = ((unpadded_length + 1 + 15) // 16) * 16
        self.checksum_pos = next16 - 1
        self.hash_start = next16
        self.hash_end = next16 + ESP_IMAGE_HASH_LEN


def compute_full_checksum(image_bytes: bytes, segments: list[tuple[int, int]]) -> int:
    """Independent from the incremental XOR-difference repair below - used
    only as a self-check that the repair produced the same byte a from-
    scratch computation would, so a bug in the incremental math cannot pass
    silently. Byte-wise XOR-fold over every segment's data, seeded with
    ESP_ROM_CHECKSUM_INITIAL (esp_image_format.c:59); mathematically
    equivalent to the production algorithm's 32-bit-word XOR followed by a
    4-byte fold (process_segment_data/process_checksum), since XOR-fold is
    linear and order-independent."""
    checksum = ESP_IMAGE_CHECKSUM_SEED
    for data_start, data_len in segments:
        for b in image_bytes[data_start : data_start + data_len]:
            checksum ^= b
    return checksum & 0xFF


def pick_tamper_offset(segments: list[tuple[int, int]]) -> int:
    """D3: 'The flipped byte must land in a loaded segment's data, not in
    padding or in the signature sector.' Picks a byte well inside the first
    segment with enough data to have a safe margin from its edges, rather
    than depending on image_size / 2 happening to fall inside some segment
    (it need not)."""
    for data_start, data_len in segments:
        if data_len >= 128:
            return data_start + 64
    if not segments:
        fail("fixture app image has no loaded segments to tamper with")
    data_start, data_len = segments[0]
    return data_start + data_len // 2


def assert_tampered_repair_only_breaks_signature(tampered_repaired_image: bytes, signing_key: Path, key_dir: Path) -> None:
    """Task 3.4a: independent, tool-verified proof that the repaired image
    is rejected *only* by signature verification, not by anything
    esp_image_verify() checks first. `espsecure verify-signature` walks the
    same header/segment/checksum/appended-hash structure esp_image_verify()
    does to compute the image digest it checks the signature against; if
    this reported anything other than a digest mismatch against an
    otherwise well-formed signature block, the repair above would not
    actually isolate rejection to the signature check, and this case would
    not demonstrate what design.md D3 / spec "Rejection is not the image
    checksum in disguise" requires."""
    espsecure = require_tool("espsecure") or require_tool("espsecure.py")
    tmp = key_dir / "tampered_repaired_for_check.bin"
    tmp.write_bytes(tampered_repaired_image)

    proc = subprocess.run(
        [espsecure, "verify-signature", "--version", "2", "--keyfile", str(signing_key), str(tmp)],
        capture_output=True,
        text=True,
    )
    if proc.returncode == 0:
        fail(
            "tampered-and-repaired image unexpectedly verified successfully against the real signing key - "
            "the tamper did not actually change the signed content (task 3.4a self-check failed)"
        )
    combined = proc.stdout + proc.stderr
    if "does not match the actual image digest" not in combined:
        fail(
            "tampered-and-repaired image was rejected for a reason other than a signature digest mismatch - "
            f"its rejection is not attributable to signature verification alone (task 3.4a):\n{combined}"
        )
    print(
        "Tampered-and-repaired image self-check (task 3.4a): rejected only by a signature digest mismatch, "
        "not by checksum or appended-hash malformation - "
        f"{[line for line in combined.splitlines() if 'does not match' in line][0].strip()}"
    )


def generate_tampered_repaired_image(image_bytes: bytes, image_size: int, signing_key: Path, key_dir: Path) -> bytes:
    """Task 3.2 (design.md D3, corrected): flip one byte inside a loaded
    segment's data, then repair the base image format's checksum byte and
    appended SHA-256 so the image is internally consistent - keeping the
    original, now-stale, trailing signature sector untouched. Without the
    repair, `process_checksum` rejects the image before signature code ever
    runs (esp_image_format.c:215-232), which would make this case
    indistinguishable from a corrupt-image case rather than a signature
    one."""
    layout = ImageLayout(image_bytes)
    if not layout.hash_appended:
        fail(
            "fixture app image has hash_appended=0 in its header - the tampered case's repair needs an "
            "appended SHA-256 to recompute (D3); this build must produce one (CONFIG_SECURE_SIGNED_ON_UPDATE=y)"
        )

    tamper_offset = pick_tamper_offset(layout.segments)
    if tamper_offset >= layout.checksum_pos:
        fail(
            f"computed tamper offset ({tamper_offset}) does not land before the checksum byte "
            f"({layout.checksum_pos}) - segment layout assumption does not hold for this image"
        )

    modified = bytearray(image_bytes)
    old_byte = modified[tamper_offset]
    new_byte = old_byte ^ 0xFF
    modified[tamper_offset] = new_byte

    # Incremental repair: process_checksum's XOR-fold is linear, so flipping
    # one byte changes the final checksum byte by exactly the XOR of the old
    # and new byte values, regardless of what else feeds the checksum.
    old_checksum = image_bytes[layout.checksum_pos]
    new_checksum = old_checksum ^ old_byte ^ new_byte
    modified[layout.checksum_pos] = new_checksum

    # process_appended_hash_and_sig (esp_image_format.c:996-1025): the
    # appended hash covers header + segments + checksum byte + padding,
    # i.e. exactly bytes [0:hash_start) - not the hash itself or the
    # signature block that follows it.
    new_hash = hashlib.sha256(bytes(modified[: layout.hash_start])).digest()
    modified[layout.hash_start : layout.hash_end] = new_hash

    # Self-check (not part of the production algorithm): an independent,
    # from-scratch checksum recomputation must agree with the incremental
    # repair above, and the just-written hash must equal a fresh recompute
    # over the repaired bytes. Both are expected to hold by construction;
    # asserting them here turns a repair bug into a loud build failure
    # instead of a silent rejection under esp-emu (task 3.4a's evidence
    # depends on this repair being correct, not just attempted).
    recomputed_checksum = compute_full_checksum(bytes(modified), layout.segments)
    if recomputed_checksum != new_checksum:
        fail(
            f"checksum self-check failed: incremental repair produced 0x{new_checksum:02x}, "
            f"from-scratch recomputation produced 0x{recomputed_checksum:02x}"
        )
    recomputed_hash = hashlib.sha256(bytes(modified[: layout.hash_start])).digest()
    if recomputed_hash != bytes(modified[layout.hash_start : layout.hash_end]):
        fail("appended-hash self-check failed: recomputation does not match the embedded hash")

    print(
        f"Tampered-and-repaired image: flipped byte at offset {tamper_offset} "
        f"(0x{old_byte:02x} -> 0x{new_byte:02x}), checksum 0x{old_checksum:02x} -> 0x{new_checksum:02x} "
        f"at offset {layout.checksum_pos}, appended hash recomputed at offset {layout.hash_start} "
        "(original signature sector left unchanged, D3)."
    )

    assert_tampered_repair_only_breaks_signature(bytes(modified), signing_key, key_dir)

    return bytes(modified)


def generate_foreign_signature_sector(app_bin: Path, image_size: int, key_dir: Path) -> bytes:
    """Task 2.3: produce a 4096-byte signature sector signed with a key
    other than the one the fixture app itself was signed with, and (task
    3.4) confirm it is structurally well-formed before returning it - so
    its later rejection by the fixture demonstrates trust-anchor
    enforcement, not a parse failure."""
    key_dir.mkdir(parents=True, exist_ok=True)
    foreign_key = key_dir / "foreign_signing_key.pem"
    payload = key_dir / "foreign_payload.bin"
    foreign_signed = key_dir / "foreign_signed.bin"

    openssl = require_tool("openssl")
    espsecure = require_tool("espsecure") or require_tool("espsecure.py")

    # Fresh every run: this is a throwaway "wrong" key, not a stable secret,
    # and keeping it out of any cache avoids accidentally reusing state
    # between fixture rebuilds that changed image_size.
    run([openssl, "ecparam", "-genkey", "-name", "prime256v1", "-noout", "-out", str(foreign_key)])

    image_bytes = app_bin.read_bytes()
    if len(image_bytes) != image_size:
        fail(f"app_bin size ({len(image_bytes)}) does not match the size passed in (image_size={image_size})")
    if image_size % SIG_SECTOR_SIZE != 0:
        fail(
            f"fixture app image size ({image_size}) is not a multiple of {SIG_SECTOR_SIZE} - "
            "the chunk-aligned foreign-key substitution in the fixture app assumes it is (D3)"
        )

    # Signing only appends the trailing signature sector; the bytes before
    # it are the same whichever key produced the signature, so slicing them
    # off the *already-signed* fixture binary gives back the exact payload
    # espsecure originally signed.
    payload_bytes = image_bytes[: image_size - SIG_SECTOR_SIZE]
    payload.write_bytes(payload_bytes)

    run(
        [
            espsecure,
            "sign-data",
            "--version",
            "2",
            "--keyfile",
            str(foreign_key),
            "--output",
            str(foreign_signed),
            str(payload),
        ]
    )

    # Task 3.4: assert structural validity *before* the run, so the
    # fixture's rejection of this image demonstrates trust-anchor
    # enforcement rather than a parse failure.
    info = run([espsecure, "signature-info-v2", str(foreign_signed)])
    if "valid" not in info.stdout.lower():
        fail(
            "foreign-signed artifact did not report a valid signature block via "
            f"'espsecure signature-info-v2' (task 3.4) - output:\n{info.stdout}"
        )
    print(f"Foreign-signed artifact structural check: {info.stdout.strip()}")

    foreign_signed_bytes = foreign_signed.read_bytes()
    if len(foreign_signed_bytes) != image_size:
        fail(
            f"foreign-signed artifact size ({len(foreign_signed_bytes)}) does not match the "
            f"fixture image size ({image_size}) - sector padding assumption (D3) does not hold"
        )

    return foreign_signed_bytes[image_size - SIG_SECTOR_SIZE :]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-bin", required=True, type=Path, help="Fixture's own signed app .bin")
    parser.add_argument("--merged-bin", required=True, type=Path, help="Output of `idf.py merge-bin`, patched in place")
    parser.add_argument("--key-dir", required=True, type=Path, help="Scratch dir for the foreign key and intermediates (never committed)")
    parser.add_argument(
        "--signing-key",
        required=True,
        type=Path,
        help="The real key --app-bin was signed with (CONFIG_SECURE_BOOT_SIGNING_KEY) - used only to prove "
        "(task 3.4a) that the tampered-and-repaired image is rejected by a signature digest mismatch and "
        "nothing else; never written anywhere, never logged.",
    )
    args = parser.parse_args()

    if not args.app_bin.is_file():
        fail(f"--app-bin not found: {args.app_bin}")
    if not args.merged_bin.is_file():
        fail(f"--merged-bin not found: {args.merged_bin} (run `idf.py merge-bin` first)")
    if not args.signing_key.is_file():
        fail(f"--signing-key not found: {args.signing_key}")

    image_bytes = args.app_bin.read_bytes()
    image_size = len(image_bytes)
    if image_size % SIG_SECTOR_SIZE != 0:
        fail(
            f"fixture app image size ({image_size}) is not a multiple of {SIG_SECTOR_SIZE}; "
            "refusing to stage a fixture the on-target chunking logic cannot handle correctly (D3)"
        )

    foreign_sig_sector = generate_foreign_signature_sector(args.app_bin, image_size, args.key_dir)
    assert len(foreign_sig_sector) == SIG_SECTOR_SIZE

    tampered_repaired_image = generate_tampered_repaired_image(image_bytes, image_size, args.signing_key, args.key_dir)
    assert len(tampered_repaired_image) == image_size

    blob = build_manifest(image_size) + image_bytes + foreign_sig_sector + tampered_repaired_image
    if len(blob) > FIXTURES_PARTITION_SIZE:
        fail(
            f"fixture blob ({len(blob)} bytes: {MANIFEST_SIZE}-byte manifest + {image_size}-byte valid image + "
            f"{SIG_SECTOR_SIZE}-byte foreign sector + {image_size}-byte tampered-repaired image) exceeds the "
            f"fixtures partition size ({FIXTURES_PARTITION_SIZE} bytes, D4) - either the fixture app grew too "
            "large or the partition table needs a larger fixtures partition"
        )

    merged_size = args.merged_bin.stat().st_size
    end_offset = FIXTURES_PARTITION_OFFSET + len(blob)
    if merged_size < end_offset:
        fail(
            f"merged flash image ({merged_size} bytes) is too small to hold the fixture blob at "
            f"0x{FIXTURES_PARTITION_OFFSET:x} (needs at least {end_offset} bytes) - was "
            "`idf.py merge-bin` run with --pad-to-size covering the full flash?"
        )

    with args.merged_bin.open("r+b") as f:
        f.seek(FIXTURES_PARTITION_OFFSET)
        f.write(blob)

    print(
        f"Spliced {len(blob)} bytes (manifest={MANIFEST_SIZE}, valid={image_size}, "
        f"foreign_sig={SIG_SECTOR_SIZE}, tampered_repaired={image_size}) into {args.merged_bin} "
        f"at 0x{FIXTURES_PARTITION_OFFSET:x}"
    )


if __name__ == "__main__":
    main()

"""Derive the three Layer-2 test images from one signed fixture binary.

Reuses the exact derivation logic Layer 1's gate uses
(scripts/ota_signature_gate_prepare.py), so both layers exercise identical
artifacts (design.md D2/D3):

- valid:     the fixture's own signed binary, byte-for-byte (D2)
- tampered:  one byte flipped inside a loaded segment, checksum byte and
             appended SHA-256 repaired, original signature sector kept (D3)
- foreign:   same bytes, trailing 4096-byte signature sector substituted with
             one generated under a throwaway P-256 key (D3)
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path


def load_layer1_module(repo_root: Path):
    module_path = repo_root / "scripts" / "ota_signature_gate_prepare.py"
    spec = importlib.util.spec_from_file_location(
        "ota_signature_gate_prepare", module_path
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {module_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules["ota_signature_gate_prepare"] = module
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-bin", type=Path, required=True,
                        help="signed fixture application binary")
    parser.add_argument("--signing-key", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    layer1 = load_layer1_module(Path(__file__).resolve().parents[2])
    args.out_dir.mkdir(parents=True, exist_ok=True)
    key_dir = args.out_dir / "keys"
    # Layer 1's prepare script assumes its caller pre-created this directory.
    key_dir.mkdir(parents=True, exist_ok=True)

    image_bytes = args.app_bin.read_bytes()
    image_size = len(image_bytes)

    valid_path = args.out_dir / "valid.bin"
    valid_path.write_bytes(image_bytes)

    tampered = layer1.generate_tampered_repaired_image(
        image_bytes, image_size, args.signing_key, key_dir
    )
    tampered_path = args.out_dir / "tampered.bin"
    tampered_path.write_bytes(tampered)

    sector = layer1.generate_foreign_signature_sector(args.app_bin, image_size, key_dir)
    if len(image_bytes) < len(sector):
        raise RuntimeError("image smaller than one signature sector")
    foreign = bytearray(image_bytes)
    foreign[-len(sector):] = sector
    foreign_path = args.out_dir / "foreign.bin"
    foreign_path.write_bytes(bytes(foreign))

    print(f"images written: {valid_path} {tampered_path} {foreign_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

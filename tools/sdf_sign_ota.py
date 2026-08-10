#!/usr/bin/env python3
"""
SDF OTA Image Signing Tool

Signs/verifies firmware images for OTA updates using ECDSA P-256 (secp256r1).

The signature covers the SHA-256 digest of the image bytes, not the raw bytes,
so the device can verify it from a digest accumulated incrementally during the
transfer instead of holding the whole image in RAM. It is appended raw as
r||s (two 32-byte big-endian integers) rather than ASN.1 DER, keeping the
footer a fixed 68 bytes that the device knows before the transfer starts.

Dependencies:
    pip install cryptography

Usage:
    # Generate keypair (one-time)
    openssl ecparam -genkey -name prime256v1 -noout -out ota_private.key
    openssl ec -in ota_private.key -pubout -out ota_public.key

    # Extract uncompressed public point for embedding in firmware (65 bytes)
    python3 sdf_sign_ota.py extract-pubkey --key ota_private.key --output ota_pubkey.bin

    # Sign firmware
    python3 sdf_sign_ota.py sign --input sdf.bin --key ota_private.key --output sdf_signed.bin

    # Verify signed firmware
    python3 sdf_sign_ota.py verify --input sdf_signed.bin --pubkey ota_public.key
"""

import argparse
import hashlib
import sys

try:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import (
        Prehashed,
        decode_dss_signature,
        encode_dss_signature,
    )
except ImportError:
    print("ERROR: cryptography library required. Install with: pip install cryptography", file=sys.stderr)
    sys.exit(1)

MAGIC = b"SDF\x01"
SIG_SIZE = 64
MAGIC_SIZE = 4
FOOTER_SIZE = SIG_SIZE + MAGIC_SIZE
# Uncompressed EC point: 0x04 || X || Y. The firmware reads exactly this many
# bytes; compressed encoding is not used because point decompression is not
# reliably compiled into ESP-IDF's mbedTLS.
PUBKEY_SIZE = 65


def _require_p256_curve(key):
    if not isinstance(key.curve, ec.SECP256R1):
        raise ValueError(
            f"Key uses curve {key.curve.name}, but OTA signing requires P-256 (secp256r1). "
            "Regenerate with: openssl ecparam -genkey -name prime256v1 -noout -out ota_private.key"
        )


def load_private_key(key_path):
    """Load an ECDSA P-256 private key from a PEM file."""
    with open(key_path, "rb") as f:
        key_data = f.read()
    private_key = serialization.load_pem_private_key(key_data, password=None)
    if not isinstance(private_key, ec.EllipticCurvePrivateKey):
        raise ValueError("Key is not an EC private key; OTA signing requires ECDSA P-256")
    _require_p256_curve(private_key)
    return private_key


def load_public_key(key_path):
    """Load an ECDSA P-256 public key from a PEM file or a raw 65-byte point."""
    with open(key_path, "rb") as f:
        key_data = f.read()
    if len(key_data) == PUBKEY_SIZE and key_data[0] == 0x04:
        # Raw uncompressed point, i.e. the same blob embedded in firmware.
        public_key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), key_data)
    else:
        public_key = serialization.load_pem_public_key(key_data)
    if not isinstance(public_key, ec.EllipticCurvePublicKey):
        raise ValueError("Key is not an EC public key; OTA verification requires ECDSA P-256")
    _require_p256_curve(public_key)
    return public_key


def _raw_signature(der_signature):
    """Convert a DER-encoded ECDSA signature to raw r||s (2 x 32 big-endian)."""
    r, s = decode_dss_signature(der_signature)
    return r.to_bytes(SIG_SIZE // 2, "big") + s.to_bytes(SIG_SIZE // 2, "big")


def _der_signature(raw_signature):
    """Convert a raw r||s ECDSA signature back to DER for verification."""
    half = SIG_SIZE // 2
    r = int.from_bytes(raw_signature[:half], "big")
    s = int.from_bytes(raw_signature[half:], "big")
    return encode_dss_signature(r, s)


def sign_image(input_path, output_path, private_key_path):
    """Sign a firmware image and append the signature + magic marker."""
    private_key = load_private_key(private_key_path)

    with open(input_path, "rb") as f:
        image_data = f.read()

    # sign() hashes internally; the digest is computed here too so it can be
    # reported, and so the tool documents exactly what the device verifies.
    digest = hashlib.sha256(image_data).digest()
    signature = _raw_signature(private_key.sign(image_data, ec.ECDSA(hashes.SHA256())))
    if len(signature) != SIG_SIZE:
        raise ValueError(f"Expected signature size {SIG_SIZE}, got {len(signature)}")

    # Write output: image + signature + magic
    with open(output_path, "wb") as f:
        f.write(image_data)
        f.write(signature)
        f.write(MAGIC)

    print(f"Signed {input_path} -> {output_path}")
    print(f"  Image size: {len(image_data)} bytes")
    print(f"  SHA-256: {digest.hex()}")
    print(f"  Signature: {len(signature)} bytes (raw r||s, ECDSA P-256)")
    print(f"  Magic: {MAGIC.hex()}")
    print(f"  Total: {len(image_data) + FOOTER_SIZE} bytes")


def verify_image(input_path, public_key_path):
    """Verify a signed firmware image, mirroring the device's procedure."""
    public_key = load_public_key(public_key_path)

    with open(input_path, "rb") as f:
        data = f.read()

    if len(data) <= FOOTER_SIZE:
        print(f"FAIL: File too small ({len(data)} <= {FOOTER_SIZE})")
        return False

    # Extract footer
    image_data = data[:-FOOTER_SIZE]
    signature = data[-FOOTER_SIZE:-MAGIC_SIZE]
    magic = data[-MAGIC_SIZE:]

    if magic != MAGIC:
        print(f"FAIL: Magic marker not found (expected {MAGIC.hex()}, got {magic.hex()})")
        return False

    # The device verifies a digest it accumulated during transfer, never the
    # message, so hash first and verify with Prehashed to match it exactly.
    digest = hashlib.sha256(image_data).digest()
    try:
        public_key.verify(
            _der_signature(signature),
            digest,
            ec.ECDSA(Prehashed(hashes.SHA256())),
        )
    except InvalidSignature:
        print("FAIL: Signature verification failed")
        return False
    except Exception as e:
        print(f"FAIL: Signature verification failed: {e}")
        return False

    print("OK: Signature verified")
    print(f"  Image size: {len(image_data)} bytes")
    print(f"  SHA-256: {digest.hex()}")
    return True


def extract_public_key(private_key_path, output_path):
    """Extract the uncompressed public point from a private key."""
    private_key = load_private_key(private_key_path)
    public_key = private_key.public_key()

    # Save as the raw 65-byte uncompressed point the firmware embeds
    raw_pub = public_key.public_bytes(
        encoding=serialization.Encoding.X962,
        format=serialization.PublicFormat.UncompressedPoint,
    )
    if len(raw_pub) != PUBKEY_SIZE:
        raise ValueError(f"Expected {PUBKEY_SIZE}-byte uncompressed point, got {len(raw_pub)}")
    with open(output_path, "wb") as f:
        f.write(raw_pub)
    print(f"Public key extracted to {output_path} ({len(raw_pub)} bytes, uncompressed)")

    # Also save as C array
    c_array = ", ".join(f"0x{b:02x}" for b in raw_pub)
    print(f"C array: {{ {c_array} }}")

    # Save as PEM too
    pem = public_key.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    pem_path = output_path + ".pem"
    with open(pem_path, "wb") as f:
        f.write(pem)
    print(f"PEM saved to {pem_path}")


def main():
    parser = argparse.ArgumentParser(description="SDF OTA Image Signing Tool (ECDSA P-256)")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Sign command
    sign_parser = subparsers.add_parser("sign", help="Sign firmware image")
    sign_parser.add_argument("--input", required=True, help="Input firmware binary")
    sign_parser.add_argument("--key", required=True, help="P-256 private key (PEM)")
    sign_parser.add_argument("--output", required=True, help="Output signed binary")

    # Verify command
    verify_parser = subparsers.add_parser("verify", help="Verify signed firmware image")
    verify_parser.add_argument("--input", required=True, help="Signed firmware binary")
    verify_parser.add_argument("--pubkey", required=True,
                               help="P-256 public key (PEM or raw 65-byte uncompressed point)")

    # Extract pubkey command
    pubkey_parser = subparsers.add_parser("extract-pubkey", help="Extract public key from private key")
    pubkey_parser.add_argument("--key", required=True, help="P-256 private key (PEM)")
    pubkey_parser.add_argument("--output", required=True,
                               help="Output public key file (raw 65-byte uncompressed point)")

    args = parser.parse_args()

    try:
        if args.command == "sign":
            sign_image(args.input, args.output, args.key)
        elif args.command == "verify":
            ok = verify_image(args.input, args.pubkey)
            sys.exit(0 if ok else 1)
        elif args.command == "extract-pubkey":
            extract_public_key(args.key, args.output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
SDF OTA Image Signing Tool

Signs/verifies firmware images for OTA updates using Ed25519 signatures.

Dependencies:
    pip install cryptography

Usage:
    # Generate keypair (one-time)
    openssl genpkey -algorithm ed25519 -out ota_private.key
    openssl pkey -in ota_private.key -pubout -out ota_public.key

    # Extract raw public key for embedding in firmware
    python3 sdf_sign_ota.py extract-pubkey --key ota_private.key --output ota_pubkey.bin

    # Sign firmware
    python3 sdf_sign_ota.py sign --input sdf.bin --key ota_private.key --output sdf_signed.bin

    # Verify signed firmware
    python3 sdf_sign_ota.py verify --input sdf_signed.bin --pubkey ota_public.key
"""

import sys
import argparse

try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ed25519
except ImportError:
    print("ERROR: cryptography library required. Install with: pip install cryptography", file=sys.stderr)
    sys.exit(1)

import argparse
import hashlib
import sys
import os

try:
    from cryptography.hazmat.primitives.asymmetric import ed25519
    from cryptography.hazmat.primitives import serialization
except ImportError:
    print("ERROR: cryptography library required. Install with: pip install cryptography")
    sys.exit(1)

MAGIC = b"SDF\x01"
SIG_SIZE = 64
MAGIC_SIZE = 4
FOOTER_SIZE = SIG_SIZE + MAGIC_SIZE


def load_private_key(key_path):
    """Load Ed25519 private key from PEM file."""
    with open(key_path, "rb") as f:
        key_data = f.read()
    try:
        private_key = serialization.load_pem_private_key(key_data, password=None)
    except ValueError:
        # Try raw key
        if len(key_data) == 32:
            private_key = ed25519.Ed25519PrivateKey.from_private_bytes(key_data)
        else:
            raise
    if not isinstance(private_key, ed25519.Ed25519PrivateKey):
        raise ValueError("Key is not Ed25519")
    return private_key


def load_public_key(key_path):
    """Load Ed25519 public key from PEM or raw file."""
    with open(key_path, "rb") as f:
        key_data = f.read()
    try:
        public_key = serialization.load_pem_public_key(key_data)
    except ValueError:
        # Try raw key
        if len(key_data) == 32:
            public_key = ed25519.Ed25519PublicKey.from_public_bytes(key_data)
        else:
            raise
    if not isinstance(public_key, ed25519.Ed25519PublicKey):
        raise ValueError("Key is not Ed25519")
    return public_key


def sign_image(input_path, output_path, private_key_path):
    """Sign firmware image and append signature + magic."""
    private_key = load_private_key(private_key_path)

    with open(input_path, "rb") as f:
        image_data = f.read()

    # Sign the image data
    signature = private_key.sign(image_data)
    if len(signature) != SIG_SIZE:
        raise ValueError(f"Expected signature size {SIG_SIZE}, got {len(signature)}")

    # Write output: image + signature + magic
    with open(output_path, "wb") as f:
        f.write(image_data)
        f.write(signature)
        f.write(MAGIC)

    print(f"Signed {input_path} -> {output_path}")
    print(f"  Image size: {len(image_data)} bytes")
    print(f"  Signature: {len(signature)} bytes")
    print(f"  Magic: {MAGIC.hex()}")
    print(f"  Total: {len(image_data) + FOOTER_SIZE} bytes")


def verify_image(input_path, public_key_path):
    """Verify signed firmware image."""
    public_key = load_public_key(public_key_path)

    with open(input_path, "rb") as f:
        data = f.read()

    if len(data) < FOOTER_SIZE:
        print(f"FAIL: File too small ({len(data)} < {FOOTER_SIZE})")
        return False

    # Extract footer
    image_data = data[:-FOOTER_SIZE]
    signature = data[-FOOTER_SIZE:-MAGIC_SIZE]
    magic = data[-MAGIC_SIZE:]

    if magic != MAGIC:
        print(f"FAIL: Magic marker not found (expected {MAGIC.hex()}, got {magic.hex()})")
        return False

    try:
        public_key.verify(signature, image_data)
        print(f"OK: Signature verified")
        print(f"  Image size: {len(image_data)} bytes")
        return True
    except Exception as e:
        print(f"FAIL: Signature verification failed: {e}")
        return False


def extract_public_key(private_key_path, output_path):
    """Extract public key from private key."""
    private_key = load_private_key(private_key_path)
    public_key = private_key.public_key()
    
    # Save as raw 32 bytes
    raw_pub = public_key.public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw
    )
    with open(output_path, "wb") as f:
        f.write(raw_pub)
    print(f"Public key extracted to {output_path} ({len(raw_pub)} bytes)")
    
    # Also save as C array
    c_array = ", ".join(f"0x{b:02x}" for b in raw_pub)
    print(f"C array: {{ {c_array} }}")
    
    # Save as PEM too
    pem = public_key.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )
    pem_path = output_path + ".pem"
    with open(pem_path, "wb") as f:
        f.write(pem)
    print(f"PEM saved to {pem_path}")


def main():
    parser = argparse.ArgumentParser(description="SDF OTA Image Signing Tool")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Sign command
    sign_parser = subparsers.add_parser("sign", help="Sign firmware image")
    sign_parser.add_argument("--input", required=True, help="Input firmware binary")
    sign_parser.add_argument("--key", required=True, help="Private key (PEM or raw 32 bytes)")
    sign_parser.add_argument("--output", required=True, help="Output signed binary")

    # Verify command
    verify_parser = subparsers.add_parser("verify", help="Verify signed firmware image")
    verify_parser.add_argument("--input", required=True, help="Signed firmware binary")
    verify_parser.add_argument("--pubkey", required=True, help="Public key (PEM or raw 32 bytes)")

    # Extract pubkey command
    pubkey_parser = subparsers.add_parser("extract-pubkey", help="Extract public key from private key")
    pubkey_parser.add_argument("--key", required=True, help="Private key (PEM or raw 32 bytes)")
    pubkey_parser.add_argument("--output", required=True, help="Output public key file (raw 32 bytes)")

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
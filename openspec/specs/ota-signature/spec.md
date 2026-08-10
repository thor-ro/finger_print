# OTA Signature

## Purpose
Specifies how OTA images are signed and verified, so that a device only commits firmware produced by the holder of the project's OTA signing key.

## Requirements

### Requirement: Signature Algorithm
The system SHALL verify OTA images using ECDSA over the NIST P-256 curve (`secp256r1`). The system SHALL NOT depend on any signature implementation not already present in the ESP-IDF-bundled mbedTLS.

#### Scenario: Verification uses already-linked primitives
- **WHEN** firmware is built with `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y`
- **THEN** the build succeeds using only `CONFIG_MBEDTLS_ECDSA_C` and `CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED` primitives
- **AND** no additional third-party cryptographic library is vendored into the firmware

### Requirement: Signed Payload Is a SHA-256 Digest
The signature SHALL be computed over the SHA-256 digest of the image bytes, where the image bytes are the transferred image excluding its 68-byte footer. Verification SHALL NOT require the complete image to be resident in memory, and SHALL NOT impose any maximum verifiable image size.

#### Scenario: Image larger than available RAM verifies
- **WHEN** an OTA image whose size exceeds the device's available heap is transferred and its signature is valid
- **THEN** verification succeeds and the OTA proceeds to commit
- **AND** no allocation proportional to the image size is performed

#### Scenario: Digest covers image bytes only
- **WHEN** a signed image of `N` total bytes is transferred
- **THEN** the digest is computed over exactly the first `N - 68` bytes
- **AND** the 64-byte signature and 4-byte magic marker are excluded from the digest

### Requirement: Streaming Digest Computation
The system SHALL compute the image digest incrementally as bytes are written during the OTA transfer, rather than by reading the written image back from flash. Each transferred byte within the signed range SHALL be incorporated into the digest exactly once, in transfer order.

#### Scenario: Digest accumulates across chunked writes
- **WHEN** an image is transferred as a sequence of chunks of varying sizes
- **THEN** the resulting digest equals the SHA-256 digest of the concatenated signed range
- **AND** the digest is independent of how the image was split into chunks

#### Scenario: Final chunk containing footer is clamped
- **WHEN** a chunk contains bytes that extend into the 68-byte footer
- **THEN** only the portion of that chunk within the signed range is incorporated into the digest

#### Scenario: Transfer resumed after disconnect
- **WHEN** a transfer is resumed and the client continues from the device's confirmed `bytes_written` offset
- **THEN** no byte is incorporated into the digest more than once
- **AND** the resulting digest matches that of an uninterrupted transfer of the same image

### Requirement: Signed Image Format
A signed image SHALL consist of the image bytes followed by a 68-byte footer: a 64-byte ECDSA P-256 signature encoded as raw `r‖s` (two 32-byte big-endian integers), followed by the 4-byte magic marker `SDF\x01`. The signature SHALL NOT be ASN.1 DER encoded, so that the footer length is a compile-time constant known before a transfer begins.

#### Scenario: Well-formed signed image accepted
- **WHEN** an image carries a valid 64-byte raw `r‖s` signature and the `SDF\x01` magic marker
- **THEN** the magic marker is recognized and the signature is verified against the image digest

#### Scenario: Missing signature rejected
- **WHEN** an OTA image has no `SDF\x01` magic marker at the expected footer offset
- **THEN** the OTA session is aborted and the image is not committed

#### Scenario: Invalid signature rejected
- **WHEN** an OTA image's signature does not verify against the embedded public key
- **THEN** the OTA session is aborted, an `OTA_SIGNATURE_INVALID` audit event is emitted, and the image is not committed

### Requirement: Embedded Public Key
The system SHALL embed the ECDSA P-256 public key in firmware at build time as a 65-byte uncompressed EC point (`0x04 || X || Y`), stored in a read-only section. Compressed point encoding SHALL NOT be used.

#### Scenario: Key embedded in firmware
- **WHEN** firmware is built with signature verification enabled
- **THEN** the 65-byte uncompressed public key is present in a read-only section and readable by `sdf_ota` at runtime

### Requirement: Verification Occurs In-App Before Commit
The bootloader SHALL NOT verify the OTA signature. Verification SHALL occur in the application before `esp_ota_end()`, and its result SHALL determine whether the image is committed. Verification SHALL NOT depend on reading the target partition while the `esp_ota_handle_t` is open: the system SHALL perform no read of the target partition between `esp_ota_begin()` and `esp_ota_end()`.

#### Scenario: Verification precedes commit
- **WHEN** an OTA transfer completes
- **THEN** signature verification runs before `esp_ota_end()` is called
- **AND** the image is committed only if verification passes

#### Scenario: Verification does not read back the target partition
- **WHEN** signature verification and the version check run for an OTA session
- **THEN** every byte they consume originates from the transfer stream, not from a read of the target partition
- **AND** no partition read occurs between `esp_ota_begin()` and `esp_ota_end()`

#### Scenario: Verification succeeds with flash encryption enabled
- **WHEN** firmware is built with `CONFIG_SECURE_FLASH_ENC_ENABLED=y` and a validly signed image is transferred
- **THEN** the magic marker is recognized and the signature verifies
- **AND** the outcome is identical to the same transfer with flash encryption disabled

#### Scenario: Bad image rejected before the target partition is finalized
- **WHEN** an image with an invalid or missing signature is transferred
- **THEN** the session is aborted without `esp_ota_end()` being called
- **AND** no image finalization or staging-to-final copy is performed for that image

### Requirement: Signature Footer Captured From Transfer Stream
The system SHALL capture the 68-byte signature footer from the bytes as they are transferred, using the window `[expected_size - 68, expected_size)` fixed at session start, rather than reading it back from the target partition. Verification SHALL use the captured footer.

#### Scenario: Footer captured across chunk boundaries
- **WHEN** the 68-byte footer window spans two or more transfer chunks
- **THEN** the captured footer equals the last 68 bytes of the transferred image
- **AND** the result is independent of how the transfer was split into chunks

#### Scenario: Footer capture unaffected by deferred flash writes
- **WHEN** the underlying OTA write layer defers trailing bytes and flushes them only at `esp_ota_end()`
- **THEN** the captured footer is complete and correct at the time verification runs

#### Scenario: Transfer resumed after disconnect
- **WHEN** a transfer is resumed and the client continues from the device's confirmed `bytes_written` offset
- **THEN** the captured footer matches that of an uninterrupted transfer of the same image

### Requirement: Application Descriptor Captured From Transfer Stream
The system SHALL capture the incoming image's 256-byte `esp_app_desc_t` from the transfer stream, using the window `[32, 288)`, and SHALL perform the version and downgrade check against the captured copy rather than against a read of the target partition.

#### Scenario: Version check uses the captured descriptor
- **WHEN** the version comparison and downgrade check run during commit
- **THEN** the incoming version is read from the captured descriptor
- **AND** no call is made that reads the descriptor from the target partition

#### Scenario: Malformed descriptor rejected
- **WHEN** the captured descriptor's magic word does not match `ESP_APP_DESC_MAGIC_WORD`
- **THEN** the commit is rejected and the image is not committed

#### Scenario: Image too small to carry a descriptor rejected at session start
- **WHEN** `sdf_ota_begin()` is called with an image size smaller than 356 bytes
- **THEN** the session is refused with an error naming the minimum size
- **AND** no OTA handle is opened

### Requirement: Window Capture Is Independently Testable
The system SHALL expose the transfer-window capture logic as a function that depends on no partition, flash, or session state, so it can be built and exercised on `IDF_TARGET=linux`.

#### Scenario: Chunk-split behavior verified on host
- **WHEN** the host test runner captures a window from a byte stream split at every possible boundary
- **THEN** every split produces a byte-identical captured window
- **AND** chunks entirely outside the window leave the destination unmodified

### Requirement: Verification Core Is Independently Testable
The system SHALL expose a signature verification function that takes a digest, a signature, and a public key as inputs and depends on no partition or flash APIs, so it can be built and exercised on `IDF_TARGET=linux`.

#### Scenario: Known-answer vectors verify on host
- **WHEN** the host test runner executes the verification core against NIST CAVP P-256 known-answer vectors
- **THEN** valid vectors return success and invalid vectors return a signature failure
- **AND** the test exercises real cryptographic verification rather than a disabled no-op path

### Requirement: Verification Enabled By Default
`CONFIG_SDF_OTA_SIGNATURE_VERIFY` SHALL default to `y`, so that the verification path is compiled by every build and continuous integration run.

#### Scenario: Default build compiles the real path
- **WHEN** firmware is built with no explicit override of `CONFIG_SDF_OTA_SIGNATURE_VERIFY`
- **THEN** the ECDSA P-256 verification code is compiled and linked, not the no-op stub

#### Scenario: Unsigned image rejected under default configuration
- **WHEN** an unsigned image is transferred to a device built with default configuration
- **THEN** the OTA session is aborted and the image is not committed

### Requirement: Signing Tool
The build system SHALL provide `tools/sdf_sign_ota.py`, which signs a built binary by appending a 64-byte raw `r‖s` ECDSA P-256 signature over the image's SHA-256 digest plus the 4-byte magic marker `SDF\x01`, and which can verify a signed binary using the same procedure the device performs.

#### Scenario: Sign a release binary
- **WHEN** running `sdf_sign_ota.py sign --input sdf.bin --key ota_private.key --output sdf_signed.bin`
- **THEN** the output is the original binary followed by a 64-byte signature and the 4-byte magic marker
- **AND** the output is verifiable by firmware carrying the corresponding public key

#### Scenario: Verify a signed binary
- **WHEN** running `sdf_sign_ota.py verify --input sdf_signed.bin --pubkey ota_public.key`
- **THEN** the command exits successfully if the signature matches the image digest, and non-zero otherwise

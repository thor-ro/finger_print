## MODIFIED Requirements

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

## ADDED Requirements

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

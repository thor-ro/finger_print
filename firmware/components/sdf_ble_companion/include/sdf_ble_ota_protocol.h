#ifndef SDF_BLE_OTA_PROTOCOL_H
#define SDF_BLE_OTA_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pure, hardware-independent validation/decision logic for the BLE OTA
 * chunked-transfer wire format: writes to the OTA characteristic are
 * `[opcode:1][payload...]`. Kept free of NimBLE/sdf_ota calls so it can be
 * exercised by host (linux target) unit tests without the BLE stack - see
 * openspec/changes/replace-wifi-ota-with-ble-transfer/design.md
 * ("Decisions" -> Framing) for the full protocol rationale. */

#define SDF_BLE_OTA_OPCODE_BEGIN 0x01
#define SDF_BLE_OTA_OPCODE_CHUNK 0x02
#define SDF_BLE_OTA_OPCODE_END 0x03

#define SDF_BLE_OTA_BEGIN_PAYLOAD_LEN 4 /* little-endian uint32 image size */
/* ATT write overhead (3 bytes) + 1 opcode byte, subtracted from the
 * negotiated ATT MTU to get the max chunk payload length. */
#define SDF_BLE_OTA_MTU_OVERHEAD 4

/* Idle timeout for an in-progress BLE OTA session: 3x the old WiFi-connect
 * timeout, since this instead bounds the gap between chunks of a transfer
 * that can legitimately span minutes over BLE and must tolerate a brief
 * phone-side reconnect/re-auth cycle, not the whole transfer duration. */
#define SDF_BLE_OTA_IDLE_TIMEOUT_MS 60000

/* Validate and decode a BEGIN message payload (opcode already stripped).
 * Returns false for a wrong-length or zero-size BEGIN, in which case
 * *image_size_out is left unmodified. */
bool sdf_ble_ota_protocol_parse_begin(const uint8_t *payload, size_t payload_len,
                                       uint32_t *image_size_out);

/* Validate an END message payload (opcode already stripped) - must be
 * empty. */
bool sdf_ble_ota_protocol_validate_end(size_t payload_len);

/* Compute the maximum CHUNK payload length for a given negotiated ATT MTU.
 * Returns 0 if the MTU is too small to carry any payload. */
size_t sdf_ble_ota_protocol_max_chunk_len(uint16_t att_mtu);

/* Validate a CHUNK message payload (opcode already stripped) against the
 * negotiated ATT MTU: non-empty and not exceeding
 * sdf_ble_ota_protocol_max_chunk_len(att_mtu). */
bool sdf_ble_ota_protocol_validate_chunk(uint16_t att_mtu, size_t payload_len);

typedef enum {
    /* Malformed BEGIN, or a size mismatch against an already-open session -
     * caller must reject the write without disturbing any open session. */
    SDF_BLE_OTA_BEGIN_REJECT = 0,
    /* No session currently open - start a new one via sdf_ota_begin(). */
    SDF_BLE_OTA_BEGIN_START_NEW,
    /* A session with a matching declared size is already open - resume it
     * (no new sdf_ota_begin() call) and report the current offset. */
    SDF_BLE_OTA_BEGIN_RESUME,
} sdf_ble_ota_begin_decision_t;

/* Decide how to handle a validated BEGIN (already-parsed image_size)
 * against the current session state. */
sdf_ble_ota_begin_decision_t sdf_ble_ota_protocol_decide_begin(bool session_active,
                                                                 uint32_t session_declared_size,
                                                                 uint32_t requested_image_size);

#ifdef __cplusplus
}
#endif

#endif /* SDF_BLE_OTA_PROTOCOL_H */

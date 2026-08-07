#include "sdf_ble_ota_protocol.h"

#include <string.h>

bool sdf_ble_ota_protocol_parse_begin(const uint8_t *payload, size_t payload_len,
                                       uint32_t *image_size_out) {
    if (payload == NULL || image_size_out == NULL ||
        payload_len != SDF_BLE_OTA_BEGIN_PAYLOAD_LEN) {
        return false;
    }

    uint32_t image_size;
    /* On-wire is little-endian; ESP32 targets (and the linux host test
     * target) are little-endian, so a direct memcpy is correct without an
     * explicit byte-swap. */
    memcpy(&image_size, payload, sizeof(image_size));
    if (image_size == 0) {
        return false;
    }

    *image_size_out = image_size;
    return true;
}

bool sdf_ble_ota_protocol_validate_end(size_t payload_len) {
    return payload_len == 0;
}

size_t sdf_ble_ota_protocol_max_chunk_len(uint16_t att_mtu) {
    if (att_mtu <= SDF_BLE_OTA_MTU_OVERHEAD) {
        return 0;
    }
    return (size_t)att_mtu - SDF_BLE_OTA_MTU_OVERHEAD;
}

bool sdf_ble_ota_protocol_validate_chunk(uint16_t att_mtu, size_t payload_len) {
    if (payload_len == 0) {
        return false;
    }
    size_t max_len = sdf_ble_ota_protocol_max_chunk_len(att_mtu);
    return max_len != 0 && payload_len <= max_len;
}

sdf_ble_ota_begin_decision_t sdf_ble_ota_protocol_decide_begin(bool session_active,
                                                                 uint32_t session_declared_size,
                                                                 uint32_t requested_image_size) {
    if (!session_active) {
        return SDF_BLE_OTA_BEGIN_START_NEW;
    }
    return (requested_image_size == session_declared_size) ? SDF_BLE_OTA_BEGIN_RESUME
                                                             : SDF_BLE_OTA_BEGIN_REJECT;
}

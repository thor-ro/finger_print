/**
 * @file test_sdf_ble_ota_protocol.c
 * @brief Unit tests for the pure BLE OTA wire-format validation/decision
 * logic in sdf_ble_ota_protocol.c.
 */
#include "sdf_ble_ota_protocol.h"
#include "unity.h"
#include <string.h>

/* ---------- sdf_ble_ota_protocol_parse_begin ---------- */

void test_ble_ota_protocol_parse_begin_valid(void) {
    uint8_t payload[4] = {0x00, 0x10, 0x00, 0x00}; /* 0x1000 = 4096, LE */
    uint32_t image_size = 0;
    TEST_ASSERT_TRUE(sdf_ble_ota_protocol_parse_begin(payload, sizeof(payload), &image_size));
    TEST_ASSERT_EQUAL_UINT32(0x1000, image_size);
}

void test_ble_ota_protocol_parse_begin_wrong_length(void) {
    uint8_t payload[3] = {0x01, 0x02, 0x03};
    uint32_t image_size = 0xAAAAAAAA;
    TEST_ASSERT_FALSE(sdf_ble_ota_protocol_parse_begin(payload, sizeof(payload), &image_size));
    /* Output left unmodified on rejection. */
    TEST_ASSERT_EQUAL_UINT32(0xAAAAAAAA, image_size);
}

void test_ble_ota_protocol_parse_begin_zero_size(void) {
    uint8_t payload[4] = {0x00, 0x00, 0x00, 0x00};
    uint32_t image_size = 0;
    TEST_ASSERT_FALSE(sdf_ble_ota_protocol_parse_begin(payload, sizeof(payload), &image_size));
}

void test_ble_ota_protocol_parse_begin_null_args(void) {
    uint8_t payload[4] = {0x01, 0x00, 0x00, 0x00};
    uint32_t image_size = 0;
    TEST_ASSERT_FALSE(sdf_ble_ota_protocol_parse_begin(NULL, sizeof(payload), &image_size));
    TEST_ASSERT_FALSE(sdf_ble_ota_protocol_parse_begin(payload, sizeof(payload), NULL));
}

/* ---------- sdf_ble_ota_protocol_validate_end ---------- */

void test_ble_ota_protocol_validate_end_empty_accepted(void) {
    TEST_ASSERT_TRUE(sdf_ble_ota_protocol_validate_end(0));
}

void test_ble_ota_protocol_validate_end_nonempty_rejected(void) {
    TEST_ASSERT_FALSE(sdf_ble_ota_protocol_validate_end(1));
    TEST_ASSERT_FALSE(sdf_ble_ota_protocol_validate_end(16));
}

/* ---------- sdf_ble_ota_protocol_max_chunk_len / validate_chunk ---------- */

void test_ble_ota_protocol_max_chunk_len_typical_mtu(void) {
    /* Default ATT MTU is 23; overhead is 4 -> 19 bytes usable. */
    TEST_ASSERT_EQUAL_UINT32(19, sdf_ble_ota_protocol_max_chunk_len(23));
}

void test_ble_ota_protocol_max_chunk_len_too_small_mtu(void) {
    TEST_ASSERT_EQUAL_UINT32(0, sdf_ble_ota_protocol_max_chunk_len(4));
    TEST_ASSERT_EQUAL_UINT32(0, sdf_ble_ota_protocol_max_chunk_len(1));
}

void test_ble_ota_protocol_validate_chunk_at_max_accepted(void) {
    TEST_ASSERT_TRUE(sdf_ble_ota_protocol_validate_chunk(23, 19));
}

void test_ble_ota_protocol_validate_chunk_over_max_rejected(void) {
    TEST_ASSERT_FALSE(sdf_ble_ota_protocol_validate_chunk(23, 20));
}

void test_ble_ota_protocol_validate_chunk_empty_rejected(void) {
    TEST_ASSERT_FALSE(sdf_ble_ota_protocol_validate_chunk(23, 0));
}

void test_ble_ota_protocol_validate_chunk_mtu_too_small_rejected(void) {
    TEST_ASSERT_FALSE(sdf_ble_ota_protocol_validate_chunk(4, 1));
}

/* ---------- sdf_ble_ota_protocol_decide_begin ---------- */

void test_ble_ota_protocol_decide_begin_no_session_starts_new(void) {
    TEST_ASSERT_EQUAL(SDF_BLE_OTA_BEGIN_START_NEW,
                       sdf_ble_ota_protocol_decide_begin(false, 0, 4096));
}

void test_ble_ota_protocol_decide_begin_matching_size_resumes(void) {
    TEST_ASSERT_EQUAL(SDF_BLE_OTA_BEGIN_RESUME,
                       sdf_ble_ota_protocol_decide_begin(true, 4096, 4096));
}

void test_ble_ota_protocol_decide_begin_mismatched_size_rejected(void) {
    TEST_ASSERT_EQUAL(SDF_BLE_OTA_BEGIN_REJECT,
                       sdf_ble_ota_protocol_decide_begin(true, 4096, 8192));
}

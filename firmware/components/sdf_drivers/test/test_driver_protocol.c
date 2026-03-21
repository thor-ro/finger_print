#include "fingerprint_testable.h"
#include "unity.h"

/* ---------- fp_checksum ---------- */

void test_checksum_all_zeros(void) {
  uint8_t frame[FP_FRAME_LEN] = {0xF5, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0xF5};
  TEST_ASSERT_EQUAL_UINT8(0x00, fp_checksum(frame));
}

void test_checksum_known_command(void) {
  /* Frame: marker, cmd=0x0C, p1=0x00, p2=0x00, p3=0x00, 0x00, chk, marker
     XOR(0x0C, 0x00, 0x00, 0x00, 0x00) = 0x0C */
  uint8_t frame[FP_FRAME_LEN] = {0xF5, 0x0C, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0xF5};
  TEST_ASSERT_EQUAL_UINT8(0x0C, fp_checksum(frame));
}

void test_checksum_with_parameters(void) {
  /* XOR(0x01, 0x00, 0x05, 0x02, 0x00) = 0x01^0x00^0x05^0x02^0x00 = 0x06 */
  uint8_t frame[FP_FRAME_LEN] = {0xF5, 0x01, 0x00, 0x05,
                                 0x02, 0x00, 0x00, 0xF5};
  TEST_ASSERT_EQUAL_UINT8(0x06, fp_checksum(frame));
}

void test_checksum_all_ff(void) {
  uint8_t frame[FP_FRAME_LEN] = {0xF5, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0x00, 0xF5};
  /* XOR of 5 identical 0xFF bytes: 0xFF (odd count) */
  TEST_ASSERT_EQUAL_UINT8(0xFF, fp_checksum(frame));
}

void test_checksum_cancellation(void) {
  /* Two identical non-zero bytes cancel: XOR(0xAA, 0xAA, 0x00, 0x00, 0x00) = 0
   */
  uint8_t frame[FP_FRAME_LEN] = {0xF5, 0xAA, 0xAA, 0x00,
                                 0x00, 0x00, 0x00, 0xF5};
  TEST_ASSERT_EQUAL_UINT8(0x00, fp_checksum(frame));
}

/* ---------- fp_user_id_valid ---------- */

void test_user_id_valid_min(void) {
  TEST_ASSERT_TRUE(fp_user_id_valid(SDF_FINGERPRINT_USER_ID_MIN));
}

void test_user_id_valid_max(void) {
  TEST_ASSERT_TRUE(fp_user_id_valid(SDF_FINGERPRINT_USER_ID_MAX));
}

void test_user_id_valid_mid(void) {
  TEST_ASSERT_TRUE(fp_user_id_valid(100));
}

void test_user_id_invalid_zero(void) {
  TEST_ASSERT_FALSE(fp_user_id_valid(0));
}

void test_user_id_invalid_above_max(void) {
  TEST_ASSERT_FALSE(fp_user_id_valid(SDF_FINGERPRINT_USER_ID_MAX + 1));
}

void test_user_id_invalid_uint16_max(void) {
  TEST_ASSERT_FALSE(fp_user_id_valid(0xFFFF));
}

/* ---------- fp_map_ack_code ---------- */

void test_map_ack_success(void) {
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_OP_OK,
                    fp_map_ack_code(SDF_FINGERPRINT_ACK_SUCCESS));
}

void test_map_ack_timeout(void) {
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_OP_TIMEOUT,
                    fp_map_ack_code(SDF_FINGERPRINT_ACK_TIMEOUT));
}

void test_map_ack_full(void) {
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_OP_FULL,
                    fp_map_ack_code(SDF_FINGERPRINT_ACK_FULL));
}

void test_map_ack_user_occupied(void) {
  TEST_ASSERT_EQUAL(
      SDF_FINGERPRINT_OP_USER_OCCUPIED,
      fp_map_ack_code(SDF_FINGERPRINT_ACK_USER_OCCUPIED));
}

void test_map_ack_finger_occupied(void) {
  TEST_ASSERT_EQUAL(
      SDF_FINGERPRINT_OP_FINGER_OCCUPIED,
      fp_map_ack_code(SDF_FINGERPRINT_ACK_FINGER_OCCUPIED));
}

void test_map_ack_fail(void) {
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_OP_FAILED,
                    fp_map_ack_code(SDF_FINGERPRINT_ACK_FAIL));
}

void test_map_ack_nouser(void) {
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_OP_FAILED,
                    fp_map_ack_code(SDF_FINGERPRINT_ACK_NOUSER));
}

void test_map_ack_unknown(void) {
  TEST_ASSERT_EQUAL(SDF_FINGERPRINT_OP_FAILED, fp_map_ack_code(0xFF));
}

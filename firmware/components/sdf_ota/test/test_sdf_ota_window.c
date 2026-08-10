#include "unity.h"

#include <string.h>

#include "sdf_ota_digest.h"

/* sdf_ota_window_capture() is the arithmetic sdf_ota_write() drives three times
 * per chunk - digest range, app-desc window, footer window. It lives in
 * sdf_ota_signature.c rather than sdf_ota.c precisely so it is reachable from
 * here: sdf_ota.c is excluded from IDF_TARGET=linux, and chunk-boundary
 * arithmetic left there would be untestable. */

/* A stream shaped like a real transfer: the app-desc window at [32, 288) and
 * the footer window at the very end, with both boundaries landing mid-chunk for
 * the 244-byte writes the BLE companion delivers. */
#define STREAM_LEN  400u
#define FOOTER_START (STREAM_LEN - SDF_OTA_FOOTER_SIZE)

#define FILL 0xCD  /* nothing the stream itself produces */

static uint8_t s_stream[STREAM_LEN];

static void fill_stream(void) {
  /* Non-repeating over the whole stream, so a window captured one byte off
   * fails rather than matching by accident. */
  for (size_t i = 0; i < sizeof(s_stream); i++) {
    s_stream[i] = (uint8_t)((i * 31u + (i >> 3)) & 0xFF);
  }
}

/* Feed the whole stream through the capture in chunks bounded by the given
 * split points, the way sdf_ota_write() feeds it: absolute stream offset,
 * every byte exactly once, in order. */
static void capture_with_splits(uint8_t *dst, uint32_t win_start, uint32_t win_len,
                                const uint32_t *splits, size_t n_splits) {
  memset(dst, FILL, win_len);

  uint32_t offset = 0;
  for (size_t i = 0; i <= n_splits; i++) {
    uint32_t end = (i < n_splits) ? splits[i] : STREAM_LEN;
    sdf_ota_window_capture(dst, win_start, win_len, offset, s_stream + offset, end - offset);
    offset = end;
  }
  TEST_ASSERT_EQUAL_UINT32(STREAM_LEN, offset);
}

void test_sdf_ota_window_every_split_point_identical(void) {
  fill_stream();

  const uint32_t windows[][2] = {
      {SDF_OTA_APP_DESC_OFFSET, SDF_OTA_APP_DESC_SIZE},  /* app descriptor */
      {FOOTER_START, SDF_OTA_FOOTER_SIZE},               /* signature footer */
      {0, STREAM_LEN},                                   /* the whole stream */
      {0, 1},                                            /* first byte only */
      {STREAM_LEN - 1, 1},                               /* last byte only */
  };

  for (size_t w = 0; w < sizeof(windows) / sizeof(windows[0]); w++) {
    const uint32_t win_start = windows[w][0];
    const uint32_t win_len = windows[w][1];

    /* Every single split point, 0..STREAM_LEN inclusive - which covers both
     * degenerate cases (one chunk of everything, and an empty leading chunk)
     * as well as every boundary that lands inside the window. */
    for (uint32_t k = 0; k <= STREAM_LEN; k++) {
      uint8_t got[STREAM_LEN];
      capture_with_splits(got, win_start, win_len, &k, 1);
      TEST_ASSERT_EQUAL_UINT8_ARRAY(s_stream + win_start, got, win_len);
    }
  }
}

void test_sdf_ota_window_spanning_three_chunks(void) {
  fill_stream();

  /* Exhaustive over pairs of split points for the app-desc window: every way
   * the window can be straddled by three chunks, including the case where it
   * lies entirely inside the middle one. */
  for (uint32_t a = 0; a <= STREAM_LEN; a++) {
    for (uint32_t b = a; b <= STREAM_LEN; b++) {
      const uint32_t splits[2] = {a, b};
      uint8_t got[SDF_OTA_APP_DESC_SIZE];
      capture_with_splits(got, SDF_OTA_APP_DESC_OFFSET, SDF_OTA_APP_DESC_SIZE, splits, 2);
      TEST_ASSERT_EQUAL_UINT8_ARRAY(s_stream + SDF_OTA_APP_DESC_OFFSET, got,
                                    SDF_OTA_APP_DESC_SIZE);
    }
  }

  /* The 68-byte footer window under real 244-byte BLE chunking: 400 = 244 + 156,
   * so the window at [332, 400) starts inside the second chunk. */
  const uint32_t ble_split[1] = {244};
  uint8_t footer[SDF_OTA_FOOTER_SIZE];
  capture_with_splits(footer, FOOTER_START, SDF_OTA_FOOTER_SIZE, ble_split, 1);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(s_stream + FOOTER_START, footer, SDF_OTA_FOOTER_SIZE);
}

void test_sdf_ota_window_chunks_outside_window_leave_dst_untouched(void) {
  fill_stream();

  uint8_t dst[SDF_OTA_APP_DESC_SIZE];
  uint8_t untouched[SDF_OTA_APP_DESC_SIZE];
  memset(dst, FILL, sizeof(dst));
  memset(untouched, FILL, sizeof(untouched));

  const uint32_t win_start = SDF_OTA_APP_DESC_OFFSET;
  const uint32_t win_len = SDF_OTA_APP_DESC_SIZE;

  /* Wholly before, including the chunk that ends exactly at win_start. */
  sdf_ota_window_capture(dst, win_start, win_len, 0, s_stream, win_start);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(untouched, dst, win_len);

  /* Wholly after, including the chunk that starts exactly at the window end. */
  const uint32_t after = win_start + win_len;
  sdf_ota_window_capture(dst, win_start, win_len, after, s_stream + after, STREAM_LEN - after);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(untouched, dst, win_len);

  /* One byte of overlap at each edge is *not* outside, and must land. */
  sdf_ota_window_capture(dst, win_start, win_len, win_start - 1, s_stream + win_start - 1, 2);
  TEST_ASSERT_EQUAL_UINT8(s_stream[win_start], dst[0]);
  TEST_ASSERT_EQUAL_UINT8(FILL, dst[1]);

  sdf_ota_window_capture(dst, win_start, win_len, after - 1, s_stream + after - 1, 2);
  TEST_ASSERT_EQUAL_UINT8(s_stream[after - 1], dst[win_len - 1]);
}

void test_sdf_ota_window_degenerate_inputs_are_no_ops(void) {
  fill_stream();

  uint8_t dst[64];
  uint8_t untouched[64];
  memset(dst, FILL, sizeof(dst));
  memset(untouched, FILL, sizeof(untouched));

  /* Empty chunk, empty window, NULL destination, NULL data. */
  sdf_ota_window_capture(dst, 0, sizeof(dst), 0, s_stream, 0);
  sdf_ota_window_capture(dst, 0, 0, 0, s_stream, STREAM_LEN);
  sdf_ota_window_capture(NULL, 0, sizeof(dst), 0, s_stream, STREAM_LEN);
  sdf_ota_window_capture(dst, 0, sizeof(dst), 0, NULL, STREAM_LEN);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(untouched, dst, sizeof(dst));

  /* Ranges that would wrap uint32 are refused rather than wrapped into a
   * bogus intersection. */
  sdf_ota_window_capture(dst, 0xFFFFFFF0u, 64, 0xFFFFFFF0u, s_stream, 64);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(untouched, dst, sizeof(dst));

  /* Feeding the same chunk twice is harmless - capture is idempotent per byte,
   * which is what makes a resumed transfer that re-sends a chunk boundary
   * safe. */
  uint8_t once[SDF_OTA_FOOTER_SIZE];
  uint8_t twice[SDF_OTA_FOOTER_SIZE];
  memset(once, FILL, sizeof(once));
  memset(twice, FILL, sizeof(twice));
  sdf_ota_window_capture(once, FOOTER_START, SDF_OTA_FOOTER_SIZE, FOOTER_START,
                         s_stream + FOOTER_START, SDF_OTA_FOOTER_SIZE);
  sdf_ota_window_capture(twice, FOOTER_START, SDF_OTA_FOOTER_SIZE, FOOTER_START,
                         s_stream + FOOTER_START, SDF_OTA_FOOTER_SIZE);
  sdf_ota_window_capture(twice, FOOTER_START, SDF_OTA_FOOTER_SIZE, FOOTER_START,
                         s_stream + FOOTER_START, SDF_OTA_FOOTER_SIZE);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(once, twice, SDF_OTA_FOOTER_SIZE);
}

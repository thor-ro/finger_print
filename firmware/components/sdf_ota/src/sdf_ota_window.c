#include "sdf_ota.h"

#include <string.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Transfer-window capture
// -----------------------------------------------------------------------------

void sdf_ota_window_capture(uint8_t *dst, uint32_t win_start, uint32_t win_len,
                            uint32_t stream_offset, const void *data, uint32_t len)
{
    if (dst == NULL || data == NULL || win_len == 0 || len == 0) {
        return;
    }
    /* Both ranges are bounded by the image size for every caller in the tree,
     * so neither sum can wrap; guarded anyway so the primitive is total and the
     * clamps below cannot be reasoned around. */
    if (win_start + win_len < win_start || stream_offset + len < stream_offset) {
        return;
    }

    const uint32_t win_end = win_start + win_len;
    const uint32_t chunk_end = stream_offset + len;

    /* Wholly before or wholly after the window: nothing to do, dst untouched. */
    if (chunk_end <= win_start || stream_offset >= win_end) {
        return;
    }

    const uint32_t from = (stream_offset > win_start) ? stream_offset : win_start;
    const uint32_t to = (chunk_end < win_end) ? chunk_end : win_end;

    memcpy(dst + (from - win_start), (const uint8_t *)data + (from - stream_offset), to - from);
}

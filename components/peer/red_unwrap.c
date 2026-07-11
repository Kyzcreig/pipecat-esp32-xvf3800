/* red_unwrap.c — RFC 2198 (RED) depacketizer. See red_unwrap.h for contract.
 *
 * Pure byte logic, host-testable (tests/host/test_red_unwrap.c). Fail-safe:
 * ANY malformation returns -1 with a zeroed output so RED headers never reach
 * the Opus decoder as though they were bare Opus.
 */
#include "red_unwrap.h"

#include <string.h>

int red_unwrap(const uint8_t* payload, size_t size, uint8_t expected_pt,
               RedParsed* out) {
  if (out == NULL) {
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (payload == NULL || size < 1) {
    return -1;
  }
  RedParsed parsed = {0};

  /* Pass 1: walk the header chain (4-byte headers while F=1, then the 1-byte
   * final header). */
  size_t pos = 0;
  size_t data_total = 0;
  while (pos < size && (payload[pos] & 0x80)) {
    if (pos + 4 > size) {
      return -1;
    }
    if (parsed.block_count >= RED_MAX_BLOCKS) {
      return -1;
    }
    uint8_t pt = payload[pos] & 0x7F;
    if (pt != expected_pt) {
      return -1;
    }
    uint32_t value = ((uint32_t)payload[pos + 1] << 16) |
                     ((uint32_t)payload[pos + 2] << 8) | payload[pos + 3];
    RedBlock* block = &parsed.blocks[parsed.block_count];
    block->ts_offset = (uint16_t)(value >> 10);
    block->length = (uint16_t)(value & 0x3FF);
    if (block->ts_offset == 0) {
      return -1;
    }
    data_total += block->length;
    parsed.block_count++;
    pos += 4;
  }
  if (pos >= size) {
    return -1;
  }
  if ((payload[pos] & 0x7F) != expected_pt) {
    return -1;
  }
  pos += 1;

  /* Pass 2: slice block data (oldest-first order, primary last). */
  if (pos + data_total > size) {
    return -1;
  }
  size_t data_pos = pos;
  for (int i = 0; i < parsed.block_count; i++) {
    parsed.blocks[i].data = payload + data_pos;
    data_pos += parsed.blocks[i].length;
  }
  parsed.primary = payload + data_pos;
  parsed.primary_size = size - data_pos;
  if (parsed.primary_size == 0) {
    return -1;
  }
  *out = parsed;
  return 0;
}

int red_validate_timestamp_advance(uint32_t last_ts, uint32_t current_ts,
                                   int gap, uint32_t expected_step,
                                   uint32_t* validated_step) {
  if (gap <= 0 || gap > RED_MAX_GAP || expected_step == 0 ||
      validated_step == NULL) {
    return -1;
  }
  uint64_t expected_advance = (uint64_t)(gap + 1) * expected_step;
  uint32_t actual_advance = current_ts - last_ts;
  if (expected_advance > UINT32_MAX ||
      actual_advance != (uint32_t)expected_advance) {
    return -1;
  }
  *validated_step = expected_step;
  return 0;
}

int red_recover_plan(const RedParsed* parsed, int gap, uint32_t ts_step,
                     int8_t actions[RED_MAX_GAP]) {
  if (parsed == NULL || actions == NULL || gap <= 0 || ts_step == 0) {
    return 0;
  }
  int count = gap > RED_MAX_GAP ? RED_MAX_GAP : gap;
  /* Missing frame i (i=0 oldest) sits (count-i) packets before primary. */
  for (int i = 0; i < count; i++) {
    uint32_t wanted_offset = (uint32_t)(count - i) * ts_step;
    actions[i] = -1;
    if (wanted_offset <= 0x3FFF) {
      for (int block = 0; block < parsed->block_count; block++) {
        if (parsed->blocks[block].ts_offset == wanted_offset &&
            parsed->blocks[block].length > 0) {
          actions[i] = (int8_t)block;
          break;
        }
      }
    }
  }
  return count;
}

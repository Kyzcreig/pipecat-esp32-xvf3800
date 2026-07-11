/* Host-side tests for the RFC 2198 depacketizer and recovery planner.
 *
 * Pure byte logic: ./tests/host/run_red_tests.sh
 * Golden vectors mirror the sender-side encapsulation contract.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "red_unwrap.h"

#define OPUS_PT 111
#define FRAME_TS 960

static int tests_run = 0;
#define RUN(fn)                 \
  do {                          \
    fn();                       \
    tests_run++;                \
    printf("ok - %s\n", #fn);  \
  } while (0)

static size_t hdr(uint8_t* out, uint8_t pt, uint16_t ts_offset, uint16_t len) {
  uint32_t value = ((uint32_t)ts_offset << 10) | len;
  out[0] = 0x80 | pt;
  out[1] = (value >> 16) & 0xFF;
  out[2] = (value >> 8) & 0xFF;
  out[3] = value & 0xFF;
  return 4;
}

static void test_primary_only(void) {
  uint8_t payload[] = {OPUS_PT, 0xAA, 0xBB, 0xCC};
  RedParsed parsed;
  assert(red_unwrap(payload, sizeof(payload), OPUS_PT, &parsed) == 0);
  assert(parsed.block_count == 0);
  assert(parsed.primary_size == 3);
  assert(memcmp(parsed.primary, "\xAA\xBB\xCC", 3) == 0);
}

static void test_one_redundant_block(void) {
  uint8_t payload[32];
  size_t size = hdr(payload, OPUS_PT, FRAME_TS, 2);
  payload[size++] = OPUS_PT;
  memcpy(payload + size, "\x01\x02", 2);
  size += 2;
  memcpy(payload + size, "\x03\x04\x05", 3);
  size += 3;
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == 0);
  assert(parsed.block_count == 1);
  assert(parsed.blocks[0].ts_offset == FRAME_TS);
  assert(parsed.blocks[0].length == 2);
  assert(memcmp(parsed.blocks[0].data, "\x01\x02", 2) == 0);
  assert(parsed.primary_size == 3);
  assert(memcmp(parsed.primary, "\x03\x04\x05", 3) == 0);
}

static void test_two_blocks_oldest_first(void) {
  uint8_t payload[32];
  size_t size = hdr(payload, OPUS_PT, 2 * FRAME_TS, 1);
  size += hdr(payload + size, OPUS_PT, FRAME_TS, 2);
  payload[size++] = OPUS_PT;
  payload[size++] = 0x01;
  memcpy(payload + size, "\x02\x02", 2);
  size += 2;
  memcpy(payload + size, "\x03\x03\x03", 3);
  size += 3;
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == 0);
  assert(parsed.block_count == 2);
  assert(parsed.blocks[0].ts_offset == 2 * FRAME_TS);
  assert(parsed.blocks[0].length == 1);
  assert(parsed.blocks[1].ts_offset == FRAME_TS);
  assert(parsed.blocks[1].length == 2);
  assert(parsed.blocks[0].data[0] == 0x01);
  assert(memcmp(parsed.blocks[1].data, "\x02\x02", 2) == 0);
  assert(parsed.primary_size == 3);
  assert(memcmp(parsed.primary, "\x03\x03\x03", 3) == 0);
}

static void test_realistic_sizes_vector(void) {
  uint8_t payload[512];
  size_t size = hdr(payload, OPUS_PT, 1920, 80);
  size += hdr(payload + size, OPUS_PT, 960, 100);
  payload[size++] = OPUS_PT;
  memset(payload + size, 0x11, 80);
  size += 80;
  memset(payload + size, 0x22, 100);
  size += 100;
  memset(payload + size, 0x33, 90);
  size += 90;
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == 0);
  assert(parsed.block_count == 2);
  assert(parsed.blocks[0].length == 80 && parsed.blocks[0].data[0] == 0x11);
  assert(parsed.blocks[1].length == 100 && parsed.blocks[1].data[99] == 0x22);
  assert(parsed.primary_size == 90);
  assert(parsed.primary[0] == 0x33 && parsed.primary[89] == 0x33);
}

static void test_truncated_header(void) {
  uint8_t payload[] = {0x80 | OPUS_PT, 0x0F};
  RedParsed parsed;
  assert(red_unwrap(payload, sizeof(payload), OPUS_PT, &parsed) == -1);
}

static void test_missing_final_header(void) {
  uint8_t payload[8];
  size_t size = hdr(payload, OPUS_PT, FRAME_TS, 2);
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == -1);
}

static void test_foreign_pt_rejected(void) {
  uint8_t payload[32];
  size_t size = hdr(payload, 96, FRAME_TS, 1);
  payload[size++] = OPUS_PT;
  payload[size++] = 0x01;
  payload[size++] = 0x02;
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == -1);
}

static void test_primary_pt_mismatch(void) {
  uint8_t payload[] = {96, 0xAA};
  RedParsed parsed;
  assert(red_unwrap(payload, sizeof(payload), OPUS_PT, &parsed) == -1);
}

static void test_lengths_exceed_payload(void) {
  uint8_t payload[16];
  size_t size = hdr(payload, OPUS_PT, FRAME_TS, 200);
  payload[size++] = OPUS_PT;
  payload[size++] = 0x01;
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == -1);
}

static void test_empty_primary_rejected(void) {
  uint8_t payload[8];
  size_t size = hdr(payload, OPUS_PT, FRAME_TS, 1);
  payload[size++] = OPUS_PT;
  payload[size++] = 0x01;
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == -1);
}

static void test_zero_ts_offset_rejected(void) {
  uint8_t payload[8];
  size_t size = hdr(payload, OPUS_PT, 0, 1);
  payload[size++] = OPUS_PT;
  payload[size++] = 0x01;
  payload[size++] = 0x02;
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == -1);
}

static void test_too_many_blocks_rejected(void) {
  uint8_t payload[64];
  size_t size = 0;
  for (int i = 0; i < RED_MAX_BLOCKS + 1; i++) {
    size += hdr(payload + size, OPUS_PT,
                (uint16_t)((i + 1) * FRAME_TS), 1);
  }
  payload[size++] = OPUS_PT;
  memset(payload + size, 0xEE, RED_MAX_BLOCKS + 2);
  size += RED_MAX_BLOCKS + 2;
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == -1);
}

static void test_empty_and_null(void) {
  RedParsed parsed;
  uint8_t byte = OPUS_PT;
  assert(red_unwrap(NULL, 10, OPUS_PT, &parsed) == -1);
  assert(red_unwrap(&byte, 0, OPUS_PT, &parsed) == -1);
  assert(red_unwrap(&byte, 1, OPUS_PT, NULL) == -1);
}

static void test_malformed_output_is_safe_gap(void) {
  uint8_t payload[] = {0x80 | OPUS_PT, 0x0F};
  RedParsed parsed;
  memset(&parsed, 0xA5, sizeof(parsed));
  assert(red_unwrap(payload, sizeof(payload), OPUS_PT, &parsed) == -1);
  assert(parsed.block_count == 0);
  assert(parsed.primary == NULL);
  assert(parsed.primary_size == 0);

  memset(&parsed, 0xA5, sizeof(parsed));
  assert(red_unwrap(payload, 0, OPUS_PT, &parsed) == -1);
  assert(parsed.block_count == 0);
  assert(parsed.primary == NULL);
  assert(parsed.primary_size == 0);
}

static RedParsed make_n2_parsed(void) {
  static uint8_t payload[64];
  size_t size = hdr(payload, OPUS_PT, 2 * FRAME_TS, 3);
  size += hdr(payload + size, OPUS_PT, FRAME_TS, 4);
  payload[size++] = OPUS_PT;
  memset(payload + size, 0xA2, 3);
  size += 3;
  memset(payload + size, 0xA1, 4);
  size += 4;
  memset(payload + size, 0xA0, 5);
  size += 5;
  RedParsed parsed;
  assert(red_unwrap(payload, size, OPUS_PT, &parsed) == 0);
  return parsed;
}

static void test_plan_single_gap(void) {
  RedParsed parsed = make_n2_parsed();
  int8_t actions[RED_MAX_GAP];
  assert(red_recover_plan(&parsed, 1, FRAME_TS, actions) == 1);
  assert(actions[0] == 1);
  assert(parsed.blocks[actions[0]].data[0] == 0xA1);
}

static void test_plan_double_gap(void) {
  RedParsed parsed = make_n2_parsed();
  int8_t actions[RED_MAX_GAP];
  assert(red_recover_plan(&parsed, 2, FRAME_TS, actions) == 2);
  assert(actions[0] == 0);
  assert(actions[1] == 1);
}

static void test_plan_triple_gap_partial(void) {
  RedParsed parsed = make_n2_parsed();
  int8_t actions[RED_MAX_GAP];
  assert(red_recover_plan(&parsed, 3, FRAME_TS, actions) == 3);
  assert(actions[0] == -1);
  assert(actions[1] == 0);
  assert(actions[2] == 1);
}

static void test_plan_no_blocks_all_plc(void) {
  uint8_t payload[] = {OPUS_PT, 0xAA};
  RedParsed parsed;
  assert(red_unwrap(payload, sizeof(payload), OPUS_PT, &parsed) == 0);
  int8_t actions[RED_MAX_GAP];
  assert(red_recover_plan(&parsed, 2, FRAME_TS, actions) == 2);
  assert(actions[0] == -1 && actions[1] == -1);
}

static void test_plan_gap_cap(void) {
  RedParsed parsed = make_n2_parsed();
  int8_t actions[RED_MAX_GAP];
  assert(red_recover_plan(&parsed, 99, FRAME_TS, actions) == RED_MAX_GAP);
}

static void test_plan_bad_args(void) {
  RedParsed parsed = make_n2_parsed();
  int8_t actions[RED_MAX_GAP];
  assert(red_recover_plan(NULL, 1, FRAME_TS, actions) == 0);
  assert(red_recover_plan(&parsed, 0, FRAME_TS, actions) == 0);
  assert(red_recover_plan(&parsed, -3, FRAME_TS, actions) == 0);
  assert(red_recover_plan(&parsed, 1, 0, actions) == 0);
  assert(red_recover_plan(&parsed, 1, FRAME_TS, NULL) == 0);
}

static void test_plan_mismatched_ts_step(void) {
  RedParsed parsed = make_n2_parsed();
  int8_t actions[RED_MAX_GAP];
  assert(red_recover_plan(&parsed, 1, 2880, actions) == 1);
  assert(actions[0] == -1);
}

static void test_timestamp_profile_exact_gap(void) {
  uint32_t step = 0;
  assert(red_validate_timestamp_advance(
             1000, 1000 + 3 * RED_OPUS_TS_STEP_20MS, 2,
             RED_OPUS_TS_STEP_20MS, &step) == 0);
  assert(step == RED_OPUS_TS_STEP_20MS);
}

static void test_timestamp_profile_rejects_mixed_ptime(void) {
  uint32_t step = 123;
  assert(red_validate_timestamp_advance(
             1000, 1000 + 3 * RED_OPUS_TS_STEP_20MS + 480, 2,
             RED_OPUS_TS_STEP_20MS, &step) == -1);
  assert(step == 123);
}

static void test_timestamp_profile_handles_wraparound(void) {
  uint32_t last = UINT32_MAX - 959;
  uint32_t current = last + 2 * RED_OPUS_TS_STEP_20MS;
  uint32_t step = 0;
  assert(red_validate_timestamp_advance(
             last, current, 1, RED_OPUS_TS_STEP_20MS, &step) == 0);
  assert(step == RED_OPUS_TS_STEP_20MS);
}

static void test_timestamp_profile_bad_args(void) {
  uint32_t step = 0;
  assert(red_validate_timestamp_advance(0, 960, 0,
                                        RED_OPUS_TS_STEP_20MS, &step) == -1);
  assert(red_validate_timestamp_advance(0, 960, RED_MAX_GAP + 1,
                                        RED_OPUS_TS_STEP_20MS, &step) == -1);
  assert(red_validate_timestamp_advance(0, 960, 1, 0, &step) == -1);
  assert(red_validate_timestamp_advance(0, 960, 1,
                                        RED_OPUS_TS_STEP_20MS, NULL) == -1);
}

int main(void) {
  RUN(test_primary_only);
  RUN(test_one_redundant_block);
  RUN(test_two_blocks_oldest_first);
  RUN(test_realistic_sizes_vector);
  RUN(test_truncated_header);
  RUN(test_missing_final_header);
  RUN(test_foreign_pt_rejected);
  RUN(test_primary_pt_mismatch);
  RUN(test_lengths_exceed_payload);
  RUN(test_empty_primary_rejected);
  RUN(test_zero_ts_offset_rejected);
  RUN(test_too_many_blocks_rejected);
  RUN(test_empty_and_null);
  RUN(test_malformed_output_is_safe_gap);
  RUN(test_plan_single_gap);
  RUN(test_plan_double_gap);
  RUN(test_plan_triple_gap_partial);
  RUN(test_plan_no_blocks_all_plc);
  RUN(test_plan_gap_cap);
  RUN(test_plan_bad_args);
  RUN(test_plan_mismatched_ts_step);
  RUN(test_timestamp_profile_exact_gap);
  RUN(test_timestamp_profile_rejects_mixed_ptime);
  RUN(test_timestamp_profile_handles_wraparound);
  RUN(test_timestamp_profile_bad_args);
  printf("PASS: %d red_unwrap host tests\n", tests_run);
  return 0;
}

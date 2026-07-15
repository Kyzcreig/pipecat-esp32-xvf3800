#include "decode_ladder.h"
#include "red_unwrap.h"

#include <assert.h>
#include <stdio.h>

static int tests_run = 0;
#define RUN(test)            \
  do {                       \
    test();                  \
    tests_run++;             \
    printf("ok - %s\n", #test); \
  } while (0)

static size_t append_frame(decode_ladder *ladder, decode_ladder_op *all,
                           size_t count) {
  decode_ladder_op next[DL_MAX_PENDING_GAPS + 1];
  size_t produced =
      dl_on_frame(ladder, next, sizeof(next) / sizeof(next[0]));
  for (size_t i = 0; i < produced; i++) all[count++] = next[i];
  return count;
}

static void assert_ops(const decode_ladder_op *actual, size_t count,
                       const decode_ladder_op *expected,
                       size_t expected_count) {
  assert(count == expected_count);
  for (size_t i = 0; i < count; i++) assert(actual[i] == expected[i]);
}

static void test_no_gap_decodes_frame_once(void) {
  decode_ladder ladder;
  decode_ladder_op operations[4];
  dl_init(&ladder);
  size_t count = append_frame(&ladder, operations, 0);
  const decode_ladder_op expected[] = {DL_OP_FRAME};
  assert_ops(operations, count, expected, 1);
}

static void test_one_gap_attempts_fec_then_decodes_frame(void) {
  decode_ladder ladder;
  decode_ladder_op operations[4];
  dl_init(&ladder);
  assert(dl_on_gap(&ladder) == 0);
  size_t count = append_frame(&ladder, operations, 0);
  const decode_ladder_op expected[] = {DL_OP_FEC_ATTEMPT, DL_OP_FRAME};
  assert_ops(operations, count, expected, 2);
}

static void test_deep_gap_uses_plc_then_fec_then_frame(void) {
  decode_ladder ladder;
  decode_ladder_op operations[8];
  dl_init(&ladder);
  assert(dl_on_gap(&ladder) == 0);
  assert(dl_on_gap(&ladder) == 0);
  assert(dl_on_gap(&ladder) == 0);
  size_t count = append_frame(&ladder, operations, 0);
  const decode_ladder_op expected[] = {
      DL_OP_PLC, DL_OP_PLC, DL_OP_FEC_ATTEMPT, DL_OP_FRAME};
  assert_ops(operations, count, expected, 4);
}

static void test_older_red_then_newer_gap(void) {
  decode_ladder ladder;
  decode_ladder_op operations[8];
  dl_init(&ladder);
  size_t count = append_frame(&ladder, operations, 0); /* older RED frame */
  assert(dl_on_gap(&ladder) == 0);                    /* newer uncovered */
  count = append_frame(&ladder, operations, count);  /* RTP primary */
  const decode_ladder_op expected[] = {
      DL_OP_FRAME, DL_OP_FEC_ATTEMPT, DL_OP_FRAME};
  assert_ops(operations, count, expected, 3);
}

static void test_older_gap_then_newer_red(void) {
  decode_ladder ladder;
  decode_ladder_op operations[8];
  dl_init(&ladder);
  assert(dl_on_gap(&ladder) == 0);                    /* older uncovered */
  size_t count = append_frame(&ladder, operations, 0); /* newer RED frame */
  count = append_frame(&ladder, operations, count);    /* RTP primary */
  const decode_ladder_op expected[] = {
      DL_OP_FEC_ATTEMPT, DL_OP_FRAME, DL_OP_FRAME};
  assert_ops(operations, count, expected, 3);
}

static void test_red_plan_partial_gap_composes_chronologically(void) {
  RedParsed parsed = {0};
  parsed.block_count = 2;
  parsed.blocks[0].ts_offset = 1920;
  parsed.blocks[0].length = 1;
  parsed.blocks[1].ts_offset = 960;
  parsed.blocks[1].length = 1;

  int8_t actions[RED_MAX_GAP] = {0};
  assert(red_recover_plan(&parsed, 3, 960, actions) == 3);
  assert(actions[0] == -1);
  assert(actions[1] == 0);
  assert(actions[2] == 1);

  decode_ladder ladder;
  decode_ladder_op operations[12];
  dl_init(&ladder);
  size_t count = 0;
  for (size_t i = 0; i < 3; i++) {
    if (actions[i] < 0) {
      assert(dl_on_gap(&ladder) == 0);
    } else {
      count = append_frame(&ladder, operations, count);
    }
  }
  count = append_frame(&ladder, operations, count); /* current primary */

  const decode_ladder_op expected[] = {
      DL_OP_FEC_ATTEMPT, DL_OP_FRAME, DL_OP_FRAME, DL_OP_FRAME};
  assert_ops(operations, count, expected, 4);
}

static void test_capacity_failure_preserves_pending_state(void) {
  decode_ladder ladder;
  decode_ladder_op operations[2];
  dl_init(&ladder);
  assert(dl_on_gap(&ladder) == 0);
  assert(dl_on_gap(&ladder) == 0);
  assert(dl_on_frame(&ladder, operations, 2) == 0);
  assert(ladder.pending_gaps == 2);
}

static void test_gap_cap_fails_closed(void) {
  decode_ladder ladder;
  dl_init(&ladder);
  for (uint32_t i = 0; i < DL_MAX_PENDING_GAPS; i++) {
    assert(dl_on_gap(&ladder) == 0);
  }
  assert(dl_on_gap(&ladder) == -1);
  assert(ladder.pending_gaps == DL_MAX_PENDING_GAPS);
}

int main(void) {
  RUN(test_no_gap_decodes_frame_once);
  RUN(test_one_gap_attempts_fec_then_decodes_frame);
  RUN(test_deep_gap_uses_plc_then_fec_then_frame);
  RUN(test_older_red_then_newer_gap);
  RUN(test_older_gap_then_newer_red);
  RUN(test_red_plan_partial_gap_composes_chronologically);
  RUN(test_capacity_failure_preserves_pending_state);
  RUN(test_gap_cap_fails_closed);
  printf("PASS: %d decode_ladder host tests\n", tests_run);
  return 0;
}

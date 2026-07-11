#include "rtp_sequence.h"

#include <assert.h>
#include <stdio.h>

static int tests_run = 0;
#define RUN(fn)                 \
  do {                          \
    fn();                       \
    tests_run++;                \
    printf("ok - %s\n", #fn);  \
  } while (0)

static void test_first_and_next_packet(void) {
  RtpSequenceState state;
  rtp_sequence_init(&state);

  RtpSequenceEvent event = rtp_sequence_update(&state, 100, 1000, 7);
  assert(event.kind == RTP_SEQUENCE_FIRST);
  assert(state.initialized == 1);
  assert(state.last_seq == 100);
  assert(state.last_timestamp == 1000);
  assert(state.ssrc == 7);

  event = rtp_sequence_update(&state, 101, 1960, 7);
  assert(event.kind == RTP_SEQUENCE_NEXT);
  assert(event.previous_timestamp == 1000);
  assert(event.gap == 0);
}

static void test_forward_gap_reports_previous_timestamp(void) {
  RtpSequenceState state;
  rtp_sequence_init(&state);
  (void)rtp_sequence_update(&state, 10, 5000, 9);

  RtpSequenceEvent event = rtp_sequence_update(&state, 13, 7880, 9);
  assert(event.kind == RTP_SEQUENCE_GAP);
  assert(event.gap == 2);
  assert(event.previous_timestamp == 5000);
  assert(state.last_seq == 13);
  assert(state.last_timestamp == 7880);
}

static void test_late_packet_does_not_move_state(void) {
  RtpSequenceState state;
  rtp_sequence_init(&state);
  (void)rtp_sequence_update(&state, 200, 10000, 11);
  (void)rtp_sequence_update(&state, 201, 10960, 11);

  RtpSequenceEvent event = rtp_sequence_update(&state, 200, 10000, 11);
  assert(event.kind == RTP_SEQUENCE_LATE);
  assert(state.last_seq == 201);
  assert(state.last_timestamp == 10960);
}

static void test_sequence_wraparound_is_next(void) {
  RtpSequenceState state;
  rtp_sequence_init(&state);
  (void)rtp_sequence_update(&state, UINT16_MAX, 1000, 13);

  RtpSequenceEvent event = rtp_sequence_update(&state, 0, 1960, 13);
  assert(event.kind == RTP_SEQUENCE_NEXT);
}

static void test_ssrc_change_starts_new_epoch(void) {
  RtpSequenceState state;
  rtp_sequence_init(&state);
  (void)rtp_sequence_update(&state, 50000, 100000, 17);

  RtpSequenceEvent event = rtp_sequence_update(&state, 10, 2000, 18);
  assert(event.kind == RTP_SEQUENCE_STREAM_RESET);
  assert(state.last_seq == 10);
  assert(state.last_timestamp == 2000);
  assert(state.ssrc == 18);
}

static void test_explicit_reinit_allows_lower_sequence_same_ssrc(void) {
  RtpSequenceState state;
  rtp_sequence_init(&state);
  (void)rtp_sequence_update(&state, 50000, 100000, 21);

  rtp_sequence_init(&state);
  RtpSequenceEvent event = rtp_sequence_update(&state, 10, 2000, 21);
  assert(event.kind == RTP_SEQUENCE_FIRST);
  assert(state.last_seq == 10);
}

static void test_five_decoder_states_are_independent(void) {
  RtpSequenceState states[5];
  for (int i = 0; i < 5; i++) {
    rtp_sequence_init(&states[i]);
    RtpSequenceEvent event = rtp_sequence_update(
        &states[i], (uint16_t)(100 + i), (uint32_t)(1000 + i * 960),
        (uint32_t)(30 + i));
    assert(event.kind == RTP_SEQUENCE_FIRST);
  }
  for (int i = 0; i < 5; i++) {
    RtpSequenceEvent event = rtp_sequence_update(
        &states[i], (uint16_t)(101 + i), (uint32_t)(1960 + i * 960),
        (uint32_t)(30 + i));
    assert(event.kind == RTP_SEQUENCE_NEXT);
  }
}

int main(void) {
  RUN(test_first_and_next_packet);
  RUN(test_forward_gap_reports_previous_timestamp);
  RUN(test_late_packet_does_not_move_state);
  RUN(test_sequence_wraparound_is_next);
  RUN(test_ssrc_change_starts_new_epoch);
  RUN(test_explicit_reinit_allows_lower_sequence_same_ssrc);
  RUN(test_five_decoder_states_are_independent);
  printf("PASS: %d rtp_sequence host tests\n", tests_run);
  return 0;
}

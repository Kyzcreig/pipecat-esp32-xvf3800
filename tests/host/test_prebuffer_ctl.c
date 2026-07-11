/* Host-side tests for gap-resume accounting and adaptive prebuffer control.
 * Pure C: ./tests/host/run_prebuffer_tests.sh
 */
#include <assert.h>
#include <stdio.h>

#include "prebuffer_ctl.h"

static int tests_run = 0;
#define RUN(fn)                 \
  do {                          \
    fn();                       \
    tests_run++;                \
    printf("ok - %s\n", #fn);  \
  } while (0)

static void test_quick_refill_counts_gap_resume(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 0);
  pbc_on_full_drain(&c, 10000);
  assert(pbc_on_refill(&c, 10200) == 1);
  assert(c.gap_resumes == 1);
}

static void test_slow_refill_is_end_of_utterance(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 0);
  pbc_on_full_drain(&c, 10000);
  assert(pbc_on_refill(&c, 13000) == 0);
  assert(c.gap_resumes == 0);
}

static void test_refill_without_drain_not_counted(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 0);
  assert(pbc_on_refill(&c, 5000) == 0);
  assert(c.gap_resumes == 0);
}

static void test_drain_consumed_once(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 0);
  pbc_on_full_drain(&c, 1000);
  assert(pbc_on_refill(&c, 1100) == 1);
  assert(pbc_on_refill(&c, 1200) == 0);
  assert(c.gap_resumes == 1);
}

static void test_window_boundary_inclusive(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 0);
  pbc_on_full_drain(&c, 1000);
  assert(pbc_on_refill(&c, 1750) == 1);
}

static void test_custom_resume_window(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 200);
  pbc_on_full_drain(&c, 1000);
  assert(pbc_on_refill(&c, 1300) == 0);
  pbc_on_full_drain(&c, 2000);
  assert(pbc_on_refill(&c, 2150) == 1);
  assert(c.gap_resumes == 1);
}

static void test_ms_tick_wraparound(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 0);
  pbc_on_full_drain(&c, 0xFFFFFF00u);
  assert(pbc_on_refill(&c, 0x00000064u) == 1);
  assert(c.gap_resumes == 1);
}

static void test_multiple_gaps_accumulate(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 0);
  for (uint32_t i = 0; i < 5; i++) {
    pbc_on_full_drain(&c, 10000 + i * 2000);
    assert(pbc_on_refill(&c, 10100 + i * 2000) == 1);
  }
  assert(c.gap_resumes == 5);
}

static void test_dark_effective_equals_base(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 0);
  assert(pbc_effective_ms(&c, 40) == 40);
  assert(pbc_effective_ms(&c, 80) == 80);
  assert(pbc_effective_ms(&c, 20) == 20);
  assert(pbc_effective_ms(&c, 1000) == 1000);
}

static void test_dark_never_reacts_to_recoveries(void) {
  prebuffer_ctl c;
  pbc_init(&c, 0, 0);
  pbc_track_recoveries(&c, 0, 0);
  for (uint32_t i = 1; i <= 100; i++) {
    pbc_track_recoveries(&c, i * 10, i * 100);
  }
  assert(c.offset_steps == 0);
  assert(c.transitions == 0);
  assert(pbc_effective_ms(&c, 80) == 80);
}

static void test_adaptive_baseline_snapshot_ignored(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_track_recoveries(&c, 500, 1000);
  assert(c.offset_steps == 0);
  pbc_track_recoveries(&c, 500, 2000);
  assert(c.offset_steps == 0);
}

static void test_adaptive_grows_on_burst(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_track_recoveries(&c, 0, 0);
  pbc_track_recoveries(&c, 5, 1000);
  assert(c.offset_steps == 1);
  assert(c.transitions == 1);
  assert(pbc_effective_ms(&c, 40) == 80);
}

static void test_adaptive_one_step_per_burst(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_track_recoveries(&c, 0, 0);
  pbc_track_recoveries(&c, 7, 1000);
  assert(c.offset_steps == 1);
  pbc_track_recoveries(&c, 8, 1100);
  assert(c.offset_steps == 1);
}

static void test_adaptive_second_burst_grows_again(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_track_recoveries(&c, 0, 0);
  pbc_track_recoveries(&c, 5, 1000);
  assert(pbc_effective_ms(&c, 40) == 80);
  pbc_track_recoveries(&c, 10, 2000);
  assert(pbc_effective_ms(&c, 40) == 120);
  assert(c.transitions == 2);
}

static void test_adaptive_ceiling_clamp(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_track_recoveries(&c, 0, 0);
  for (uint32_t i = 1; i <= 10; i++) {
    pbc_track_recoveries(&c, i * 5, i * 1000);
  }
  assert(pbc_effective_ms(&c, 40) == PBC_MAX_MS);
  assert(c.offset_steps == (PBC_MAX_MS - PBC_MIN_MS) / PBC_STEP_MS);
}

static void test_adaptive_decays_after_quiet(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_track_recoveries(&c, 0, 0);
  pbc_track_recoveries(&c, 5, 1000);
  assert(pbc_effective_ms(&c, 40) == 80);
  pbc_track_recoveries(&c, 5, 1000 + PBC_DECAY_QUIET_MS);
  assert(c.offset_steps == 0);
  assert(pbc_effective_ms(&c, 40) == 40);
  assert(c.transitions == 2);
}

static void test_adaptive_decay_one_step_per_quiet_period(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_track_recoveries(&c, 0, 0);
  pbc_track_recoveries(&c, 5, 1000);
  pbc_track_recoveries(&c, 10, 2000);
  assert(c.offset_steps == 2);
  uint32_t time = 2000 + PBC_DECAY_QUIET_MS;
  pbc_track_recoveries(&c, 10, time);
  assert(c.offset_steps == 1);
  pbc_track_recoveries(&c, 10, time + 1000);
  assert(c.offset_steps == 1);
  pbc_track_recoveries(&c, 10, time + PBC_DECAY_QUIET_MS);
  assert(c.offset_steps == 0);
}

static void test_adaptive_never_below_floor(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_track_recoveries(&c, 0, 0);
  pbc_track_recoveries(&c, 0, PBC_DECAY_QUIET_MS * 4);
  assert(c.offset_steps == 0);
  assert(c.transitions == 0);
  assert(pbc_effective_ms(&c, 20) == PBC_MIN_MS);
}

static void test_adaptive_slow_trickle_never_grows(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_track_recoveries(&c, 0, 0);
  for (uint32_t i = 1; i <= 20; i++) {
    pbc_track_recoveries(&c, i, i * (PBC_WINDOW_MS + 1000));
  }
  assert(c.offset_steps == 0);
  assert(c.transitions == 0);
}

static void test_adaptive_effective_at_default_base_80(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_set_base_ms(&c, 80);
  pbc_track_recoveries(&c, 0, 0);
  assert(pbc_effective_ms(&c, 80) == 80);
  pbc_track_recoveries(&c, 5, 1000);
  assert(pbc_effective_ms(&c, 80) == 120);
  pbc_track_recoveries(&c, 10, 2000);
  assert(pbc_effective_ms(&c, 80) == 160);
  pbc_track_recoveries(&c, 15, 3000);
  assert(pbc_effective_ms(&c, 80) == 160);
  assert(c.offset_steps == 2);
  assert(c.transitions == 2);
}

static void test_adaptive_clamps_runtime_base_before_stepping(void) {
  prebuffer_ctl c;
  pbc_init(&c, 1, 0);
  pbc_set_base_ms(&c, 20);
  pbc_track_recoveries(&c, 0, 0);
  assert(pbc_effective_ms(&c, 20) == 40);
  pbc_track_recoveries(&c, 5, 1000);
  assert(pbc_effective_ms(&c, 20) == 80);

  pbc_set_base_ms(&c, 1000);
  assert(c.offset_steps == 0);
  assert(pbc_effective_ms(&c, 1000) == 160);
}

int main(void) {
  RUN(test_quick_refill_counts_gap_resume);
  RUN(test_slow_refill_is_end_of_utterance);
  RUN(test_refill_without_drain_not_counted);
  RUN(test_drain_consumed_once);
  RUN(test_window_boundary_inclusive);
  RUN(test_custom_resume_window);
  RUN(test_ms_tick_wraparound);
  RUN(test_multiple_gaps_accumulate);
  RUN(test_dark_effective_equals_base);
  RUN(test_dark_never_reacts_to_recoveries);
  RUN(test_adaptive_baseline_snapshot_ignored);
  RUN(test_adaptive_grows_on_burst);
  RUN(test_adaptive_one_step_per_burst);
  RUN(test_adaptive_second_burst_grows_again);
  RUN(test_adaptive_ceiling_clamp);
  RUN(test_adaptive_decays_after_quiet);
  RUN(test_adaptive_decay_one_step_per_quiet_period);
  RUN(test_adaptive_never_below_floor);
  RUN(test_adaptive_slow_trickle_never_grows);
  RUN(test_adaptive_effective_at_default_base_80);
  RUN(test_adaptive_clamps_runtime_base_before_stepping);
  printf("PASS: %d prebuffer_ctl host tests\n", tests_run);
  return 0;
}

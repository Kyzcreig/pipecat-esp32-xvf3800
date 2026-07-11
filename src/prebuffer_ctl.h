/* Playback-ring gap accounting and optional adaptive prebuffer.
 *
 * Pure C, no ESP-IDF dependencies; tests/host/test_prebuffer_ctl.c exercises
 * the state machine on a host compiler.
 *
 * A full drain followed by a fast refill increments gap_resumes, closing the
 * old diagnostic blind spot where only partial-frame drains counted underruns.
 *
 * The adaptive mode is off by default. When enabled, recovery bursts grow the
 * effective prebuffer one step at a time and quiet periods decay it. With the
 * feature off, pbc_effective_ms returns the base unchanged.
 */
#ifndef PREBUFFER_CTL_H
#define PREBUFFER_CTL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PBC_STEP_MS
#define PBC_STEP_MS 40u
#endif
#ifndef PBC_MIN_MS
#define PBC_MIN_MS 40u
#endif
#ifndef PBC_MAX_MS
#define PBC_MAX_MS 160u
#endif
#ifndef PBC_WINDOW_MS
#define PBC_WINDOW_MS 10000u
#endif
#ifndef PBC_RATE_THRESHOLD
#define PBC_RATE_THRESHOLD 5u
#endif
#ifndef PBC_DECAY_QUIET_MS
#define PBC_DECAY_QUIET_MS 30000u
#endif

typedef struct {
  uint32_t resume_window_ms;
  uint32_t drain_at_ms;
  int drain_pending;
  uint32_t gap_resumes;

  int adaptive;
  uint32_t base_ms;
  uint32_t offset_steps;
  uint32_t transitions;
  uint32_t window_start_ms;
  uint32_t window_events;
  uint32_t last_recovery_ms;
  uint32_t last_recovery_total;
  int have_baseline;
} prebuffer_ctl;

void pbc_init(prebuffer_ctl *c, int adaptive, uint32_t resume_window_ms);
void pbc_on_full_drain(prebuffer_ctl *c, uint32_t now_ms);
int pbc_on_refill(prebuffer_ctl *c, uint32_t now_ms);
void pbc_set_base_ms(prebuffer_ctl *c, uint32_t base_ms);
void pbc_track_recoveries(prebuffer_ctl *c, uint32_t cumulative_recoveries,
                          uint32_t now_ms);
uint32_t pbc_effective_ms(const prebuffer_ctl *c, uint32_t base_ms);

#ifdef __cplusplus
}
#endif

#endif /* PREBUFFER_CTL_H */

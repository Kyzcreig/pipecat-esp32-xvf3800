/* prebuffer_ctl.c — see prebuffer_ctl.h. Pure C, host-testable. */
#include "prebuffer_ctl.h"

#include <string.h>

#define PBC_DEFAULT_RESUME_WINDOW_MS 750u

static uint32_t pbc_clamp_base(uint32_t base_ms) {
  if (base_ms < PBC_MIN_MS) return PBC_MIN_MS;
  if (base_ms > PBC_MAX_MS) return PBC_MAX_MS;
  return base_ms;
}

static uint32_t pbc_max_steps(uint32_t base_ms) {
  uint32_t clamped = pbc_clamp_base(base_ms);
  return (PBC_MAX_MS - clamped + PBC_STEP_MS - 1) / PBC_STEP_MS;
}

void pbc_init(prebuffer_ctl *c, int adaptive, uint32_t resume_window_ms) {
  memset(c, 0, sizeof(*c));
  c->adaptive = adaptive ? 1 : 0;
  c->resume_window_ms =
      resume_window_ms ? resume_window_ms : PBC_DEFAULT_RESUME_WINDOW_MS;
  c->base_ms = PBC_MIN_MS;
}

void pbc_on_full_drain(prebuffer_ctl *c, uint32_t now_ms) {
  c->drain_pending = 1;
  c->drain_at_ms = now_ms;
}

int pbc_on_refill(prebuffer_ctl *c, uint32_t now_ms) {
  if (!c->drain_pending) {
    return 0;
  }
  c->drain_pending = 0;
  if ((uint32_t)(now_ms - c->drain_at_ms) <= c->resume_window_ms) {
    c->gap_resumes++;
    return 1;
  }
  return 0;
}

void pbc_set_base_ms(prebuffer_ctl *c, uint32_t base_ms) {
  c->base_ms = pbc_clamp_base(base_ms);
  uint32_t max_steps = pbc_max_steps(c->base_ms);
  if (c->offset_steps > max_steps) {
    c->offset_steps = max_steps;
  }
}

void pbc_track_recoveries(prebuffer_ctl *c, uint32_t cumulative_recoveries,
                          uint32_t now_ms) {
  if (!c->have_baseline) {
    c->have_baseline = 1;
    c->last_recovery_total = cumulative_recoveries;
    c->window_start_ms = now_ms;
    c->last_recovery_ms = now_ms;
    return;
  }

  uint32_t delta = cumulative_recoveries - c->last_recovery_total;
  c->last_recovery_total = cumulative_recoveries;

  if (!c->adaptive) {
    return;
  }

  if (delta > 0) {
    c->window_events += delta;
    c->last_recovery_ms = now_ms;
  }

  if ((uint32_t)(now_ms - c->window_start_ms) >= PBC_WINDOW_MS) {
    c->window_start_ms = now_ms;
    c->window_events = delta > 0 ? delta : 0;
  }

  if (c->window_events >= PBC_RATE_THRESHOLD) {
    uint32_t max_steps = pbc_max_steps(c->base_ms);
    if (c->offset_steps < max_steps) {
      c->offset_steps++;
      c->transitions++;
    }
    c->window_events = 0;
    c->window_start_ms = now_ms;
  }

  if (c->offset_steps > 0 &&
      (uint32_t)(now_ms - c->last_recovery_ms) >= PBC_DECAY_QUIET_MS) {
    c->offset_steps--;
    c->transitions++;
    c->last_recovery_ms = now_ms;
  }
}

uint32_t pbc_effective_ms(const prebuffer_ctl *c, uint32_t base_ms) {
  if (!c->adaptive) {
    return base_ms;
  }
  uint32_t effective =
      pbc_clamp_base(base_ms) + c->offset_steps * PBC_STEP_MS;
  if (effective > PBC_MAX_MS) effective = PBC_MAX_MS;
  return effective;
}

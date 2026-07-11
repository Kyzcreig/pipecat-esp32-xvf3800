#include "rtp_sequence.h"

#include <string.h>

void rtp_sequence_init(RtpSequenceState* state) {
  if (state != NULL) {
    memset(state, 0, sizeof(*state));
  }
}

static void accept_packet(RtpSequenceState* state, uint16_t sequence_number,
                          uint32_t timestamp, uint32_t ssrc) {
  state->last_seq = sequence_number;
  state->last_timestamp = timestamp;
  state->ssrc = ssrc;
  state->initialized = 1;
}

RtpSequenceEvent rtp_sequence_update(RtpSequenceState* state,
                                     uint16_t sequence_number,
                                     uint32_t timestamp, uint32_t ssrc) {
  RtpSequenceEvent event = {
      .kind = RTP_SEQUENCE_FIRST,
      .gap = 0,
      .previous_timestamp = 0,
  };
  if (state == NULL) {
    return event;
  }

  if (!state->initialized) {
    accept_packet(state, sequence_number, timestamp, ssrc);
    return event;
  }

  if (state->ssrc != ssrc) {
    event.kind = RTP_SEQUENCE_STREAM_RESET;
    accept_packet(state, sequence_number, timestamp, ssrc);
    return event;
  }

  event.previous_timestamp = state->last_timestamp;
  uint16_t expected = (uint16_t)(state->last_seq + 1u);
  int16_t delta = (int16_t)(sequence_number - expected);
  if (delta < 0) {
    event.kind = RTP_SEQUENCE_LATE;
    return event;
  }

  event.kind = delta == 0 ? RTP_SEQUENCE_NEXT : RTP_SEQUENCE_GAP;
  event.gap = delta;
  accept_packet(state, sequence_number, timestamp, ssrc);
  return event;
}

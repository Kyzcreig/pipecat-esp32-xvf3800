#ifndef RTP_SEQUENCE_H_
#define RTP_SEQUENCE_H_

#include <stdint.h>

typedef enum RtpSequenceKind {
  RTP_SEQUENCE_FIRST = 0,
  RTP_SEQUENCE_NEXT,
  RTP_SEQUENCE_GAP,
  RTP_SEQUENCE_LATE,
  RTP_SEQUENCE_STREAM_RESET,
} RtpSequenceKind;

typedef struct RtpSequenceState {
  uint16_t last_seq;
  uint32_t last_timestamp;
  uint32_t ssrc;
  uint8_t initialized;
} RtpSequenceState;

typedef struct RtpSequenceEvent {
  RtpSequenceKind kind;
  int16_t gap;
  uint32_t previous_timestamp;
} RtpSequenceEvent;

void rtp_sequence_init(RtpSequenceState* state);

RtpSequenceEvent rtp_sequence_update(RtpSequenceState* state,
                                     uint16_t sequence_number,
                                     uint32_t timestamp, uint32_t ssrc);

#endif  // RTP_SEQUENCE_H_

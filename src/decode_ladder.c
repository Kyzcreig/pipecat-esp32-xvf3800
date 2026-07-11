/* decode_ladder.c — see decode_ladder.h. Pure C, host-testable. */
#include "decode_ladder.h"

void dl_init(decode_ladder *ladder) { ladder->pending_gaps = 0; }

int dl_on_gap(decode_ladder *ladder) {
  if (ladder->pending_gaps >= DL_MAX_PENDING_GAPS) {
    return -1;
  }
  ladder->pending_gaps++;
  return 0;
}

size_t dl_on_frame(decode_ladder *ladder, decode_ladder_op *operations,
                   size_t capacity) {
  size_t required = (size_t)ladder->pending_gaps + 1;
  if (operations == NULL || capacity < required) {
    return 0;
  }

  size_t count = 0;
  while (ladder->pending_gaps > 1) {
    operations[count++] = DL_OP_PLC;
    ladder->pending_gaps--;
  }
  if (ladder->pending_gaps == 1) {
    operations[count++] = DL_OP_FEC_ATTEMPT;
    ladder->pending_gaps = 0;
  }
  operations[count++] = DL_OP_FRAME;
  return count;
}

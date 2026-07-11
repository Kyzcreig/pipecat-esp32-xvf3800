/* Pure control plane for chronological Opus gap recovery.
 *
 * A real frame may be either the current RTP primary or a complete Opus frame
 * recovered from RFC 2198 RED. In both cases it is the correct next
 * chronological frame and therefore the FEC opportunity for one immediately
 * preceding uncovered gap. This module makes that callback contract explicit
 * and host-testable without linking Opus or ESP-IDF.
 */
#ifndef DECODE_LADDER_H
#define DECODE_LADDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DL_MAX_PENDING_GAPS 16u

typedef enum {
  DL_OP_PLC = 0,
  DL_OP_FEC_ATTEMPT = 1,
  DL_OP_FRAME = 2,
} decode_ladder_op;

typedef struct {
  uint32_t pending_gaps;
} decode_ladder;

void dl_init(decode_ladder *ladder);
int dl_on_gap(decode_ladder *ladder);

/* Return the chronological operations for the next real Opus frame.
 * The caller needs capacity for pending_gaps + 1 operations. A short output
 * buffer returns zero without consuming state. */
size_t dl_on_frame(decode_ladder *ladder, decode_ladder_op *operations,
                   size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* DECODE_LADDER_H */

/* red_unwrap.h — RFC 2198 (RED) depacketizer for the audio downlink.
 *
 * A matching server can wrap each Opus frame with complete copies of the two
 * preceding frames. This module parses that payload and plans lossless recovery
 * of bursts up to N-2, beyond the single-frame reach of Opus in-band FEC.
 *
 * This is pure byte logic with no ESP-IDF or libpeer dependency. rtp.c owns
 * sequence classification, counters, and decoder feed order.
 *
 * Safety rule: any parse failure zeroes the parsed output. RED framing must not
 * reach the Opus decoder as though it were a bare PT 111 payload.
 *
 * RFC 2198 payload layout:
 *   redundant header (4 bytes): |F=1|block PT(7)|ts offset(14)|length(10)|
 *   primary header   (1 byte):  |F=0|block PT(7)|
 */
#ifndef RED_UNWRAP_H_
#define RED_UNWRAP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Must match the SDP offer and the sender's dynamic payload type. */
#define RED_PAYLOAD_TYPE 63

/* The documented sender profile uses N-2; keep bounded N-3/N-4 headroom. */
#define RED_MAX_BLOCKS 4

#if defined(__cplusplus)
static_assert(RED_MAX_BLOCKS <= INT8_MAX,
              "RED block index must fit the signed action sentinel");
#else
_Static_assert(RED_MAX_BLOCKS <= INT8_MAX,
               "RED block index must fit the signed action sentinel");
#endif

/* Matches rtp.c's existing maximum concealment burst. */
#define RED_MAX_GAP 16

/* Supported downlink profile: 20 ms Opus on a 48 kHz RTP clock. */
#define RED_OPUS_TS_STEP_20MS 960u

typedef struct RedBlock {
  uint16_t ts_offset;
  uint16_t length;
  const uint8_t* data;
} RedBlock;

typedef struct RedParsed {
  int block_count;
  RedBlock blocks[RED_MAX_BLOCKS];
  const uint8_t* primary;
  size_t primary_size;
} RedParsed;

/* Parse one RFC 2198 payload. expected_pt is the Opus payload type carried by
 * every block. Returns 0 on success and -1 with a zeroed out parameter on any
 * malformation. */
int red_unwrap(const uint8_t* payload, size_t size, uint8_t expected_pt,
               RedParsed* out);

/* Validate that a forward gap stayed on the fixed 20 ms sender profile.
 * Timestamp subtraction is intentionally uint32_t so RTP wraparound works.
 * Returns 0 and writes expected_step on an exact match; otherwise -1. */
int red_validate_timestamp_advance(uint32_t last_ts, uint32_t current_ts,
                                   int gap, uint32_t expected_step,
                                   uint32_t* validated_step);

/* Plan recovery for gap missing packets, oldest first. actions[i] >= 0 selects
 * parsed->blocks[actions[i]]; -1 retains the PLC/FEC fallback. ts_step is the
 * validated RTP timestamp increment per packet. */
int red_recover_plan(const RedParsed* parsed, int gap, uint32_t ts_step,
                     int8_t actions[RED_MAX_GAP]);

#ifdef __cplusplus
}
#endif

#endif /* RED_UNWRAP_H_ */

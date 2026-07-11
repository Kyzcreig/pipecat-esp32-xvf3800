# Playback resilience

This firmware treats audio quality as a pipeline, not a single codec knob.

## Data path

```text
WebRTC/SRTP packet
  -> vendored libpeer RTP decoder
  -> RFC 2198 depacketizer (when payload type 63 is negotiated)
  -> sequence classification (next / gap / late)
  -> RED block, Opus FEC, or PLC recovery
  -> 16 kHz mono PSRAM ring
  -> dedicated FreeRTOS playback task
  -> 3x polyphase windowed-sinc interpolation
  -> 48 kHz stereo 32-bit I2S
  -> XVF3800/AIC3104
```

## Sequence handling

The upstream generic RTP decoder did not use sequence numbers. This fork embeds sequence state in each `RtpDecoder`:

- expected packet: decode normally;
- packet behind expected: count `late_drops` and discard it because playback has moved on;
- forward gap of 1..16 packets: count one `gap_events` event and signal one missing frame per sequence number;
- larger jump: resynchronize instead of synthesizing more than about 320 ms.

`rtp_decoder_init()` clears the state for every new connection lifecycle, and an SSRC change starts a new epoch immediately. No process-global pointer table survives decoder destruction or allocator address reuse. The seven host sequence tests cover reconnect to a lower random sequence, same-address reinitialization, SSRC replacement, wraparound, late packets, gaps, and five independent decoder instances.

For RED packets, the decoder maps missing timestamps to redundant blocks and feeds matching copies oldest-first. A frame without a matching RED block arrives at the audio callback as `(NULL, 0)`; the callback can then defer a single-frame gap for Opus FEC and use PLC for the remainder.

## RED, FEC, and PLC ladder

Recovery order is:

1. an RFC 2198 block whose timestamp offset exactly matches the missing frame;
2. Opus in-band FEC from the following chronological Opus frame for one uncovered gap;
3. Opus PLC for any remaining missing frames.

RED and in-band FEC recover encoded content; PLC synthesizes continuity from decoder state. RED blocks are fed in timestamp order before the new primary so the Opus decoder state advances coherently. If an older gap is not covered by RED, the next recovered RED block is itself the correct chronological packet from which to attempt in-band FEC before decoding that block normally. A late or duplicate RED packet is dropped wholesale; feeding its redundant blocks after later audio would corrupt decoder order.

The `decode_ladder` control module owns this chronological callback contract. Its host tests compose real `red_recover_plan` actions with both partial-coverage orders and assert one recovery operation per missing timeslot plus one normal decode per real frame. This keeps RED planning and Opus FEC/PLC interpretation from drifting independently.

The RED parser is fail-closed. Truncated headers, foreign payload types, impossible offsets, oversized block declarations, and empty primaries all return an error with a zeroed output. The receive path counts `red_parse_failures` and submits a `(NULL, 0)` loss signal; it never feeds malformed RED framing to Opus as though it were bare Opus. Independently negotiated PT 111 packets continue down the plain-Opus path.

The sender must separately enable Opus in-band FEC and RED encapsulation. Device-side support cannot create redundancy that the sender did not transmit.

## Ring-buffered I2S

Writing I2S directly from the WebRTC callback couples DMA timing to network arrival, ICE/RTVI work, and decoder scheduling. Real speech then develops micro-gaps even when a perfectly paced tone sounds clean.

The firmware instead uses:

- 32,768 int16 samples (about 2.05 seconds, 64 KiB) in PSRAM with internal-memory fallback;
- a single producer on the WebRTC thread;
- a consumer task pinned to core 1 at priority 6;
- 20 ms output frames paced by blocking `i2s_channel_write()`;
- occupancy-based prebuffering, not an `is_playing` edge that can retrigger mid-word.

Default prebuffer is 80 ms. Two 20 ms RED copies can arrive up to 40 ms behind their originals; a 40 ms ring leaves no scheduler margin. `GET /playback/stats?prebuffer_ms=N` changes the base at runtime for 20..1000 ms. More buffering tolerates jitter but adds reply latency.

`PIPECAT_ADAPTIVE_PREBUFFER=1` enables an optional controller that grows the effective buffer in 40 ms steps after five recovery events in a 10-second window and decays one step after 30 quiet seconds. Bounds are 40..160 ms. The default is off; when off, the controller returns the configured base unchanged. `PIPECAT_GAP_RESUME_MS` controls the default 750 ms window used to classify full-drain/refill cycles as mid-speech gaps.

## Sample-rate conversion

### Capture: 48 kHz to 16 kHz

The old peak-picker was nonlinear and did not low-pass before decimation. It folded high-frequency energy into the speech band. The mono path now averages both ASR slots and applies a simple linear low-pass/decimation over the 3:1 input ratio. Dual-stream mode keeps each lane separate.

### Playback: 16 kHz to 48 kHz

Zero-order hold caused obvious spectral images. Linear interpolation improved it; the production path uses a 24-tap Hamming-windowed sinc in three 8-tap polyphase branches.

The coefficients are scaled by 0.668. That reduces output about 3.5 dB but keeps the proven worst-case accumulated magnitude under int16 full scale. Do not restore unity coefficient sums without rerunning the generator's headroom proof.

## Diagnostics

`GET /playback/stats` returns cumulative counters:

| Field | Meaning |
|---|---|
| `frames` | 20 ms frames written by the playback task |
| `write_fail` | I2S writes that returned an error |
| `underruns` | partial ring frames stranded before refill |
| `plc` | explicit null-packet Opus PLC calls |
| `fec_attempts` | single-gap calls made with `decode_fec=1`; Opus may use FEC or internal concealment |
| `ptime_mismatches` | Opus payloads rejected because they were not exactly one 20 ms frame |
| `late_drops` | reordered/duplicate RTP packets arriving behind playback |
| `gap_events` | distinct forward sequence gaps |
| `stream_resets` | SSRC changes that started a fresh RTP sequence epoch |
| `packets_received` | all audio RTP packets presented to the generic decoder |
| `red_recovered` | missing frames restored from exact timestamp-matched RED blocks |
| `red_dup_drops` | late/duplicate RED packets whose redundancy was intentionally ignored |
| `red_parse_failures` | malformed PT 63 payloads rejected before Opus decode |
| `red_profile_mismatches` | RED gap packets rejected because timestamp advance was not fixed 20 ms Opus |
| `gap_resumes` | full ring drains followed by a refill inside the resume window |
| `prebuffer_ms` | configured base occupancy threshold |
| `prebuffer_effective_ms` | threshold after optional adaptive adjustment |
| `prebuffer_transitions` | cumulative adaptive growth/decay steps |

Interpretation:

- audible defect + rising underruns: delivery timing/prebuffer problem;
- audible defect + rising gap events/PLC/FEC attempts: network loss or sender redundancy problem;
- rising gap events with rising `red_recovered`: RED is actively restoring loss;
- RED negotiated but `red_recovered` remains zero during induced loss: check sender encapsulation, payload type 63, and timestamp offsets;
- rising `red_parse_failures`: the sender selected PT 63 but emitted malformed RFC 2198 framing;
- rising `ptime_mismatches`: the sender ignored the negotiated 20 ms packetization profile;
- rising `red_profile_mismatches`: sender ptime changed or RTP timestamps left the supported 960-tick cadence;
- rising `gap_resumes`: the ring is fully draining mid-speech even if `underruns` remains zero;
- late drops without gaps: reordering exceeds what this no-jitter-buffer RTP layer tolerates;
- clean counters + bad `/playback/selftest`: investigate FIR, I2S, codec registers, analog stage, speaker;
- clean self-test + bad streamed audio: investigate Opus, RTP, frame boundaries, or server pacing.

Opus deliberately hides whether a `decode_fec=1` call found in-band FEC: when no FEC exists it returns a concealed frame rather than a distinct "no FEC" status. For that reason this firmware reports `fec_attempts`, not a dishonest FEC-success count.

`POST /playback/selftest` queues a flash-embedded 16 kHz mono PCM clip through the ring, FIR, I2S, codec, and speaker while bypassing network and Opus.

## Decoder reset guard

RTVI `bot-started-speaking` can arrive after the first audio packet. Resetting Opus at that point damages the first syllable. The decoder reset is therefore allowed only when the ring is empty and playback is idle.

## RED sender contract

The firmware offer advertises Opus PT 111 and RED PT 63:

```text
m=audio 9 UDP/TLS/RTP/SAVP 111 63
a=rtpmap:111 opus/48000/2
a=rtpmap:63 red/48000/2
a=fmtp:63 111/111/111
a=ptime:20
```

The three slash-separated payload-type entries negotiate two redundant generations followed by the primary. The implemented profile is fixed 20 ms Opus at a 48 kHz RTP clock, and the offer explicitly advertises `a=ptime:20`. A steady-state RED packet contains the full N-2 block (timestamp offset 1920), full N-1 block (offset 960), then the current primary. Before selecting those blocks, the receiver requires the RTP timestamp advance across a gap to equal exactly `960 * (missing + 1)`; mixed ptime or a discontinuity increments `red_profile_mismatches` and falls back to FEC/PLC. Every real Opus payload is also checked with `opus_packet_get_nb_samples`; a sender that ignores `ptime:20` is unsupported, increments `ptime_mismatches`, and is converted to a loss signal instead of being decoded into a fixed 20 ms buffer. The sender may select plain Opus instead; the receiver remains compatible. `tests/host/run_red_tests.sh` compiles 25 byte-level, timestamp-profile, and malformed-input cases without ESP-IDF.

This repository contains the receive half. End-to-end RED requires a sender that selects PT 63 and emits matching RFC 2198 payloads; the static SDP offer alone is not proof that redundancy is on the wire.

## Rejected retransmit experiment

An experimental NACK lane requested bursts over the existing reliable ordered data channel. Instrumentation showed the resend path itself worked, but loss episodes caused head-of-line blocking and measured request-to-resend latency of roughly 4.3–4.7 seconds—far outside an 80 ms playback budget. The lane was therefore excluded from this public firmware. A future attempt needs unordered delivery or RTCP-native feedback; increasing the wait window would only turn packet recovery into seconds of user-visible latency.

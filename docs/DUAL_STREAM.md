# Optional dual-stream capture

`PIPECAT_DUAL_STREAM` is disabled by default. The normal build keeps a mono ASR path: both XVF3800 output slots are `[7,3]`, the ESP32 averages the slots during 48-to-16 kHz decimation, and Opus uses one channel at 30 kbit/s.

Enable the experiment at CMake configure time:

```sh
export PIPECAT_DUAL_STREAM=1
idf.py reconfigure
idf.py build
```

## Channel contract

| Opus channel | I2S slot | XVF mux | Consumer |
|---:|---|---|---|
| 0 | left | `[7,3]` ASR auto-select beam | VAD/STT |
| 1 | right | `[6,3]` post-processed auto-select beam | wake/barge scoring |

`AEC ASROUTONOFF=1` remains required for channel 0. Both slots are explicitly upsampled onto the existing 48 kHz stereo I2S bus. The ESP32 decimates the slots independently and feeds interleaved 16 kHz stereo to a two-channel Opus encoder.

The server must negotiate/decode stereo Opus and split channels immediately. Send only the left lane to STT and only the right lane to wake/barge scoring. Mixing or downmixing loses the reason the mode exists.

## Cost

The nominal Opus target rises from 30 to 60 kbit/s before RTP/SRTP/UDP/IP overhead. Uncompressed input rises from 256 to 512 kbit/s. I2S wire rate is unchanged because the board link is already 48 kHz stereo 32-bit.

## Rollback and gates

Unset `PIPECAT_DUAL_STREAM`, run `idf.py reconfigure`, and rebuild. The host-side gates are:

```sh
python3 scripts/dualstream_lint.py
python3 scripts/asr_routing_lint.py
```

Before flashing a flag-on image, prove the matching server split independently. Expected firmware readback is `[7,3]` left, `[6,3]` right, upsample `[1,1]`, and ASR output enabled. Default mode should read `[7,3]` on both slots.

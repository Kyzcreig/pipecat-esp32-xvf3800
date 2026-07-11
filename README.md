# pipecat-esp32 for ReSpeaker XVF3800 + XIAO ESP32-S3

A hardware-specific Pipecat/WebRTC firmware and field guide for the Seeed ReSpeaker XVF3800 USB 4-Mic Array with its XIAO ESP32-S3 module.

> **Mic audio is rail-pinned, loud in silence, or complete garbage?** The XVF3800 is the I2S clock **master** on the tested DFU image. The ESP32 must use `I2S_ROLE_SLAVE`. Configuring both chips as masters makes them fight BCLK/WS and produces bit-smeared samples. See [The I2S role inversion](#the-i2s-role-inversion) before changing gain, VAD, or wake thresholds.

This repository is a focused, buildable extraction of [`pipecat-ai/pipecat-esp32`](https://github.com/pipecat-ai/pipecat-esp32), not a replacement for the upstream multi-board SDK. It contains the complete XIAO/XVF3800 target, the production dependency subset, regression guards, and the register-level lessons that made the board usable for ASR and full-duplex playback.

## What works

Tested with ESP-IDF 5.5.4 and XVF3800 DFU firmware v6.34.4:

- 16 kHz Opus microphone audio over SmallWebRTC.
- Correct XVF3800-as-master / ESP32-as-slave I2S clocking.
- Clean ASR beam routing: `AUDIO_MGR OP_L=[7,3]` and `AEC ASROUTONOFF=1`.
- Optional dual-stream capture: clean ASR lane plus post-processed wake/barge lane.
- 16-to-48 kHz polyphase windowed-sinc playback with proven FIR headroom.
- Dedicated PSRAM playback ring and I2S writer task, decoupled from WebRTC packet timing.
- RTP sequence-gap detection, late-packet rejection, Opus PLC/in-band FEC, and RFC 2198 receive-side N-2 recovery when the sender negotiates RED payload type 63.
- An 80 ms static playback prebuffer, full-drain gap accounting, and optional adaptive 80/120/160 ms control.
- XVF3800 LED-ring effects driven by local state and RTVI phases.
- Wi-Fi/WebRTC reconnect watchdogs, application-level RTVI ping/pong, and a 30-second libpeer keepalive timeout.
- HTTP OTA with SHA-256 reporting, health validation, and automatic rollback.
- Runtime DSP tuning and playback diagnostics over HTTP.

The optional dual-stream mode requires a server that negotiates stereo Opus and splits the lanes correctly. The default build remains mono.

## Hardware

- Seeed ReSpeaker XVF3800 USB 4-Mic Array
- Seeed Studio XIAO ESP32-S3 with PSRAM
- XVF3800 DFU image known to drive BCLK and WS as I2S master; v6.34.4 is the tested version
- Optional speaker on the board's speaker output

The firmware uses the board wiring already present on the ReSpeaker assembly:

| Signal | ESP32-S3 GPIO |
|---|---:|
| I2S BCLK | 8 |
| I2S WS/LRCLK | 7 |
| I2S data from XVF3800 | 43 |
| I2S data to XVF3800 | 44 |
| I2C SDA | 5 |
| I2C SCL | 6 |

## Quick start

1. Install the pinned toolchain and prepare `.env` by following [`docs/BUILD_AND_FLASH.md`](docs/BUILD_AND_FLASH.md).
2. Run the static regression guards:

   ```sh
   python3 scripts/i2s_role_lint.py
   python3 scripts/asr_routing_lint.py
   python3 scripts/dualstream_lint.py
   python3 scripts/publication_gate.py
   python3 scripts/check_docs.py
   python3 -m unittest discover -s tests
   ./tests/host/run_red_tests.sh
   ./tests/host/run_rtp_sequence_tests.sh
   ./tests/host/run_decode_ladder_tests.sh
   ./tests/host/run_prebuffer_tests.sh
   ```

3. Configure and build:

   ```sh
   set -a
   . ./.env
   set +a
   idf.py set-target esp32s3
   idf.py reconfigure
   python3 scripts/verify_build_config.py
   idf.py build
   ```

4. Identify the USB-connected board before flashing, then flash:

   ```sh
   cp devices.example.json devices.json
   # Replace the example device id and MAC in devices.json.
   FLASH_ASSERT_DEVICE=my-satellite scripts/flash.sh flash monitor
   ```

`devices.json`, `.env`, generated configuration, build output, and firmware binaries are ignored. Do not commit them.

## The I2S role inversion

The original target configured the ESP32 as `I2S_ROLE_MASTER`. The tested XVF3800 image already drives BCLK/WS. With both ends driving the bus, silence measured roughly RMS 13,600 with the int16 peak pinned at 32,768; adding sound barely changed it. That signature is clock-fight garbage, not a hot microphone.

Changing the ESP32 to `I2S_ROLE_SLAVE` moved quiet capture to roughly RMS 14 and made real acoustic input track sound level. `scripts/i2s_role_lint.py` fails closed if the role changes or the source layout becomes unparseable. `scripts/mic_sanity_check.py` is the runtime companion for server-side inbound-RMS logs.

## The second critical fix: route the ASR beam

The XVF3800 is a conferencing DSP. Its voice-communication output is optimized for human listening and can remove speech detail that ASR needs. The mono ESP32 path consumes the left I2S slot, so boot configuration must set:

- `AUDIO_MGR OP_L = [7,3]`: category 7, ASR, automatic beam selection.
- `AEC ASROUTONOFF = 1`: enable the ASR output.

Category 6 is useful as an independently preserved wake/barge lane, but it is not the default STT lane. These values are volatile on the XMOS side and must be written after every power cycle. See [`docs/XVF3800_REGISTER_COOKBOOK.md`](docs/XVF3800_REGISTER_COOKBOOK.md).

## Playback resilience

The playback path is deliberately layered:

```text
SRTP/RTP -> RED depacketizer -> sequence-gap detector -> RED/FEC/PLC recovery
         -> 16 kHz ring buffer -> dedicated playback task
         -> headroom-scaled 3x FIR -> 48 kHz I2S -> XVF3800/AIC3104 -> speaker
```

The ring separates network jitter from DMA timing. The FIR avoids zero-order-hold imaging and is scaled so worst-case interpolation remains below int16 full scale. For negotiated RED, each packet can carry N-1 and N-2 copies; matching redundant blocks are fed in timestamp order before the primary. Uncovered single gaps can use Opus in-band FEC, and deeper gaps fall back to PLC. Details, sender requirements, and diagnostic interpretation are in [`docs/PLAYBACK_RESILIENCE.md`](docs/PLAYBACK_RESILIENCE.md).

## HTTP diagnostics and OTA

The device exposes an unauthenticated HTTP server on port 80:

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/ota/status` | Running slot, OTA state, SHA-256, uptime, firmware/device identity |
| `POST` | `/ota/upload` | Stream an application image to the next OTA partition and reboot |
| `POST` | `/ota/rollback` | Mark the running image invalid and reboot to the previous image |
| `POST` | `/xvf/tune?param=...&value=...` | Volatile allowlisted XVF/AIC3104 tuning |
| `GET` | `/playback/stats` | Ring, I2S, PLC/FEC, loss, and prebuffer counters |
| `POST` | `/playback/selftest` | Play an embedded clip without network or Opus |

**Security boundary:** there is no authentication or TLS. Keep the device on a trusted/isolated LAN and block port 80 from untrusted networks. Do not expose these endpoints to the internet. See [`docs/HTTP_API.md`](docs/HTTP_API.md).

## Documentation

- [`docs/BUILD_AND_FLASH.md`](docs/BUILD_AND_FLASH.md) — pinned IDF setup, Python-environment trap, CMake cache trap, USB and OTA flow.
- [`docs/XVF3800_REGISTER_COOKBOOK.md`](docs/XVF3800_REGISTER_COOKBOOK.md) — control protocol, RESIDs, commands, boot profile, and tuning method.
- [`docs/AEC_GAIN_STAGING_POSTMORTEM.md`](docs/AEC_GAIN_STAGING_POSTMORTEM.md) — why the vendor sample's linear `8.0` reference gain was an 8x overdrive.
- [`docs/PLAYBACK_RESILIENCE.md`](docs/PLAYBACK_RESILIENCE.md) — RED/FEC/PLC, resampler, ring, counters, and rejected retransmit experiments.
- [`docs/DUAL_STREAM.md`](docs/DUAL_STREAM.md) — optional two-lane ASR plus wake/barge contract.
- [`docs/HTTP_API.md`](docs/HTTP_API.md) — exact endpoint behavior and safety notes.
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — vendored dependency provenance and licenses.

## Regression guards

- `i2s_role_lint.py`: ESP32 remains the I2S slave; parse failure is an error.
- `asr_routing_lint.py`: left slot remains category 7 and ASR mode stays enabled.
- `dualstream_lint.py`: optional stereo lane and CMake/header gates stay coherent.
- `device_identity.py`: USB MAC must match an operator-owned local registry before flash.
- `publication_gate.py`: refuses private addresses, known local identifiers, secret files, build artifacts, and unsafe key files.
- `check_docs.py`: resolves every repository-relative Markdown link.
- `verify_build_config.py`: checks that CMake baked the current environment values without printing secrets.
- `tests/host/run_red_tests.sh`: 25 parser/recovery tests, including fail-closed malformed packets, fixed-profile timestamp validation, wraparound, and N-1/N-2 golden vectors.
- `tests/host/run_rtp_sequence_tests.sh`: 7 stream-epoch tests, including reconnect reinitialization, SSRC replacement, sequence wraparound, and five independent decoder lifecycles.
- `tests/host/run_decode_ladder_tests.sh`: 8 planner-to-decoder control tests, including both partial RED coverage orders and the exact interleaved callback stream.
- `tests/host/run_prebuffer_tests.sh`: 21 gap-resume and adaptive-controller tests, including wraparound, dark-mode invariance, growth, decay, runtime base changes, and clamps.

## Provenance and upstream candidates

The base firmware comes from `pipecat-ai/pipecat-esp32`. The `components/peer` wrapper vendors the production `libpeer` source needed by this target, overrides `peer_connection.c` and `rtp.c`, and extends the vendored `RtpDecoder` structure for per-stream sequence state. The clean upstream candidates are:

- configurable libpeer keepalive timeout and reconnect semantics;
- RTP sequence-gap signaling for Opus PLC;
- Opus in-band FEC decode on the packet following a single-frame gap.
- RFC 2198 offer/depacketization as a generic receive-side option.

Hardware-specific XVF3800 routing, AEC gain staging, LED behavior, and I2S pins belong here rather than in generic upstream defaults.

## License

MIT for this repository's original and inherited pipecat-esp32 code. Vendored dependencies retain their own notices; see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). This project is not affiliated with or endorsed by Seeed, XMOS, or Pipecat.

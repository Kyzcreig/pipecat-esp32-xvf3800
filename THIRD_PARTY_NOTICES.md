# Third-party notices

This repository is a focused extraction of the MIT-licensed [`pipecat-ai/pipecat-esp32`](https://github.com/pipecat-ai/pipecat-esp32) project with hardware-specific modifications. The repository root `LICENSE` retains that MIT license.

Production dependency sources are vendored so the XIAO/XVF3800 target is buildable without nested Git submodules.

| Component | Upstream | Vendored revision | License file |
|---|---|---|---|
| libpeer production `src/` | [`aconchillo/libpeer`](https://github.com/aconchillo/libpeer) | `9319aa434cb9e893faed0293ba9d2a21eca59c8b` | `components/peer/libpeer/LICENSE` (MIT) |
| ESP-IDF SRTP port + production libsrtp subset | [`sepfy/esp_ports`](https://github.com/sepfy/esp_ports) | `f39a4a2c70a4b7d09f9f852a58b5ea39b9c1182c` | `components/srtp/LICENSE` (MIT), `components/srtp/libsrtp/LICENSE` (BSD-style) |
| esp-libopus | [`XasWorks/esp-libopus`](https://github.com/XasWorks/esp-libopus) | `260b16cc540285e84220c709de1bb2796ea7ec41` | `components/esp-libopus/COPYING` (3-clause BSD-style plus patent notices) |

## Local libpeer overrides

`components/peer/CMakeLists.txt` compiles the vendored libpeer production source but excludes its `peer_connection.c` and `rtp.c`. The repository supplies reviewed local versions at:

- `components/peer/peer_connection.c`
- `components/peer/rtp.c`

The overrides add the target's reconnect behavior, RTP sequence-gap classification, late-packet dropping, RFC 2198 negotiation/depacketization, and missing-frame signals used by RED/Opus FEC/PLC recovery. The vendored `libpeer/src/rtp.h` has one corresponding local change: each `RtpDecoder` embeds repository-local `RtpSequenceState`, avoiding process-global pointer identity and stale reconnect state. `rtp_sequence.c`, `rtp_sequence.h`, `red_unwrap.c`, and `red_unwrap.h` are repository-local helpers compiled by the same component. The override and helpers remain under the inherited MIT terms.

## Repository-local firmware helpers

`components/peer/rtp_sequence.c` makes stream-epoch classification host-testable, `src/decode_ladder.c` makes the chronological RED/FEC/PLC callback contract host-testable, and `src/prebuffer_ctl.c` implements gap-resume accounting plus the optional adaptive prebuffer. Their headers and host tests are original repository-local code under the root MIT license, not vendored dependency code.

## Deliberate production-only vendoring

Upstream dependency test trees, generated artifacts, example certificates, and test private keys are not included. They are unnecessary for this ESP-IDF component build and create avoidable secret-scanner noise in a public firmware repository. License and production source files required by the build are retained.

When refreshing a dependency:

1. record the exact upstream revision;
2. preserve its license files;
3. reapply/review local overrides against upstream;
4. build from a clean checkout;
5. run `gitleaks` and `scripts/publication_gate.py` before committing.

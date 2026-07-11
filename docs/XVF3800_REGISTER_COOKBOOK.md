# XVF3800 register cookbook

This is the hardware contract for the XVF3800 control port used by this firmware. It describes the values implemented in `src/media.cpp`, why they exist, and how to tune them without turning a field experiment into an unrepeatable boot state.

Verified against the ReSpeaker XVF3800 USB 4-Mic Array with XVF3800 DFU firmware v6.34.4. XMOS command maps can differ by firmware image; read back known anchors before assuming these numeric IDs apply to another image.

## Golden rules

1. **The tested XVF3800 is the I2S master.** Configure the ESP32 as `I2S_ROLE_SLAVE`.
2. **Route STT from category 7, not the conferencing output.** Set left output to `[7,3]` and enable ASR output.
3. **Use a repeated-start I2C read.** A split write followed by read can return stale framing instead of the live value.
4. **Retry control status `WAIT` and `RETRY`.** They are protocol states, not immediate hard failures.
5. **Reapply the profile at every boot.** XVF parameters are volatile across XMOS reset/power cycle.
6. **Tune one variable at a time and preserve gain headroom.** Lower echo residual is not a pass if barge-in speech is also removed.

## Bus and wire format

- XVF3800 control-port I2C address: `0x2c`
- AIC3104 codec I2C address: `0x18`
- Write request: `[resid, cmd, length, data...]`
- Read request: `[resid, cmd | 0x80, response_length]`
- Read response: `[status, value...]`

Status values used here:

| Status | Value | Action |
|---|---:|---|
| done | `0x00` | consume the returned value |
| wait | `0x01` | retry, bounded |
| servicer-command retry | `0x40` | retry, bounded |

The firmware uses `i2c_master_transmit_receive()` and at most eight attempts. It does not insert a STOP between request and response.

## Servicer map

| RESID | Servicer | Role |
|---:|---|---|
| 17 | post-processor (`PP`) | AGC, limiter, noise/echo attenuation, double-talk sensitivity |
| 20 | general-purpose output (`GPO`) | LED ring and physical mute GPIO |
| 33 | acoustic echo canceller (`AEC`) | ASR output, far-end reference, beam telemetry |
| 35 | audio manager (`AUDIO_MGR`) | gains, output mux, upsample flags, system delay |

## Audio-manager commands (RESID 35)

| Command | ID | Type | Firmware value | Why |
|---|---:|---|---|---|
| `MIC_GAIN` | 0 | float | build default `60.0` | Leaves near-field headroom; AGC can recover far-field level. |
| `REF_GAIN` | 1 | float | `1.0` | Avoids the vendor sample's linear 8x far-reference overdrive. |
| `OP_UPSAMPLE` | 14 | two bytes | `[1,1]` in dual-stream mode | Places both 16 kHz lanes on the 48 kHz I2S slots. |
| `OP_L` | 15 | two bytes | `[7,3]` | Clean ASR auto-selected beam for STT. |
| `OP_R` | 19 | two bytes | `[7,3]` mono; `[6,3]` dual | Mono mirrors ASR; dual preserves a post-processed wake/barge lane. |
| `SYS_DELAY` | 26 | int32 | `12` | Tested far-reference alignment on this board. |

Output tuples are `[category, source]`:

- category 6: processed/voice-communication path; useful for wake/barge experiments, not the default STT lane;
- category 7: clean ASR path after beam selection;
- source 3: automatic beam selection.

## AEC commands (RESID 33)

| Command | ID | Type | Firmware value | Notes |
|---|---:|---|---|---|
| `HPFONOFF` | 1 | int32 | `2` | High-pass mode used by the tested profile. |
| `FAR_EXTGAIN` | 5 | float | build default `12.0` dB | Scales the far-end reference seen by AEC. Tune with care. |
| `ASROUTONOFF` | 35 | int32 | `1` | Required mate to category-7 output routing. |
| `ASROUTGAIN` | 36 | float | runtime tuning only | Fixed ASR-path output gain. |
| `FIXEDBEAMSONOFF` | 37 | int32 | `0` | Keep adaptive beam selection. |
| `AZIMUTH_VALUES` | 75 | float array, read | telemetry | Used for LED direction. |
| `SPENERGY_VALUES` | 80 | float array, read | telemetry | Speech-energy signal. |

## Post-processor commands (RESID 17)

| Command | ID | Type | Firmware value |
|---|---:|---|---:|
| `AGCONOFF` | 10 | int32 | 1 |
| `AGCMAXGAIN` | 11 | float | 64.0 |
| `AGCDESIREDLEVEL` | 12 | float | build default 0.0045 |
| `AGCGAIN` | 13 | float | 2.0 |
| `LIMITONOFF` | 19 | int32 | 1 |
| `MIN_NS` | 21 | float | 0.15 |
| `MIN_NN` | 22 | float | 0.51 |
| `ECHOONOFF` | 23 | int32 | 1 |
| `NLATTENONOFF` | 27 | int32 | 1 |
| `DTSENSITIVE` | 31 | int32 | 30 |
| `ATTNS_MODE` | 32 | int32 | 1 |
| `ATTNS_NOMINAL` | 33 | float | 1.0 |
| `ATTNS_SLOPE` | 34 | float | 1.0 |

`DTSENSITIVE=30` was the largest measured echo-residual lever on the tested assembly. A higher value can suppress near-end double talk, so every increase must be followed by a real barge-in test.

## GPO and codec

### LED ring

- GPO RESID 20, command 18
- 48 data bytes: 12 LEDs × `[blue, green, red, 0x00]`
- The full transaction is 51 bytes including the control header; do not route it through a helper capped at 29 data bytes.

### Physical mute

The known GPO mute pin is 30. This firmware defines the constants but does not expose a public mute endpoint. Treat physical-mute behavior as unverified until a complete user-facing path and test are added.

### AIC3104 DAC

Registers `0x2b` and `0x2c` control left/right digital attenuation. `0x00` is 0 dB and each increment is -0.5 dB. The firmware default `0x0c` is -6 dB. Value 128 mutes in the runtime tuning handler.

## Boot profile sequence

`configure_xvf3800_dsp_profile()` runs after I2C device creation and the XVF version probe. In order, it:

1. writes the output routing;
2. sets reference/microphone gains and system delay;
3. enables ASR output, adaptive beamforming, HPF, and far-end reference gain;
4. enables/configures AGC, limiter, echo processing, noise floors, and double-talk sensitivity;
5. logs acknowledged writes;
6. starts the optional LED boot splash.

The important invariant is not the exact order between independent values; it is that all volatile routing and AEC values are written on every boot and failures remain visible.

## ASR routing tradeoff

Category 7 preserves speech detail for STT but bypasses parts of the voice-communication post-processor that reduce residual echo. Do not fix self-hearing by switching STT back to category 6; that reintroduces garbled recognition. Tune the core AEC reference and double-talk controls, then add server-side echo gating only if needed.

The optional dual-stream mode keeps category 7 on the left for STT and category 6 on the right for wake/barge scoring. The server must preserve and split those channels; mixing them defeats the contract.

## Runtime tuning endpoint

`POST /xvf/tune?param=<name>&value=<number>` accepts this allowlist:

| Name | Target | Type |
|---|---|---|
| `far_extgain` | AEC far reference gain | float |
| `asr_gain` | AEC ASR output gain | float |
| `ref_gain` | audio-manager reference gain | float |
| `mic_gain` | audio-manager microphone gain | float |
| `sys_delay` | audio-manager reference delay | int32 |
| `echo_onoff` | PP echo processing | int32 |
| `nlatten_onoff` | PP nonlinear attenuation | int32 |
| `dtsensitive` | PP double-talk sensitivity | int32 |
| `dac_atten` | AIC3104 attenuation, 0..128 | int |

Writes are volatile. The endpoint does not persist a winning value and does not authenticate the caller.

## Repeatable AEC tuning procedure

1. Keep the mic path live; do not hide echo with a blanket microphone mute.
2. Play the same calibrated signal through the device speaker.
3. Measure inbound microphone RMS during playback at the Pipecat server. Average at least three runs per value because network/audio timing is noisy.
4. Sweep one parameter at a time in this order:
   1. `dtsensitive`;
   2. `far_extgain`;
   3. `sys_delay` only if buffer or I2S timing changed;
   4. gain staging (`ref_gain`, `dac_atten`) if the speaker chain clips.
5. After each candidate, test both conditions:
   - playback does not produce a second assistant turn;
   - a real person talking over playback is still detected.
6. Bake the winner into `configure_xvf3800_dsp_profile()`, rebuild, reboot, and repeat the same measurement. Runtime tuning alone disappears on power cycle.

Measured on the tested assembly, `DTSENSITIVE=30` plus `FAR_EXTGAIN=12 dB` reduced a representative self-echo RMS from roughly 1389 to roughly 380. Those numbers are evidence for that fixture, not universal thresholds for every enclosure and speaker.

## Regression gates

- `scripts/i2s_role_lint.py` checks the ESP32 remains I2S slave.
- `scripts/asr_routing_lint.py` checks category 7 and ASR mode remain present.
- `scripts/dualstream_lint.py` checks the optional two-lane compile gate stays coherent.

If a source refactor makes a guard unable to parse, the guard exits 2. Re-establish the invariant and update the guard; do not weaken it to a silent pass.

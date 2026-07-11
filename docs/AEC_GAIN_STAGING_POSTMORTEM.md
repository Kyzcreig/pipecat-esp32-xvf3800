# AEC and playback gain-staging postmortem

## Summary

Playback rasp was not one bug. The transport path had real timing and sample-rate defects, but after those were fixed the analog path could still crackle because a vendor sample value set the far/reference gain to **8.0 linear**.

A linear factor of 8 is **+18.06 dB**. Combined with a 0 dB AIC3104 DAC setting, it over-drove the output chain. The production profile now uses:

- `AUDIO_MGR REF_GAIN = 1.0`;
- AIC3104 DAC attenuation `0x0c` = -6 dB;
- headroom-scaled interpolation taps;
- `PIPECAT_AEC_FAR_EXTGAIN_DB` independently tuned for the AEC reference model.

Reference gain, acoustic output level, and AEC far-end gain are related but not interchangeable controls.

## Symptoms

- A static sine could measure clean while real streamed speech sounded raspy.
- Fixing network/I2S timing improved speech but did not eliminate level-dependent crackle.
- Lowering level reduced the defect without changing packet-loss or underrun counters.
- The HTTP self-test and `/playback/stats` made it possible to separate transport timing from the codec/analog layer.

## Why the vendor default was dangerous

The sample's `REF_GAIN=8.0` is a multiplier, not a decibel value. Treating it as a modest gain setting missed the scale:

```text
20 * log10(8) = 18.06 dB
```

That is eight times the sample amplitude before later gain stages. A chain that is individually legal at each API boundary can still clip when those gains multiply.

The correct response was not to tune the FIR, the AEC, and the DAC as one opaque loudness knob. Each stage has a separate job:

| Stage | Job | Failure when too hot |
|---|---|---|
| FIR headroom | convert 16 kHz PCM to 48 kHz without imaging/clipping | intersample peaks clip before I2S |
| `AUDIO_MGR REF_GAIN` | scale the far/reference path through the XVF audio manager | digital/analog playback overdrive |
| AIC3104 DAC attenuation | set codec output headroom | codec/output-stage clipping |
| `AEC FAR_EXTGAIN` | tell AEC how strong the far-end reference is | poor cancellation or near-end suppression |
| microphone gain/AGC | deliver usable near-end speech | near-field clipping or noisy far-field capture |

## Investigation sequence

1. **Exonerate the source.** Use a known-clean PCM artifact.
2. **Split static from dynamic.** A clean continuous tone but bad streamed speech implicates scheduling, packet timing, or frame boundaries before frequency response.
3. **Decouple playback.** Move I2S writes off the WebRTC callback into a dedicated ring-buffer consumer.
4. **Instrument delivery.** Track I2S write failures, ring underruns, RTP gaps, late drops, PLC, and FEC.
5. **Bypass network and codec.** Use `/playback/selftest` to send an embedded PCM clip through ring -> FIR -> I2S -> codec.
6. **Run a level ladder.** Change one gain stage at a time while counters remain visible.
7. **Bake the lowest-complexity stable values.** Keep a separate AEC sweep for cancellation quality and barge-in preservation.

## Resampler headroom

The original interpolation fixes also exposed an intersample-peak problem. Unity-gain 24-tap FIR coefficients could produce a worst-case accumulated magnitude above int16 full scale for adversarial input. The committed coefficients are scaled by 0.668 (about -3.5 dB) so the proven worst-case sum remains below the rail. Loudness is recovered later where headroom is explicit.

Do not normalize the coefficients back to unity without rerunning the worst-case sum check in `scripts/gen_fir_taps.py` and an acoustic level test.

## AEC tuning after gain staging

Gain staging comes first. An AEC sweep performed while the playback path clips optimizes against a nonlinear signal the canceller cannot model correctly.

After playback is clean:

1. keep `REF_GAIN=1.0` and a non-clipping DAC setting;
2. play a repeatable far-end signal;
3. measure inbound residual;
4. sweep `dtsensitive`, then `far_extgain`, then `sys_delay` if timing changed;
5. verify real double-talk/barge-in, not only the residual number;
6. bake the winning values and retest after power cycle.

## What not to do

- Do not assume a vendor sample value is a safe board default.
- Do not use one gain to compensate for clipping created by another stage.
- Do not treat zero underruns as proof the analog path is clean.
- Do not use a static sine alone as proof streamed speech is clean.
- Do not switch STT back to category 6 to hide echo; it damages ASR detail.
- Do not persist runtime `/xvf/tune` values implicitly. Deliberately bake and review them.

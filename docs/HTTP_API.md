# Device HTTP API

The firmware runs an ESP-IDF HTTP server on port 80. It is an operational/debug interface, not a public web API.

## Security model

There is no authentication, authorization, TLS, CSRF defense, or rate limiting. Any host that can reach port 80 can tune audio registers, upload firmware, trigger playback, or roll the application back.

- Put devices on a trusted/isolated network.
- Block port 80 at every untrusted boundary.
- Never port-forward these endpoints.
- Treat an exposed device as remotely flashable.

## `GET /ota/status`

Returns the running application state:

```json
{
  "booted_slot": "ota_0",
  "app_valid": true,
  "ota_state": "valid",
  "sha256": "<64 lowercase hex characters>",
  "uptime_s": 123,
  "firmware_version": "<ESP-IDF app version>",
  "satellite_id": "my-satellite",
  "mdns_hostname": "my-satellite-xvf3800.local"
}
```

If no upload SHA is saved for the running slot, the handler calculates the partition SHA-256.

## `POST /ota/upload`

Request body is a raw ESP-IDF application image with a valid `Content-Length`.

```sh
curl --fail --show-error --request POST \
  --data-binary @build/src.bin \
  "http://<device-host>/ota/upload"
```

Success:

```json
{
  "status": "reboot_pending",
  "boot_slot": "ota_1",
  "sha256": "<64 lowercase hex characters>"
}
```

The handler streams in 4096-byte chunks, hashes while writing, saves SHA/slot metadata in NVS, switches the boot partition, responds, and schedules reboot. A pending image gets 60 seconds to pass Wi-Fi, mDNS, HTTP-server, and XVF3800-presence checks. Otherwise it is marked invalid and rolled back.

Errors include missing content length, no update partition, allocation/write/finalization failure, and boot-partition switch failure.

## `POST /ota/rollback`

Returns `{"status":"rollback_pending"}`, marks the running image invalid, and reboots to the previous valid image. This is destructive and unauthenticated.

## `POST /xvf/tune`

Query parameters:

- `param`: one allowlisted parameter name;
- `value`: parsed by `strtof`; integer-backed controls truncate to int32.

Example:

```sh
curl --fail --show-error --request POST \
  "http://<device-host>/xvf/tune?param=dtsensitive&value=30"
```

Allowlist:

| Parameter | Destination | Type/range enforced by handler |
|---|---|---|
| `far_extgain` | AEC far external gain | float; no explicit range |
| `asr_gain` | AEC ASR output gain | float; no explicit range |
| `ref_gain` | audio-manager reference gain | float; no explicit range |
| `mic_gain` | audio-manager microphone gain | float; no explicit range |
| `sys_delay` | audio-manager system delay | int32 conversion |
| `echo_onoff` | post-processor echo switch | int32 conversion |
| `nlatten_onoff` | nonlinear attenuation switch | int32 conversion |
| `dtsensitive` | double-talk sensitivity | int32 conversion |
| `dac_atten` | AIC3104 left/right DAC attenuation | integer 0..128 |

Success returns `{"ok":true,"param":"...","value":...}`. Missing parameters return 400, unknown names 404, and failed control writes 500.

Writes are volatile. XVF values disappear on XMOS power cycle; codec values disappear on board reset. Use this endpoint for bounded experiments, then review and bake winners into the boot profile.

## `GET /playback/stats`

Returns cumulative counters and active prebuffer:

```json
{
  "frames": 1000,
  "write_fail": 0,
  "underruns": 0,
  "plc": 2,
  "fec_attempts": 8,
  "ptime_mismatches": 0,
  "late_drops": 1,
  "gap_events": 10,
  "stream_resets": 0,
  "packets_received": 1000,
  "red_recovered": 4,
  "red_dup_drops": 0,
  "red_parse_failures": 0,
  "red_profile_mismatches": 0,
  "gap_resumes": 1,
  "prebuffer_ms": 80,
  "prebuffer_effective_ms": 80,
  "prebuffer_transitions": 0
}
```

`fec_attempts` counts every call made with Opus `decode_fec=1`, including failed decoder returns. The Opus API returns a concealed frame when the packet has no FEC payload, so this counter proves that the FEC-capable path ran, not that redundancy was present. `plc` counts explicit null-packet PLC calls; internal concealment during an FEC attempt is not separately observable. `red_recovered` does prove that a matching RFC 2198 block reached the receive path; an SDP offer alone does not. `red_parse_failures` counts malformed PT 63 payloads rejected before Opus. `red_profile_mismatches` counts RED gap packets whose RTP timestamps violate the supported fixed 20 ms Opus profile; those packets fall back to FEC/PLC without selecting a possibly wrong redundant block. `ptime_mismatches` counts ordinary or recovered Opus payloads rejected because they did not contain exactly one 20 ms frame. `stream_resets` counts SSRC changes that safely started a new sequence epoch; decoder initialization also resets the epoch but does not increment the runtime counter. `gap_resumes` counts full ring drains followed by a refill inside `PIPECAT_GAP_RESUME_MS` (750 ms by default), covering a diagnostic blind spot that `underruns` misses. `prebuffer_ms` is the configured base. `prebuffer_effective_ms` includes optional adaptive adjustment, and `prebuffer_transitions` counts growth/decay steps.

Optional `prebuffer_ms=N` changes the runtime prebuffer when `20 <= N <= 1000`:

```sh
curl --fail --show-error \
  "http://<device-host>/playback/stats?prebuffer_ms=80"
```

Out-of-range or unparsable values are ignored; the response reports the active value.

The firmware's static default is 80 ms. Adaptive control is disabled unless `PIPECAT_ADAPTIVE_PREBUFFER=1` was set when CMake configured the build; when disabled, the effective value equals the base exactly.

## `POST /playback/selftest`

Queues the embedded 16 kHz mono PCM clip through ring -> FIR -> I2S -> codec -> speaker, bypassing network and Opus.

Success:

```json
{"ok":true,"path":"flash->ring->FIR->I2S (no opus/network)"}
```

The endpoint acknowledges queueing, not acoustic success. Use an independent microphone or a human listener for the final air-path verdict.

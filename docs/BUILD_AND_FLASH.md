# Build and flash

This guide is for the ReSpeaker XVF3800 + XIAO ESP32-S3 target in this repository. Commands were verified against ESP-IDF 5.5.4 on macOS. Other 5.5 patch releases may work but are not the pinned path.

## Requirements

- ESP-IDF **v5.5.4**
- Python supported by that IDF installation
- Git, CMake, Ninja, and a serial-capable USB cable
- An HTTP SmallWebRTC offer endpoint reachable from the device
- The board's own XVF3800 firmware already installed; v6.34.4 is the tested image

## Install the pinned ESP-IDF

Use a dedicated checkout rather than a moving `stable` branch:

```sh
mkdir -p "$HOME/esp"
git clone --branch v5.5.4 --depth 1 --recursive \
  https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v5.5.4"
cd "$HOME/esp/esp-idf-v5.5.4"
./install.sh esp32s3
. ./export.sh
idf.py --version
```

The last command must report ESP-IDF 5.5.4.

### `IDF_PYTHON_ENV_PATH` gotcha

A stale shell can point IDF at a Python environment created for a different Python or IDF version. Typical failures are `ESP-IDF Python virtual environment ... not found` and `No module named esp_idf_monitor`.

For the official installation above, clear stale overrides before sourcing the pinned export script:

```sh
unset IDF_PYTHON_ENV_PATH IDF_TOOLS_PYTHON_CMD VIRTUAL_ENV
export IDF_PATH="$HOME/esp/esp-idf-v5.5.4"
. "$IDF_PATH/export.sh"
command -v idf.py
idf.py --version
```

If you intentionally use a PlatformIO-managed IDF, point both variables at the environment that PlatformIO installed for that exact IDF, then invoke `idf.py` through that Python:

```sh
export IDF_PATH="$HOME/.platformio/packages/framework-espidf"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/<matching-idf-env>"
export IDF_TOOLS_PYTHON_CMD="$IDF_PYTHON_ENV_PATH/bin/python"
. "$IDF_PATH/export.sh"
"$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" --version
```

Do not copy an environment-directory name from another machine; discover the matching local environment.

## Configure secrets and identity

```sh
cp .env.example .env
cp devices.example.json devices.json
```

Edit `.env` and `devices.json`. Both files are ignored. Keep Wi-Fi credentials, internal server addresses, device MACs, and personal location labels out of commits.

Load the build environment:

```sh
set -a
. ./.env
set +a
```

CMake rejects empty Wi-Fi credentials and a missing SmallWebRTC URL.

## Run guards before building

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

The hardware-contract lints fail closed if they cannot parse the source. The host C tests compile the RFC 2198 parser, per-decoder RTP sequence state, decode ladder, and prebuffer state machine with `cc`; they do not require ESP-IDF. A parse or compile failure is not permission to continue.

### Playback resilience options

The default `.env.example` keeps the optional adaptive controller off and uses the standard 750 ms full-drain/refill diagnostic window:

```sh
PIPECAT_ADAPTIVE_PREBUFFER=0
PIPECAT_GAP_RESUME_MS=750
```

The static base prebuffer is 80 ms and can be changed at runtime through `/playback/stats?prebuffer_ms=N`. Setting `PIPECAT_ADAPTIVE_PREBUFFER=1` at configure time allows the effective value to grow to 120 or 160 ms after recovery bursts and decay after quiet periods. Keep it off until the counters show that static buffering is insufficient for the target network.

## Configure and build

```sh
idf.py set-target esp32s3
idf.py reconfigure
python3 scripts/verify_build_config.py
idf.py build
```

### CMake bakes identity and URL

`WIFI_SSID`, `WIFI_PASSWORD`, `PIPECAT_SMALLWEBRTC_URL`, device identity, and mDNS values flow through CMake into `build/generated/pipecat_build_config.h`. `idf.py build` can reuse the previous CMake cache after an environment change.

Run `idf.py reconfigure` every time any baked value changes. Then run `verify_build_config.py`; it compares the generated header with the current environment and prints only field names, never values. This prevents flashing an image that silently contains the previous device's URL or identity.

For a completely clean configuration:

```sh
idf.py fullclean
idf.py set-target esp32s3
idf.py reconfigure
python3 scripts/verify_build_config.py
idf.py build
```

## USB flash with identity preflight

Populate `devices.json` with the board MAC returned by `esptool read_mac`, keyed by a stable non-location device id. Then:

```sh
FLASH_ASSERT_DEVICE=my-satellite scripts/flash.sh flash monitor
```

`flash.sh` reads the USB board MAC before any `flash` command and refuses a mismatch. For a brand-new board that is not yet registered, identify it first:

```sh
python3 scripts/device_identity.py --port "$PORT"
```

Add the returned MAC to `devices.json`, then use the asserted flash path. `FLASH_ASSERT_DEVICE=skip` exists for recovery but removes the safety check; do not make it your default.

Opening the serial port can reset the ESP32 and drop its WebRTC connection. Do not treat a serial-monitor-induced disconnect as a network failure.

## First boot checks

Expected boot evidence includes:

- XVF3800 version probe succeeds.
- DSP profile writes are acknowledged.
- Wi-Fi obtains an address.
- mDNS and the HTTP server start.
- WebRTC reaches connected state.
- Quiet inbound audio has a low RMS rather than a full-scale pinned floor.

On the server, feed inbound-RMS logs to:

```sh
python3 scripts/mic_sanity_check.py --file /path/to/inbound-audio.log
```

## HTTP OTA

The first migration to this firmware requires USB. Later builds can use the firmware's own HTTP OTA path:

```sh
curl --fail --show-error --request POST \
  --data-binary @build/src.bin \
  "http://<device-host>/ota/upload"
curl --fail --show-error "http://<device-host>/ota/status"
```

The upload response contains the target slot and SHA-256. The device reboots, validates Wi-Fi, mDNS, HTTP, and XVF3800 health, then marks the image valid. If health does not pass within 60 seconds while the image is pending verification, it rolls back.

The HTTP API has no authentication or TLS. Restrict it to a trusted/isolated network.

## Rollback

Manual OTA rollback:

```sh
curl --fail --show-error --request POST "http://<device-host>/ota/rollback"
```

If the device cannot join Wi-Fi, recover over USB with a previously archived full flash image. Keep those images outside Git, with their SHA-256 and source commit recorded separately.

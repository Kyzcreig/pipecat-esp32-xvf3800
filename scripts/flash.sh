#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [[ -f .env ]]; then
  set -a
  # shellcheck source=/dev/null
  . ./.env
  set +a
fi

: "${WIFI_SSID:?Set WIFI_SSID in the environment or .env}"
: "${WIFI_PASSWORD:?Set WIFI_PASSWORD in the environment or .env}"
: "${PIPECAT_SMALLWEBRTC_URL:?Set PIPECAT_SMALLWEBRTC_URL in the environment or .env}"

IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}"
PORT="${PORT:-}"
if [[ ! -f "$IDF_PATH/export.sh" ]]; then
  echo "flash: ESP-IDF export.sh not found at $IDF_PATH/export.sh" >&2
  exit 2
fi

if [[ -n "${IDF_PYTHON_ENV_PATH:-}" ]]; then
  export IDF_TOOLS_PYTHON_CMD="${IDF_TOOLS_PYTHON_CMD:-$IDF_PYTHON_ENV_PATH/bin/python}"
fi
# shellcheck source=/dev/null
. "$IDF_PATH/export.sh" >/dev/null

idf_py=(idf.py)
if [[ -n "${IDF_PYTHON_ENV_PATH:-}" ]]; then
  idf_py=("$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py")
fi

if [[ $# -eq 0 ]]; then
  set -- build
fi

if [[ ! -f sdkconfig ]] || ! grep -q '^CONFIG_IDF_TARGET="esp32s3"' sdkconfig; then
  "${idf_py[@]}" set-target esp32s3
fi

# Build-time values are cached by CMake. Always reconfigure before build/flash.
"${idf_py[@]}" reconfigure
python3 scripts/verify_build_config.py

if [[ " $* " == *" flash "* ]]; then
  assert_device="${FLASH_ASSERT_DEVICE:-}"
  if [[ -z "$assert_device" ]]; then
    echo "flash: refusing to flash without FLASH_ASSERT_DEVICE=<id> (or =skip for recovery)" >&2
    exit 3
  fi
  if [[ "$assert_device" != "skip" ]]; then
    identity_args=(--assert "$assert_device")
    [[ -n "$PORT" ]] && identity_args+=(--port "$PORT")
    python3 scripts/device_identity.py "${identity_args[@]}"
  fi
fi

command=("${idf_py[@]}")
[[ -n "$PORT" ]] && command+=(-p "$PORT")
command+=("$@")
exec "${command[@]}"

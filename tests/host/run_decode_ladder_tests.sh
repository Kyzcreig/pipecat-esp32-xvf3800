#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${TMPDIR:-/tmp}/pipecat-decode-ladder-tests-$$"
trap 'rm -f "$BIN"' EXIT

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT/src" -I"$ROOT/components/peer" \
  "$ROOT/tests/host/test_decode_ladder.c" \
  "$ROOT/src/decode_ladder.c" \
  "$ROOT/components/peer/red_unwrap.c" \
  -o "$BIN"
"$BIN"

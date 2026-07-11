#!/usr/bin/env bash
# Host-side RTP sequence epoch tests; no ESP-IDF installation required.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PEER="$ROOT/components/peer"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

CC="${CC:-cc}"
"$CC" -std=c99 -Wall -Wextra -Werror -I "$PEER" \
  "$ROOT/tests/host/test_rtp_sequence.c" "$PEER/rtp_sequence.c" \
  -o "$OUT/test_rtp_sequence"
"$OUT/test_rtp_sequence"

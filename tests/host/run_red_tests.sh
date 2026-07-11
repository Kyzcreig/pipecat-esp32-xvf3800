#!/usr/bin/env bash
# Host-side RFC 2198 parser/recovery tests; no ESP-IDF installation required.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PEER="$ROOT/components/peer"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

CC="${CC:-cc}"
"$CC" -std=c99 -Wall -Wextra -Werror -I "$PEER" \
  "$ROOT/tests/host/test_red_unwrap.c" "$PEER/red_unwrap.c" \
  -o "$OUT/test_red_unwrap"
"$OUT/test_red_unwrap"

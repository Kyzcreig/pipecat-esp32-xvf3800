#!/usr/bin/env bash
# Host-side gap-resume/adaptive-prebuffer tests; no ESP-IDF installation required.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/src"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

CC="${CC:-cc}"
"$CC" -std=c99 -Wall -Wextra -Werror -I "$SRC" \
  "$ROOT/tests/host/test_prebuffer_ctl.c" "$SRC/prebuffer_ctl.c" \
  -o "$OUT/test_prebuffer_ctl"
"$OUT/test_prebuffer_ctl"

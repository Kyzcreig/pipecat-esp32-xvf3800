#!/usr/bin/env python3
"""Verify CMake baked the current environment without printing sensitive values."""
from __future__ import annotations

import ast
import os
import re
import sys
from pathlib import Path

HEADER = Path("build/generated/pipecat_build_config.h")
STRING_FIELDS = (
    "WIFI_SSID",
    "WIFI_PASSWORD",
    "PIPECAT_SMALLWEBRTC_URL",
    "PIPECAT_SATELLITE_ID",
    "PIPECAT_MDNS_HOSTNAME",
    "PIPECAT_MDNS_INSTANCE",
)


def parse_strings(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for name in STRING_FIELDS:
        match = re.search(rf"^#define\s+{name}\s+(\"(?:\\.|[^\"])*\")\s*$", text, re.MULTILINE)
        if match:
            value = ast.literal_eval(match.group(1))
            if isinstance(value, str):
                values[name] = value
    return values


def main() -> int:
    if not HEADER.is_file():
        print(f"build-config: missing {HEADER}; run idf.py reconfigure", file=sys.stderr)
        return 2
    values = parse_strings(HEADER.read_text())
    failures: list[str] = []
    for name in STRING_FIELDS:
        expected = os.environ.get(name, "")
        actual = values.get(name)
        if not expected:
            failures.append(f"{name}: environment value is empty")
        elif actual is None:
            failures.append(f"{name}: define is missing")
        elif actual != expected:
            failures.append(f"{name}: generated value does not match current environment")
    if failures:
        for failure in failures:
            print(f"build-config: FAIL — {failure}", file=sys.stderr)
        print("build-config: run idf.py reconfigure after changing any build value", file=sys.stderr)
        return 1
    print("build-config: OK — all baked string fields match the current non-empty environment")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Fail closed if the publishable tree contains private deployment material."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEXT_SUFFIXES = {
    "", ".c", ".cc", ".cmake", ".cpp", ".h", ".hpp", ".json", ".md", ".py",
    ".sh", ".txt", ".yaml", ".yml", ".example",
}
SECRET_BASENAMES = {".env", "devices.json", "credentials.json", "secrets.json"}
FORBIDDEN_PARTS = (
    "kit" + "chen",
    "thea" + "ter",
    "clan" + "ker",
    "sky" + "net",
    "ace" + "-ai",
    "Wi-" + "Fight this Feeling",
    "hello" + "kitty",
)
PRIVATE_IP = re.compile(
    r"(?<![0-9])(?:10(?:\.[0-9]{1,3}){3}|192\.168(?:\.[0-9]{1,3}){2}|"
    r"172\.(?:1[6-9]|2[0-9]|3[01])(?:\.[0-9]{1,3}){2})(?![0-9])"
)
PERSONAL_HOME = re.compile(r"/(?:Users|home)/[A-Za-z0-9._-]+/")
HARDWARE_ID = re.compile(
    r"(?i)(?<![0-9a-f])(?:[0-9a-f]{2}:){5}[0-9a-f]{2}(?![0-9a-f])"
)
DOCUMENTATION_HARDWARE_ID = "02:00" + ":00:00:00:01"
PRIVATE_KEY = re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----")


def publishable_files() -> list[Path]:
    try:
        output = subprocess.run(
            ["git", "-C", str(ROOT), "ls-files", "-z"],
            check=True,
            capture_output=True,
        ).stdout
    except subprocess.CalledProcessError:
        output = b""
    if output:
        return [ROOT / item.decode() for item in output.split(b"\0") if item]
    return [path for path in ROOT.rglob("*") if path.is_file() and ".git" not in path.parts]


def main() -> int:
    failures: list[str] = []
    files = publishable_files()
    for path in files:
        relative = path.relative_to(ROOT)
        if path.name in SECRET_BASENAMES:
            failures.append(f"tracked secret/local file: {relative}")
        if "build" in relative.parts or path.suffix.lower() in {
            ".bin", ".elf", ".key", ".map", ".p12", ".pem", ".pfx"
        }:
            failures.append(f"generated or key artifact: {relative}")
        if path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        try:
            text = path.read_text(errors="strict")
        except (UnicodeDecodeError, OSError):
            continue
        for line_number, line in enumerate(text.splitlines(), 1):
            if PRIVATE_IP.search(line):
                failures.append(f"private IP: {relative}:{line_number}")
            if PERSONAL_HOME.search(line):
                failures.append(f"personal home path: {relative}:{line_number}")
            for match in HARDWARE_ID.finditer(line):
                allowed_example = (
                    relative == Path("devices.example.json")
                    and match.group(0).casefold() == DOCUMENTATION_HARDWARE_ID
                )
                if not allowed_example:
                    failures.append(f"hardware identity: {relative}:{line_number}")
            if PRIVATE_KEY.search(line):
                failures.append(f"private key material: {relative}:{line_number}")
            folded = line.casefold()
            for value in FORBIDDEN_PARTS:
                if value.casefold() in folded:
                    failures.append(f"private identifier/credential: {relative}:{line_number}")
                    break
    if failures:
        for failure in sorted(set(failures)):
            print(f"publication-gate: FAIL — {failure}", file=sys.stderr)
        return 1
    print(f"publication-gate: OK — scanned {len(files)} publishable files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

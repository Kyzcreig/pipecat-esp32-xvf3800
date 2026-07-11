#!/usr/bin/env python3
"""Resolve the USB-connected board identity before any flash operation."""
from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
from glob import glob
from pathlib import Path

MAC_RE = re.compile(r"MAC:\s*([0-9a-fA-F:]{17})")


def default_port() -> str | None:
    ports = sorted(glob("/dev/cu.usbmodem*") + glob("/dev/ttyUSB*") + glob("/dev/ttyACM*"))
    return ports[0] if ports else None


def read_mac(port: str, esptool: str = "esptool") -> str | None:
    base = ["--chip", "esp32s3", "-p", port, "--before", "default_reset",
            "--after", "no_reset", "read_mac"]
    candidates: list[list[str]] = []
    if esptool and esptool != "esptool":
        candidates.append(shlex.split(esptool) + base)
    candidates.append(["esptool"] + base)
    candidates.append([sys.executable, "-m", "esptool"] + base)
    for python in glob(str(Path.home() / ".espressif/python_env/*/bin/python")):
        candidates.append([python, "-m", "esptool"] + base)

    for command in candidates:
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        match = MAC_RE.search((result.stdout or "") + (result.stderr or ""))
        if match:
            return match.group(1).lower()
    return None


def load_registry(path: Path) -> dict[str, str]:
    try:
        raw = json.loads(path.read_text())
    except FileNotFoundError:
        return {}
    except (json.JSONDecodeError, OSError) as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    if not isinstance(raw, dict):
        raise ValueError(f"{path} must contain an object mapping device ids to MAC addresses")
    registry: dict[str, str] = {}
    for device_id, mac in raw.items():
        if not isinstance(device_id, str) or not isinstance(mac, str):
            raise ValueError(f"{path} keys and values must be strings")
        if not re.fullmatch(r"[0-9a-fA-F:]{17}", mac):
            raise ValueError(f"{path}: invalid MAC for device id {device_id!r}")
        registry[device_id] = mac.lower()
    return registry


def identify(mac: str, registry: dict[str, str]) -> str | None:
    matches = [device_id for device_id, known_mac in registry.items() if known_mac == mac.lower()]
    return matches[0] if len(matches) == 1 else None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port (default: first supported USB serial device)")
    parser.add_argument("--assert", dest="assert_device", help="required device id")
    parser.add_argument("--registry", default="devices.json", help="local device-id-to-MAC JSON")
    parser.add_argument("--esptool", default="esptool")
    args = parser.parse_args(argv)

    port = args.port or default_port()
    if not port:
        print("device-identity: no supported USB serial port found", file=sys.stderr)
        return 2
    try:
        registry = load_registry(Path(args.registry))
    except ValueError as exc:
        print(f"device-identity: {exc}", file=sys.stderr)
        return 2

    mac = read_mac(port, args.esptool)
    if not mac:
        print(f"device-identity: could not read MAC on {port}; do not flash blind", file=sys.stderr)
        return 2
    device_id = identify(mac, registry)
    print(f"device-identity: {port} MAC={mac} device={device_id or 'UNKNOWN'}")

    if args.assert_device:
        expected = registry.get(args.assert_device)
        if expected is None:
            print(f"device-identity: asserted id {args.assert_device!r} is absent from {args.registry}",
                  file=sys.stderr)
            return 1
        if expected != mac:
            print(f"device-identity: mismatch; connected board is not {args.assert_device!r}",
                  file=sys.stderr)
            return 1
        print(f"device-identity: assertion passed for {args.assert_device!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

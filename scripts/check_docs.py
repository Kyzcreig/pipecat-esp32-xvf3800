#!/usr/bin/env python3
"""Check that repository-relative Markdown links resolve without network access."""
from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
LINK_RE = re.compile(r"(?<!!)\[[^]]*\]\(([^)]+)\)")


def main() -> int:
    failures: list[str] = []
    checked = 0
    for document in sorted(ROOT.rglob("*.md")):
        if any(part in {".git", "build", "managed_components"} for part in document.parts):
            continue
        text = document.read_text(encoding="utf-8", errors="strict")
        for match in LINK_RE.finditer(text):
            raw = match.group(1).strip()
            if not raw or raw.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target_text = raw.split(maxsplit=1)[0].strip("<>").split("#", 1)[0]
            if not target_text:
                continue
            if target_text.startswith("/"):
                failures.append(f"absolute local link: {document.relative_to(ROOT)} -> {raw}")
                continue
            checked += 1
            target = (document.parent / unquote(target_text)).resolve()
            try:
                target.relative_to(ROOT)
            except ValueError:
                failures.append(f"link escapes repository: {document.relative_to(ROOT)} -> {raw}")
                continue
            if not target.exists():
                failures.append(f"missing link: {document.relative_to(ROOT)} -> {raw}")
    if failures:
        for failure in failures:
            print(f"docs-check: FAIL — {failure}", file=sys.stderr)
        return 1
    print(f"docs-check: OK — {checked} repository-relative links resolve")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

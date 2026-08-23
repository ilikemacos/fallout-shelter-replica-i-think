#!/usr/bin/env python3
"""Build the self-contained macOS installer: dist/macOS/haven.sh

The installer is scripts/installer_header.sh with a base64 tarball of the
game source appended after the __HAVEN_PAYLOAD_BELOW__ marker.

    python3 scripts/make_installer.py

Runs on any platform — the produced haven.sh runs on macOS.
"""

from __future__ import annotations
import base64
import io
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "scripts" / "installer_header.sh"
OUT = ROOT / "dist" / "macOS" / "haven.sh"

INCLUDE = ["haven", "scripts", "run.py", "requirements.txt", "README.md", "LICENSE"]
EXCLUDE_DIRS = {"__pycache__", ".git", "build", "dist", ".venv"}


def _filter(info: tarfile.TarInfo):
    parts = Path(info.name).parts
    if any(p in EXCLUDE_DIRS for p in parts):
        return None
    if info.name.endswith((".pyc", ".ico", ".icns")):
        return None
    # deterministic metadata so the installer is reproducible
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mtime = 0
    return info


def build_payload() -> bytes:
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz", compresslevel=9) as tar:
        for name in INCLUDE:
            src = ROOT / name
            if not src.exists():
                raise SystemExit(f"missing {src}")
            tar.add(src, arcname=name, filter=_filter)
    return buf.getvalue()


def main():
    header = HEADER.read_text(encoding="utf-8")
    marker = "__HAVEN_PAYLOAD_BELOW__\n"
    if marker not in header:
        raise SystemExit("header is missing the payload marker")
    header = header[: header.index(marker) + len(marker)]

    payload = build_payload()
    b64 = base64.b64encode(payload).decode("ascii")
    lines = "\n".join(b64[i:i + 76] for i in range(0, len(b64), 76))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write(header)
        f.write(lines)
        f.write("\n")
    OUT.chmod(0o755)
    print(f"Wrote {OUT}  ({OUT.stat().st_size / 1024:.0f} KB, "
          f"payload {len(payload) / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
